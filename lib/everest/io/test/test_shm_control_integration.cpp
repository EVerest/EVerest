// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_client.hpp>
#include <everest/io/shm/control_server.hpp>
#include <everest/io/shm/coordinator.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/topic.hpp>

using everest::lib::io::event::unique_fd;
using everest::lib::io::shm::coordinator;
using everest::lib::io::shm::ring_buffer;
using everest::lib::io::shm::shared_memory;
using everest::lib::io::shm::topic;
using namespace everest::lib::io::shm::control;

namespace {

constexpr auto timeout_ms = 5000;
constexpr auto topic_name = "topic/integration";
constexpr auto multi_topic_a_name = "topic/a";
constexpr auto multi_topic_b_name = "topic/b";
constexpr auto first_payload = "{\"value\":1}";
constexpr auto second_payload = "{\"value\":2}";
constexpr auto topic_a_payload = "{\"topic\":\"a\"}";
constexpr auto topic_b_payload = "{\"topic\":\"b\"}";

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

struct pipe_pair {
    unique_fd read;
    unique_fd write;
};

pipe_pair make_pipe(const char* context) {
    int fds[2]{};
    require(::pipe(fds) == 0, context);
    return pipe_pair{unique_fd(fds[0]), unique_fd(fds[1])};
}

bool write_all(int fd, const void* data, std::size_t size) {
    const auto* cursor = static_cast<const char*>(data);
    while (size > 0) {
        const auto written = ::write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        cursor += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool read_all(int fd, void* data, std::size_t size) {
    auto* cursor = static_cast<char*>(data);
    while (size > 0) {
        const auto bytes = ::read(fd, cursor, size);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes == 0) {
            return false;
        }
        cursor += bytes;
        size -= static_cast<std::size_t>(bytes);
    }
    return true;
}

bool wait_readable(int fd, int milliseconds) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    while (true) {
        const auto ready = ::poll(&descriptor, 1, milliseconds);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        return ready > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR));
    }
}

bool write_byte(int fd, char value) {
    return write_all(fd, &value, sizeof(value));
}

char read_byte(int fd, const char* context) {
    require(wait_readable(fd, timeout_ms), context);
    char value{};
    require(read_all(fd, &value, sizeof(value)), context);
    return value;
}

void write_string(int fd, const std::string& value) {
    const auto size = static_cast<std::uint32_t>(value.size());
    require(write_all(fd, &size, sizeof(size)), "child should write string length");
    require(write_all(fd, value.data(), value.size()), "child should write string payload");
}

std::string read_string(int fd, const char* context) {
    require(wait_readable(fd, timeout_ms), context);
    std::uint32_t size{0};
    require(read_all(fd, &size, sizeof(size)), "parent should read string length");
    std::string value(size, '\0');
    require(read_all(fd, value.data(), value.size()), "parent should read string payload");
    return value;
}

std::optional<handshake_response> handle_next_with_timeout(server& control_server, const char* context) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto response = control_server.handle_next();
        if (response.has_value()) {
            return response;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(false, context);
    return std::nullopt;
}

void wait_for_child(pid_t child, const char* context) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status{0};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = ::waitpid(child, &status, WNOHANG);
        if (result == child) {
            require(WIFEXITED(status) && WEXITSTATUS(status) == 0, context);
            return;
        }
        require(result >= 0 || errno == EINTR, context);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ::kill(child, SIGKILL);
    ::waitpid(child, &status, 0);
    require(false, context);
}

shared_memory map_topic(const topic_mapping& mapping) {
    return shared_memory(mapping.shm_name, ring_buffer::calculate_required_size(mapping.total_slots, mapping.slot_size),
                         false);
}

client_options local_options(const std::string& server_name) {
    client_options opts;
    opts.server_name = server_name;
    opts.server_abstract_namespace = false;
    return opts;
}

void run_subscriber_child(const std::string& server_name, pipe_pair ready_pipe, pipe_pair result_pipe,
                          pipe_pair release_pipe) {
    ::alarm(10);
    ready_pipe.read.close();
    result_pipe.read.close();
    release_pipe.write.close();

    // Keep the session alive for the whole child lifetime: closing it triggers manager-side
    // cleanup of every registration owned by this client.
    session sess(local_options(server_name));
    if (!sess.is_open()) {
        std::cerr << "subscriber session failed to open\n";
        std::_Exit(2);
    }
    auto result = sess.handshake("module-sub", topic_name, role::subscriber);
    if (!result.is_accepted()) {
        std::cerr << "subscriber handshake failed\n";
        std::_Exit(2);
    }

    auto shm = map_topic(result.accepted->mapping);
    std::uint32_t received_count{0};
    std::string received_payload;
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), std::move(result.accepted->fds.broadcast), std::move(result.accepted->fds.ack),
        [&](const std::string& data) {
            ++received_count;
            received_payload = data;
        },
        topic::subscriber_cursor{result.accepted->cursor->write_idx, result.accepted->cursor->sequence},
        result.accepted->subscriber_id.value_or(0U));

    if (!write_byte(ready_pipe.write, 'r')) {
        std::_Exit(3);
    }
    ready_pipe.write.close();

    pollfd broadcast{};
    broadcast.fd = subscriber->get_event_fd()->get_raw_fd();
    broadcast.events = POLLIN;
    if (::poll(&broadcast, 1, timeout_ms) <= 0) {
        std::_Exit(4);
    }

    subscriber->handle_event();
    write_string(result_pipe.write, received_payload);
    if (!write_all(result_pipe.write, &received_count, sizeof(received_count))) {
        std::_Exit(5);
    }
    result_pipe.write.close();

    char release{};
    while (::read(release_pipe.read, &release, sizeof(release)) < 0 && errno == EINTR) {
    }
    std::_Exit(0);
}

void run_publisher_child(const std::string& server_name, pipe_pair command_pipe, pipe_pair done_pipe) {
    ::alarm(10);
    command_pipe.write.close();
    done_pipe.read.close();

    session sess(local_options(server_name));
    if (!sess.is_open()) {
        std::cerr << "publisher session failed to open\n";
        std::_Exit(2);
    }
    auto result = sess.handshake("module-pub", topic_name, role::publisher);
    if (!result.is_accepted()) {
        std::cerr << "publisher handshake failed\n";
        std::_Exit(2);
    }

    auto shm = map_topic(result.accepted->mapping);
    auto publisher = topic::make_publisher(ring_buffer(shm.get_ptr()), std::move(result.accepted->fds.publication),
                                           std::move(result.accepted->fds.release));

    if (!publisher->publish(first_payload, topic::full_buffer_policy::fail_immediately)) {
        std::_Exit(3);
    }
    if (!write_byte(done_pipe.write, '1')) {
        std::_Exit(4);
    }

    const auto command = read_byte(command_pipe.read, "publisher should receive second publish command");
    if (command != '2') {
        std::_Exit(5);
    }
    if (!publisher->publish(second_payload, topic::full_buffer_policy::fail_immediately)) {
        std::_Exit(6);
    }
    if (!write_byte(done_pipe.write, '2')) {
        std::_Exit(7);
    }
    std::_Exit(0);
}

void write_multi_topic_report(int fd, char topic_id, const std::string& payload, std::uint32_t topic_a_count,
                              std::uint32_t topic_b_count) {
    require(write_all(fd, &topic_id, sizeof(topic_id)), "child should write report topic id");
    write_string(fd, payload);
    require(write_all(fd, &topic_a_count, sizeof(topic_a_count)), "child should write topic A count");
    require(write_all(fd, &topic_b_count, sizeof(topic_b_count)), "child should write topic B count");
}

void run_multi_topic_subscriber_child(const std::string& server_name, const std::string& shm_a_name,
                                      const std::string& shm_b_name, pipe_pair ready_pipe, pipe_pair result_pipe,
                                      pipe_pair release_pipe) {
    ::alarm(10);
    ready_pipe.read.close();
    result_pipe.read.close();
    release_pipe.write.close();

    // T-4.009 acceptance: one persistent session can host registrations for many topics.
    session sess(local_options(server_name));
    if (!sess.is_open()) {
        std::cerr << "multi-topic session failed to open\n";
        std::_Exit(2);
    }
    auto result_a = sess.handshake("module-multi-sub", multi_topic_a_name, role::subscriber);
    if (!result_a.is_accepted()) {
        std::cerr << "topic A subscriber handshake failed\n";
        std::_Exit(2);
    }

    auto result_b = sess.handshake("module-multi-sub", multi_topic_b_name, role::subscriber);
    if (!result_b.is_accepted()) {
        std::cerr << "topic B subscriber handshake failed\n";
        std::_Exit(3);
    }

    require(result_a.accepted->mapping.shm_name == shm_a_name, "topic A subscriber should receive topic A mapping");
    require(result_b.accepted->mapping.shm_name == shm_b_name, "topic B subscriber should receive topic B mapping");
    require(result_a.accepted->mapping.shm_name != result_b.accepted->mapping.shm_name,
            "multi-topic subscriber mappings should reference distinct rings");
    require(result_a.accepted->fds.broadcast.is_fd() && result_a.accepted->fds.ack.is_fd(),
            "topic A subscriber should receive broadcast and ACK fds");
    require(result_b.accepted->fds.broadcast.is_fd() && result_b.accepted->fds.ack.is_fd(),
            "topic B subscriber should receive broadcast and ACK fds");

    auto shm_a = map_topic(result_a.accepted->mapping);
    auto shm_b = map_topic(result_b.accepted->mapping);
    std::uint32_t received_a_count{0};
    std::uint32_t received_b_count{0};
    std::string received_a_payload;
    std::string received_b_payload;
    auto subscriber_a = topic::make_subscriber(
        ring_buffer(shm_a.get_ptr()), std::move(result_a.accepted->fds.broadcast),
        std::move(result_a.accepted->fds.ack),
        [&](const std::string& data) {
            ++received_a_count;
            received_a_payload = data;
        },
        topic::subscriber_cursor{result_a.accepted->cursor->write_idx, result_a.accepted->cursor->sequence},
        result_a.accepted->subscriber_id.value_or(0U));
    auto subscriber_b = topic::make_subscriber(
        ring_buffer(shm_b.get_ptr()), std::move(result_b.accepted->fds.broadcast),
        std::move(result_b.accepted->fds.ack),
        [&](const std::string& data) {
            ++received_b_count;
            received_b_payload = data;
        },
        topic::subscriber_cursor{result_b.accepted->cursor->write_idx, result_b.accepted->cursor->sequence},
        result_b.accepted->subscriber_id.value_or(0U));

    if (!write_byte(ready_pipe.write, 'r')) {
        std::_Exit(4);
    }
    ready_pipe.write.close();

    pollfd broadcasts[2]{};
    broadcasts[0].fd = subscriber_a->get_event_fd()->get_raw_fd();
    broadcasts[0].events = POLLIN;
    broadcasts[1].fd = subscriber_b->get_event_fd()->get_raw_fd();
    broadcasts[1].events = POLLIN;
    if (::poll(broadcasts, 2, timeout_ms) <= 0) {
        std::_Exit(5);
    }
    require((broadcasts[0].revents & POLLIN) != 0 || (broadcasts[1].revents & POLLIN) != 0,
            "client broadcast wake should become readable for topic A dispatch");
    subscriber_a->handle_event();
    write_multi_topic_report(result_pipe.write, 'a', received_a_payload, received_a_count, received_b_count);

    broadcasts[0].revents = 0;
    broadcasts[1].revents = 0;
    if (::poll(broadcasts, 2, timeout_ms) <= 0) {
        std::_Exit(6);
    }
    require((broadcasts[0].revents & POLLIN) != 0 || (broadcasts[1].revents & POLLIN) != 0,
            "client broadcast wake should become readable for topic B dispatch");
    subscriber_b->handle_event();
    write_multi_topic_report(result_pipe.write, 'b', received_b_payload, received_a_count, received_b_count);
    result_pipe.write.close();

    char release{};
    while (::read(release_pipe.read, &release, sizeof(release)) < 0 && errno == EINTR) {
    }
    std::_Exit(0);
}

pid_t fork_child(const char* context) {
    const auto child = ::fork();
    require(child >= 0, context);
    return child;
}

struct multi_topic_report {
    char topic_id{};
    std::string payload;
    std::uint32_t topic_a_count{0};
    std::uint32_t topic_b_count{0};
};

multi_topic_report read_multi_topic_report(int fd, const char* context) {
    require(wait_readable(fd, timeout_ms), context);
    multi_topic_report report;
    require(read_all(fd, &report.topic_id, sizeof(report.topic_id)), "parent should read report topic id");
    report.payload = read_string(fd, "parent should read report payload");
    require(read_all(fd, &report.topic_a_count, sizeof(report.topic_a_count)),
            "parent should read topic A receive count");
    require(read_all(fd, &report.topic_b_count, sizeof(report.topic_b_count)),
            "parent should read topic B receive count");
    return report;
}

std::size_t cleanup_until_removed(server& control_server, std::size_t expected) {
    std::size_t removed = 0;
    for (int attempt = 0; attempt < 50 && removed < expected; ++attempt) {
        removed += control_server.cleanup_disconnected_clients().size();
        if (removed < expected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return removed;
}

void test_single_topic_subscriber_and_publisher() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    const auto pid = std::to_string(::getpid());
    const std::string shm_name = "/everest_shm_control_integration_" + pid;
    const std::string server_name = "/tmp/everest-shm-control-integration-" + pid;

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    require(control_server.open(server_name, false), "control server should open");
    control_server.register_topic(topic_name, server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    auto subscriber_ready = make_pipe("subscriber ready pipe should open");
    auto subscriber_result = make_pipe("subscriber result pipe should open");
    auto subscriber_release = make_pipe("subscriber release pipe should open");

    const auto subscriber_pid = fork_child("subscriber fork should succeed");
    if (subscriber_pid == 0) {
        run_subscriber_child(server_name, std::move(subscriber_ready), std::move(subscriber_result),
                             std::move(subscriber_release));
    }
    subscriber_ready.write.close();
    subscriber_result.write.close();
    subscriber_release.read.close();

    const auto subscriber_handshake = handle_next_with_timeout(control_server, "subscriber handshake should arrive");
    require(subscriber_handshake->accepted, "subscriber handshake should be accepted");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "subscriber handshake should add one active subscriber");
    require(read_byte(subscriber_ready.read, "subscriber should report ready") == 'r', "subscriber ready byte");

    auto publisher_command = make_pipe("publisher command pipe should open");
    auto publisher_done = make_pipe("publisher done pipe should open");
    const auto publisher_pid = fork_child("publisher fork should succeed");
    if (publisher_pid == 0) {
        run_publisher_child(server_name, std::move(publisher_command), std::move(publisher_done));
    }
    publisher_command.read.close();
    publisher_done.write.close();

    const auto publisher_handshake = handle_next_with_timeout(control_server, "publisher handshake should arrive");
    require(publisher_handshake->accepted, "publisher handshake should be accepted");
    require(read_byte(publisher_done.read, "publisher should report first publish") == '1',
            "publisher first publish byte");

    require(wait_readable(manager.publication_event_fd()->get_raw_fd(), timeout_ms),
            "manager should receive first publication event");
    require(manager.handle_publication() == 1, "manager should dispatch first publication");
    require(control_server.wake_subscriber_clients(topic_name, {subscriber_handshake->subscriber_id.value()}) == 1U,
            "control server should wake subscriber client for first publication");

    const auto received_payload = read_string(subscriber_result.read, "subscriber should report received payload");
    std::uint32_t received_count{0};
    require(read_all(subscriber_result.read, &received_count, sizeof(received_count)),
            "subscriber should report receive count");
    require(received_payload == first_payload, "subscriber should receive expected payload");
    require(received_count == 1, "subscriber should receive exactly one payload");

    require(wait_readable(manager.ack_event_fd(0)->get_raw_fd(), timeout_ms), "manager should receive subscriber ACK");
    require(manager.handle_ack(0), "manager should release first slot after subscriber ACK");

    require(write_byte(publisher_command.write, '2'), "parent should command second publish");
    publisher_command.write.close();
    require(read_byte(publisher_done.read, "publisher should report second publish") == '2',
            "publisher second publish byte");
    require(wait_readable(manager.publication_event_fd()->get_raw_fd(), timeout_ms),
            "manager should receive second publication event");
    require(manager.handle_publication() == 1, "manager should dispatch second publication");
    require(control_server.wake_subscriber_clients(topic_name, {subscriber_handshake->subscriber_id.value()}) == 1U,
            "control server should wake subscriber client for second publication");
    require(rb.get_slot_header(0)->target_subscribers.load(std::memory_order_acquire) == 1,
            "second slot should initially target the live subscriber");
    require(rb.get_slot_header(0)->ack_count.load(std::memory_order_acquire) == 0,
            "second slot should be pending subscriber ACK before cleanup");

    subscriber_release.write.close();
    wait_for_child(subscriber_pid, "subscriber child should exit cleanly");
    require(cleanup_until_removed(control_server, 1) >= 1,
            "manager should remove at least the closed subscriber session");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "session cleanup should remove active subscriber target");
    require(rb.get_slot_header(0)->target_subscribers.load(std::memory_order_acquire) == 0,
            "session cleanup should release pending slot target");

    wait_for_child(publisher_pid, "publisher child should exit cleanly");

    control_server.close();
    shm.unlink();
}

void test_one_client_subscribes_to_two_topics() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    const auto pid = std::to_string(::getpid());
    const std::string shm_a_name = "/everest_shm_control_multi_topic_a_" + pid;
    const std::string shm_b_name = "/everest_shm_control_multi_topic_b_" + pid;
    const std::string server_name = "/tmp/everest-shm-control-multi-topic-" + pid;

    shared_memory shm_a(shm_a_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shared_memory shm_b(shm_b_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    ring_buffer rb_a(shm_a.get_ptr());
    ring_buffer rb_b(shm_b.get_ptr());
    coordinator::initialize_ring_buffer(rb_a, slots, slot_size, 0);
    coordinator::initialize_ring_buffer(rb_b, slots, slot_size, 0);
    coordinator manager_a(rb_a);
    coordinator manager_b(rb_b);

    server control_server;
    require(control_server.open(server_name, false), "multi-topic control server should open");
    control_server.register_topic(multi_topic_a_name,
                                  server::topic_endpoint{shm_a_name, 0, slots, slot_size, &manager_a});
    control_server.register_topic(multi_topic_b_name,
                                  server::topic_endpoint{shm_b_name, 0, slots, slot_size, &manager_b});

    auto subscriber_ready = make_pipe("multi-topic subscriber ready pipe should open");
    auto subscriber_result = make_pipe("multi-topic subscriber result pipe should open");
    auto subscriber_release = make_pipe("multi-topic subscriber release pipe should open");

    const auto subscriber_pid = fork_child("multi-topic subscriber fork should succeed");
    if (subscriber_pid == 0) {
        run_multi_topic_subscriber_child(server_name, shm_a_name, shm_b_name, std::move(subscriber_ready),
                                         std::move(subscriber_result), std::move(subscriber_release));
    }
    subscriber_ready.write.close();
    subscriber_result.write.close();
    subscriber_release.read.close();

    const auto subscriber_a_handshake =
        handle_next_with_timeout(control_server, "topic A subscriber handshake should arrive");
    require(subscriber_a_handshake->accepted, "topic A subscriber handshake should be accepted");
    require(subscriber_a_handshake->topic == multi_topic_a_name, "topic A handshake should name topic A");
    require(subscriber_a_handshake->mapping->shm_name == shm_a_name, "topic A handshake should map ring A");
    const auto subscriber_b_handshake =
        handle_next_with_timeout(control_server, "topic B subscriber handshake should arrive");
    require(subscriber_b_handshake->accepted, "topic B subscriber handshake should be accepted");
    require(subscriber_b_handshake->topic == multi_topic_b_name, "topic B handshake should name topic B");
    require(subscriber_b_handshake->mapping->shm_name == shm_b_name, "topic B handshake should map ring B");

    require(rb_a.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "topic A handshake should add one active subscriber");
    require(rb_b.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "topic B handshake should add one active subscriber");

    const auto topic_a_subscribers = control_server.subscribers_for_topic(multi_topic_a_name);
    const auto topic_b_subscribers = control_server.subscribers_for_topic(multi_topic_b_name);
    require(topic_a_subscribers.size() == 1U, "topic A should have one visible subscriber");
    require(topic_b_subscribers.size() == 1U, "topic B should have one visible subscriber");
    require(topic_a_subscribers[0].client_id == "module-multi-sub", "topic A snapshot should keep shared client id");
    require(topic_b_subscribers[0].client_id == "module-multi-sub", "topic B snapshot should keep shared client id");
    require(topic_a_subscribers[0].subscriber == 0U, "topic A subscriber should use topic A subscriber id zero");
    require(topic_b_subscribers[0].subscriber == 0U, "topic B subscriber should use topic B subscriber id zero");
    require(topic_a_subscribers[0].topic_coordinator == &manager_a,
            "topic A snapshot should reference topic A coordinator");
    require(topic_b_subscribers[0].topic_coordinator == &manager_b,
            "topic B snapshot should reference topic B coordinator");
    require(topic_a_subscribers[0].connection_fd == topic_b_subscribers[0].connection_fd,
            "T-4.009: both topics for the same client should share one server-side connection FD");
    require(topic_a_subscribers[0].session_id == topic_b_subscribers[0].session_id,
            "T-4.009: both topics for the same client should share one session id");

    const auto all_snapshots = control_server.subscriber_snapshots();
    require(all_snapshots.at(multi_topic_a_name).size() == 1U, "all snapshots should include topic A entry");
    require(all_snapshots.at(multi_topic_b_name).size() == 1U, "all snapshots should include topic B entry");
    require(all_snapshots.at(multi_topic_a_name)[0].client_id == "module-multi-sub",
            "all snapshots topic A entry should keep shared client id");
    require(all_snapshots.at(multi_topic_b_name)[0].client_id == "module-multi-sub",
            "all snapshots topic B entry should keep shared client id");
    require(read_byte(subscriber_ready.read, "multi-topic subscriber should report ready") == 'r',
            "multi-topic subscriber ready byte");

    auto publisher_a = topic::make_publisher(ring_buffer(shm_a.get_ptr()), manager_a.make_publication_fd(),
                                             manager_a.make_release_fd());
    auto publisher_b = topic::make_publisher(ring_buffer(shm_b.get_ptr()), manager_b.make_publication_fd(),
                                             manager_b.make_release_fd());

    require(publisher_a->publish(topic_a_payload, topic::full_buffer_policy::fail_immediately),
            "topic A publish should succeed");
    require(wait_readable(manager_a.publication_event_fd()->get_raw_fd(), timeout_ms),
            "manager A should receive publication event");
    require(manager_a.handle_publication() == 1, "manager A should dispatch topic A publication");
    require(control_server.wake_subscriber_clients(multi_topic_a_name,
                                                   {subscriber_a_handshake->subscriber_id.value()}) == 1U,
            "control server should wake multi-topic client for topic A publication");
    const auto topic_a_report =
        read_multi_topic_report(subscriber_result.read, "subscriber should report topic A callback");
    require(topic_a_report.topic_id == 'a', "first callback report should be for topic A");
    require(topic_a_report.payload == topic_a_payload, "topic A callback should receive topic A payload");
    require(topic_a_report.topic_a_count == 1, "topic A callback count should be one after topic A dispatch");
    require(topic_a_report.topic_b_count == 0, "topic B callback count should stay zero after topic A dispatch");
    require(wait_readable(manager_a.topic_ack_event_fd()->get_raw_fd(), timeout_ms),
            "manager A should receive ACK on topic A");
    require(!wait_readable(manager_b.topic_ack_event_fd()->get_raw_fd(), 0),
            "manager B should not receive ACK for topic A dispatch");
    require(manager_a.handle_topic_ack().released, "manager A should handle topic A ACK through shared topic fd");

    require(publisher_b->publish(topic_b_payload, topic::full_buffer_policy::fail_immediately),
            "topic B publish should succeed");
    require(wait_readable(manager_b.publication_event_fd()->get_raw_fd(), timeout_ms),
            "manager B should receive publication event");
    require(manager_b.handle_publication() == 1, "manager B should dispatch topic B publication");
    require(control_server.wake_subscriber_clients(multi_topic_b_name,
                                                   {subscriber_b_handshake->subscriber_id.value()}) == 1U,
            "control server should wake multi-topic client for topic B publication");
    const auto topic_b_report =
        read_multi_topic_report(subscriber_result.read, "subscriber should report topic B callback");
    require(topic_b_report.topic_id == 'b', "second callback report should be for topic B");
    require(topic_b_report.payload == topic_b_payload, "topic B callback should receive topic B payload");
    require(topic_b_report.topic_a_count == 1, "topic A callback count should stay one after topic B dispatch");
    require(topic_b_report.topic_b_count == 1, "topic B callback count should be one after topic B dispatch");
    require(wait_readable(manager_b.topic_ack_event_fd()->get_raw_fd(), timeout_ms),
            "manager B should receive ACK on topic B");
    require(!wait_readable(manager_a.topic_ack_event_fd()->get_raw_fd(), 0),
            "manager A should not receive another ACK for topic B dispatch");
    require(manager_b.handle_topic_ack().released, "manager B should handle topic B ACK through shared topic fd");

    subscriber_release.write.close();
    wait_for_child(subscriber_pid, "multi-topic subscriber child should exit cleanly");
    // Closing one session must release every registration it owned, regardless of topic.
    require(cleanup_until_removed(control_server, 1) >= 1,
            "closing the multi-topic session should remove at least one server-side connection FD");
    require(control_server.subscribers_for_topic(multi_topic_a_name).empty(),
            "session cleanup should remove topic A visible subscriber");
    require(control_server.subscribers_for_topic(multi_topic_b_name).empty(),
            "session cleanup should remove topic B visible subscriber");
    require(rb_a.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "session cleanup should remove topic A active subscriber target");
    require(rb_b.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "session cleanup should remove topic B active subscriber target");

    control_server.close();
    shm_a.unlink();
    shm_b.unlink();
}

} // namespace

int main() {
    test_single_topic_subscriber_and_publisher();
    test_one_client_subscribes_to_two_topics();
    return 0;
}
