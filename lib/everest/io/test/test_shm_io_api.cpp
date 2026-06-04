// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <future>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_client.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/shm_client.hpp>
#include <everest/io/shm/shm_server.hpp>
#include <everest/io/shm/structures.hpp>
#include <everest/io/shm/topic.hpp>

using namespace everest::lib::io;

namespace {

constexpr auto timeout_ms = 3000;
constexpr auto cross_process_timeout_ms = 5000;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void require_io_ok(const shm::io_result& result, const char* message) {
    if (result.status != shm::io_status::ok) {
        std::cerr << "FAILED: " << message << ": " << shm::to_string(result.status) << " " << result.message << "\n";
        std::exit(1);
    }
}

bool uds_bind_unavailable(const shm::io_result& result) {
    return result.status == shm::io_status::resource_error &&
           result.message.find("Operation not permitted") != std::string::npos &&
           result.message.find("bind UDS server") != std::string::npos;
}

std::uint64_t test_segment_size(const std::vector<shm::topic_definition>& topics,
                                std::uint32_t registry_capacity = 0U) {
    if (registry_capacity == 0U) {
        registry_capacity = static_cast<std::uint32_t>(topics.size());
    }
    const auto registry_offset = static_cast<std::uint64_t>(sizeof(shm::SegmentHeader));
    auto next_offset =
        shm::align_shm_topic_offset(registry_offset + (registry_capacity * shm::shm_segment_registry_entry_size));
    for (const auto& topic : topics) {
        next_offset = shm::align_shm_topic_offset(
            next_offset + shm::shm_ring_buffer_required_size_u64(topic.total_slots, topic.slot_size));
    }
    return next_offset;
}

shm::server_options make_server_options(std::string suffix) {
    shm::server_options options;
    options.shm_name = "/everest-shm-api-test-" + suffix;
    options.control_socket_name = "/tmp/everest-shm-control-sub-" + std::to_string(::getpid()) + "-" + suffix;
    options.control_socket_abstract_namespace = false;
    options.topics.push_back({"telemetry/session", 4, 256});
    options.topics.push_back({"telemetry/summary", 2, 128});
    options.unlink_shm_on_close = true;
    return options;
}

shm::server_options make_small_ring_server_options(std::string suffix) {
    auto options = make_server_options(std::move(suffix));
    options.topics.clear();
    options.topics.push_back({"telemetry/session", 1, 256});
    options.topics.push_back({"telemetry/summary", 1, 128});
    return options;
}

shm::client_options make_client_options(const shm::server_options& server_options, std::string client_id) {
    shm::client_options options;
    options.client_id = std::move(client_id);
    options.control.server_name = server_options.control_socket_name;
    options.control.server_abstract_namespace = server_options.control_socket_abstract_namespace;
    return options;
}

bool poll_once(event::fd_event_handler& handler) {
    const auto ready = handler.poll(std::chrono::milliseconds(10));
    handler.run_actions();
    return ready;
}

void poll_until(event::fd_event_handler& handler, const std::function<bool()>& done, const char* context) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return;
        }
        poll_once(handler);
    }
    require(done(), context);
}

template <typename Future>
void drive_until_ready(event::fd_event_handler& handler, Future& future, const char* context) {
    poll_until(
        handler, [&future]() { return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; },
        context);
}

template <typename Future> void drive_until_ready(shm::shm_server& server, Future& future, const char* context) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return;
        }
        server.sync();
    }
    require(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, context);
}

void spin_until(const std::function<void()>& tick, const std::function<bool()>& done, const char* context) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return;
        }
        tick();
    }
    require(done(), context);
}

void drive_server_and_client_until(shm::shm_server& server, shm::shm_client& client, const std::function<bool()>& done,
                                   const char* context) {
    spin_until(
        [&]() {
            server.sync();
            client.sync();
        },
        done, context);
}

void drive_server_until(shm::shm_server& server, const std::function<bool()>& done, const char* context) {
    spin_until([&]() { server.sync(); }, done, context);
}

void drive_client_until(shm::shm_client& client, const std::function<bool()>& done, const char* context) {
    spin_until([&]() { client.sync(); }, done, context);
}

shm::io_result subscribe_with_server(shm::shm_server& server, shm::shm_client& client, std::string topic,
                                     shm::shm_client::message_callback callback, shm::subscribe_options options = {}) {
    auto future =
        std::async(std::launch::async, [&]() { return client.subscribe(topic, std::move(callback), options); });
    drive_until_ready(server, future, "client subscribe handshake should complete");
    return future.get();
}

shm::io_result subscribe_with_server(shm::shm_server& server, shm::shm_client& client, std::vector<std::string> topics,
                                     shm::shm_client::message_callback callback, shm::subscribe_options options = {}) {
    auto future =
        std::async(std::launch::async, [&]() { return client.subscribe(topics, std::move(callback), options); });
    drive_until_ready(server, future, "client multi-topic subscribe handshake should complete");
    return future.get();
}

shm::io_result publish_with_server(shm::shm_server& server, shm::shm_client& client, std::string topic,
                                   std::string payload, shm::publish_options options = {}) {
    auto future = std::async(std::launch::async, [&]() { return client.publish(topic, payload, options); });
    drive_until_ready(server, future, "client publish handshake should complete");
    return future.get();
}

shm::io_result unsubscribe_with_server(shm::shm_server& server, shm::shm_client& client, std::string topic) {
    auto future = std::async(std::launch::async, [&]() { return client.unsubscribe(topic); });
    drive_until_ready(server, future, "client unsubscribe request should complete");
    return future.get();
}

// T-4.009: drives the server while the caller-supplied session sends one handshake. The
// session is owned by the caller so the resulting subscriber/publisher registrations
// outlive the function — request_handshake() would close its session at return and tear
// the registration down immediately.
shm::control::client_handshake_result handshake_through_session(shm::shm_server& server, shm::control::session& sess,
                                                                const std::string& client_id, const std::string& topic,
                                                                shm::control::role role) {
    event::fd_event_handler handler;
    require(server.register_events(handler), "server should register external events");
    auto future = std::async(std::launch::async,
                             [&sess, client_id, topic, role]() { return sess.handshake(client_id, topic, role); });
    drive_until_ready(handler, future, "control handshake should complete through shm_server event registration");
    require(server.unregister_events(handler), "server should unregister external events");
    return future.get();
}

bool fd_readable(int fd) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
}

struct pipe_pair {
    event::unique_fd read;
    event::unique_fd write;
};

pipe_pair make_pipe(const char* context) {
    int fds[2]{};
    require(::pipe(fds) == 0, context);
    return pipe_pair{event::unique_fd(fds[0]), event::unique_fd(fds[1])};
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

bool write_byte(int fd, char value) {
    return write_all(fd, &value, sizeof(value));
}

bool read_byte_nonblocking(int fd, char& value) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    const auto ready = ::poll(&descriptor, 1, 0);
    if (ready <= 0 || (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return false;
    }
    return read_all(fd, &value, sizeof(value));
}

[[noreturn]] void child_fail(const char* message, int code) {
    std::cerr << "FAILED child: " << message << "\n";
    std::_Exit(code);
}

void child_require(bool condition, const char* message, int code) {
    if (!condition) {
        child_fail(message, code);
    }
}

void child_require_io_ok(const shm::io_result& result, const char* message, int code) {
    if (result.status != shm::io_status::ok) {
        std::cerr << "FAILED child: " << message << ": " << shm::to_string(result.status) << " " << result.message
                  << "\n";
        std::_Exit(code);
    }
}

shm::server_options make_cross_process_server_options(std::string suffix) {
    shm::server_options options;
    options.shm_name = "/everest-shm-api-cross-" + std::to_string(::getpid()) + "-" + suffix;
    options.control_socket_name = "/tmp/everest-shm-api-cross-" + std::to_string(::getpid()) + "-" + suffix;
    options.control_socket_abstract_namespace = false;
    options.topics.push_back({"client_a/state", 4, 256});
    options.topics.push_back({"client_a/telemetry", 4, 256});
    options.topics.push_back({"client_b/state", 4, 256});
    options.topics.push_back({"client_b/telemetry", 4, 256});
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    return options;
}

struct cross_process_client_config {
    const char* client_id;
    const char* subscribe_state;
    const char* subscribe_telemetry;
    const char* publish_state;
    const char* publish_telemetry;
    const char* expected_state_payload;
    const char* expected_telemetry_payload;
    const char* state_payload;
    const char* telemetry_payload;
};

void run_cross_process_api_client(shm::server_options server_options, cross_process_client_config config,
                                  pipe_pair ready_pipe, pipe_pair start_pipe) {
    ::alarm(10);
    ready_pipe.read.close();
    start_pipe.write.close();

    shm::shm_client client(make_client_options(server_options, config.client_id));
    child_require_io_ok(client.connect(), "client should connect", 2);

    bool received_state = false;
    bool received_telemetry = false;
    const std::vector<std::string> subscribe_topics{config.subscribe_state, config.subscribe_telemetry};
    child_require_io_ok(
        client.subscribe(subscribe_topics,
                         [&](std::string_view topic, std::string_view payload) {
                             if (topic == config.subscribe_state && payload == config.expected_state_payload) {
                                 received_state = true;
                             }
                             if (topic == config.subscribe_telemetry && payload == config.expected_telemetry_payload) {
                                 received_telemetry = true;
                             }
                         }),
        "client should subscribe to peer topics", 3);

    child_require(write_byte(ready_pipe.write, 'r'), "client should report subscription readiness", 4);
    ready_pipe.write.close();

    char command{};
    child_require(read_all(start_pipe.read, &command, sizeof(command)) && command == 's',
                  "client should receive publish command", 5);
    start_pipe.read.close();

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    child_require_io_ok(client.publish(config.publish_state, config.state_payload, publish_options),
                        "client should publish state", 6);
    child_require_io_ok(client.publish(config.publish_telemetry, config.telemetry_payload, publish_options),
                        "client should publish telemetry", 7);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cross_process_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && (!received_state || !received_telemetry)) {
        (void)client.sync();
    }
    child_require(received_state, "client should receive peer state", 8);
    child_require(received_telemetry, "client should receive peer telemetry", 9);
    child_require_io_ok(client.disconnect(), "client should disconnect cleanly", 10);
    std::_Exit(0);
}

pid_t fork_cross_process_client(const shm::server_options& server_options, const cross_process_client_config& config,
                                pipe_pair& ready_pipe, pipe_pair& start_pipe) {
    const auto child = ::fork();
    require(child >= 0, "fork should create SHM API client child");
    if (child == 0) {
        run_cross_process_api_client(server_options, config, std::move(ready_pipe), std::move(start_pipe));
    }
    ready_pipe.write.close();
    start_pipe.read.close();
    return child;
}

void run_cross_process_liveness_server(shm::server_options server_options, pipe_pair ready_pipe) {
    ::alarm(10);
    ready_pipe.read.close();
    shm::shm_server server(std::move(server_options));
    child_require_io_ok(server.open(), "liveness server should open", 2);
    child_require(write_byte(ready_pipe.write, 'r'), "liveness server should report readiness", 3);
    ready_pipe.write.close();
    while (true) {
        (void)server.sync();
    }
}

pid_t fork_cross_process_liveness_server(const shm::server_options& server_options, pipe_pair& ready_pipe) {
    const auto child = ::fork();
    require(child >= 0, "fork should create SHM liveness server child");
    if (child == 0) {
        run_cross_process_liveness_server(server_options, std::move(ready_pipe));
    }
    ready_pipe.write.close();
    return child;
}

void wait_single_ready(int fd, const char* context) {
    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cross_process_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && !ready) {
        char value{};
        if (read_byte_nonblocking(fd, value)) {
            ready = value == 'r';
        }
    }
    require(ready, context);
}

void wait_child_success_while_driving_server(pid_t child_a, pid_t child_b, shm::shm_server& server) {
    bool child_a_done = false;
    bool child_b_done = false;
    int status_a = 0;
    int status_b = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cross_process_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && (!child_a_done || !child_b_done)) {
        (void)server.sync();
        if (!child_a_done) {
            const auto result = ::waitpid(child_a, &status_a, WNOHANG);
            require(result >= 0 || errno == EINTR, "waitpid should not fail for client A");
            child_a_done = result == child_a;
        }
        if (!child_b_done) {
            const auto result = ::waitpid(child_b, &status_b, WNOHANG);
            require(result >= 0 || errno == EINTR, "waitpid should not fail for client B");
            child_b_done = result == child_b;
        }
    }

    if (!child_a_done) {
        ::kill(child_a, SIGKILL);
        (void)::waitpid(child_a, &status_a, 0);
    }
    if (!child_b_done) {
        ::kill(child_b, SIGKILL);
        (void)::waitpid(child_b, &status_b, 0);
    }

    require(child_a_done, "client A should exit before timeout");
    require(child_b_done, "client B should exit before timeout");
    require(WIFEXITED(status_a) && WEXITSTATUS(status_a) == 0, "client A should exit successfully");
    require(WIFEXITED(status_b) && WEXITSTATUS(status_b) == 0, "client B should exit successfully");
}

void wait_ready_while_driving_server(int fd_a, int fd_b, shm::shm_server& server) {
    bool ready_a = false;
    bool ready_b = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cross_process_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && (!ready_a || !ready_b)) {
        (void)server.sync();
        char value{};
        if (!ready_a && read_byte_nonblocking(fd_a, value)) {
            ready_a = value == 'r';
        }
        if (!ready_b && read_byte_nonblocking(fd_b, value)) {
            ready_b = value == 'r';
        }
    }
    require(ready_a, "client A should subscribe before timeout");
    require(ready_b, "client B should subscribe before timeout");
}

void wait_cross_process_snapshots_empty(shm::shm_server& server) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cross_process_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)server.sync();
        if (server.subscriber_snapshots().empty()) {
            return;
        }
    }
    require(server.subscriber_snapshots().empty(),
            "server should clean snapshots after cross-process client disconnect");
}

void expect_status_and_error_callback(shm::server_options options, shm::io_status expected_status,
                                      const char* context) {
    shm::shm_server server(std::move(options));
    bool callback_called = false;
    server.set_error_handler([&](shm::io_status status, std::string_view message) {
        callback_called = status == expected_status && !message.empty();
    });
    const auto result = server.open();
    require(result.status == expected_status, context);
    require(callback_called, "server error callback should report failed open");
}

void test_invalid_server_options() {
    auto options = make_server_options("invalid-missing-shm");
    options.shm_name.clear();
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                     "missing shm_name should fail");

    options = make_server_options("invalid-missing-control");
    options.control_socket_name.clear();
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                     "missing control_socket_name should fail");

    options = make_server_options("invalid-empty-topics");
    options.topics.clear();
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                     "empty topic list should fail");

    options = make_server_options("invalid-duplicate-topic");
    options.topics.push_back(options.topics.front());
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                     "duplicate topics should fail");

    options = make_server_options("invalid-zero-slots");
    options.topics.front().total_slots = 0;
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options, "zero slots should fail");

    options = make_server_options("invalid-zero-size");
    options.topics.front().slot_size = 0;
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options, "zero slot size should fail");

    options = make_server_options("invalid-registry-capacity");
    options.topic_registry_capacity = 1;
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                     "registry capacity smaller than topic count should fail");

    options = make_server_options("invalid-segment-size");
    options.segment_size = 1;
    expect_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                     "segment size smaller than topic layout should fail");
}

void test_open_initializes_two_topic_segment() {
    auto options = make_server_options("segment");
    const auto segment_size = test_segment_size(options.topics);
    shm::shm_server server(options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "server open should succeed");
    require(server.is_open(), "server should report open");

    shm::shared_memory mapped(options.shm_name, static_cast<std::size_t>(segment_size), false);
    auto* header = static_cast<shm::SegmentHeader*>(mapped.get_ptr());
    require(shm::validate_segment_header(mapped.get_ptr(), mapped.get_size()) ==
                shm::SegmentHeaderValidationStatus::valid,
            "server should initialize a valid segment header");
    require(header->registry_entry_count == 2U, "server should initialize two registry entries");

    const auto* entry_a = shm::topic_registry_entry_at(mapped.get_ptr(), *header, 0U);
    const auto* entry_b = shm::topic_registry_entry_at(mapped.get_ptr(), *header, 1U);
    require(shm::validate_topic_registry_entry(mapped.get_ptr(), mapped.get_size(), header, entry_a) ==
                shm::TopicRegistryValidationStatus::valid,
            "topic A registry entry should be valid");
    require(shm::validate_topic_registry_entry(mapped.get_ptr(), mapped.get_size(), header, entry_b) ==
                shm::TopicRegistryValidationStatus::valid,
            "topic B registry entry should be valid");
    require(entry_a->ring_offset < entry_b->ring_offset, "topic rings should be laid out deterministically");

    const auto* bytes = static_cast<const std::uint8_t*>(mapped.get_ptr());
    auto rb_a = shm::ring_buffer(const_cast<std::uint8_t*>(bytes + entry_a->ring_offset));
    auto rb_b = shm::ring_buffer(const_cast<std::uint8_t*>(bytes + entry_b->ring_offset));
    require(rb_a.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0U,
            "topic A ring should start with zero subscribers");
    require(rb_b.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0U,
            "topic B ring should start with zero subscribers");

    require_io_ok(server.close(), "server close should succeed");
    require(!server.is_open(), "server should report closed");
}

void test_server_options_defaults_and_explicit_segment_layout() {
    auto options = make_server_options("explicit-layout");
    options.topic_registry_capacity = 8;
    options.default_ring_slots = 3;
    options.default_slot_size = 192;
    options.topics.clear();
    options.topics.push_back({"telemetry/defaults"});
    options.topics.push_back({"telemetry/explicit", 2, 128});
    options.segment_size = test_segment_size({{"telemetry/defaults", 3, 192}, {"telemetry/explicit", 2, 128}},
                                             options.topic_registry_capacity);

    shm::shm_server server(options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "server open with explicit layout should succeed");

    shm::shared_memory mapped(options.shm_name, static_cast<std::size_t>(options.segment_size), false);
    auto* header = static_cast<shm::SegmentHeader*>(mapped.get_ptr());
    require(header->segment_size == options.segment_size, "header should use explicit segment size");
    require(header->registry_entry_capacity == options.topic_registry_capacity,
            "header should use explicit registry capacity");
    require(header->registry_entry_count == 2U, "header should keep active topic count");

    const auto* default_entry = shm::topic_registry_entry_at(mapped.get_ptr(), *header, 0U);
    require(default_entry->total_slots == options.default_ring_slots, "default slots should be applied");
    require(default_entry->slot_size == options.default_slot_size, "default slot size should be applied");

    const auto& effective_options = server.options();
    require(effective_options.topics.at(0).total_slots == options.default_ring_slots,
            "server options should report effective default slots");
    require(effective_options.topics.at(0).slot_size == options.default_slot_size,
            "server options should report effective default slot size");
    require(effective_options.topics.at(1).total_slots == 2U, "server options should keep explicit slots");
    require(effective_options.topics.at(1).slot_size == 128U, "server options should keep explicit slot size");

    require_io_ok(server.close(), "server close should succeed");
}

void test_server_cleanup_after_partial_open_failure() {
    auto options = make_server_options("partial-open-failure");
    options.control_socket_name = "/tmp/everest-shm-missing-dir-" + std::to_string(::getpid()) + "/control";

    shm::shm_server server(options);
    const auto open_result = server.open();
    require(open_result.status == shm::io_status::resource_error,
            "server open should fail when the control socket cannot be created");
    require(!server.is_open(), "server should not report open after partial failure");
    require(server.close().status == shm::io_status::ok, "close after failed open should be idempotent");
    require(server.close().status == shm::io_status::ok, "second close after failed open should be idempotent");

    try {
        shm::shared_memory mapped(options.shm_name, 1, false);
        (void)mapped;
        require(false, "failed server startup should unlink the SHM segment");
    } catch (const std::exception&) {
    }
}

void test_control_handshakes_snapshots_and_close_cleanup() {
    auto options = make_server_options("control");
    const auto shm_name = options.shm_name;
    shm::shm_server server(options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "control server wrapper should open");
    require(server.open().status == shm::io_status::already_open, "second open should fail predictably");

    shm::control::client_options client_options;
    client_options.server_name = options.control_socket_name;
    client_options.server_abstract_namespace = options.control_socket_abstract_namespace;

    shm::control::session subscriber_session(client_options);
    require(subscriber_session.is_open(), "subscriber session should open");
    auto subscriber = handshake_through_session(server, subscriber_session, "sub-client", "telemetry/session",
                                                shm::control::role::subscriber);
    require(subscriber.is_accepted(), "subscriber handshake should be accepted");
    require(subscriber.accepted->mapping.shm_name == shm_name, "subscriber should receive server SHM mapping");

    shm::control::session publisher_session(client_options);
    require(publisher_session.is_open(), "publisher session should open");
    auto publisher = handshake_through_session(server, publisher_session, "pub-client", "telemetry/session",
                                               shm::control::role::publisher);
    require(publisher.is_accepted(), "publisher handshake should be accepted");
    require(publisher.accepted->fds.publication.is_fd(), "publisher should receive publication fd");

    const auto snapshots = server.subscriber_snapshots();
    require(snapshots.size() == 1U, "server snapshots should expose one subscriber");
    require(snapshots.front().topic == "telemetry/session", "snapshot topic should match");
    require(snapshots.front().client_id == "sub-client", "snapshot client id should match");
    require(server.subscriber_snapshots("telemetry/session").size() == 1U, "topic snapshot lookup should match");
    require(server.subscriber_snapshots("telemetry/summary").empty(), "empty topic snapshot lookup should be empty");

    event::fd_event_handler liveness_handler;
    require(server.register_events(liveness_handler), "server should register liveness events");
    // T-4.009: closing the persistent control session itself is the liveness signal; the
    // server tears down every registration owned by that session in one sweep.
    subscriber_session.close();
    poll_until(
        liveness_handler, [&]() { return server.subscriber_snapshots("telemetry/session").empty(); },
        "server liveness event should clean up idle disconnected subscriber");
    require(server.unregister_events(liveness_handler), "server should unregister liveness events");

    require_io_ok(server.close(), "server close should succeed");
    require_io_ok(server.close(), "server close should be idempotent");
    require(server.subscriber_snapshots().empty(), "snapshots should clear after close");

    try {
        shm::shared_memory should_not_exist(shm_name, 4096, false);
        (void)should_not_exist;
        require(false, "server close should unlink owned SHM");
    } catch (const std::exception&) {
    }

    auto rejected_after_close = shm::control::request_handshake(client_options, "late-client", "telemetry/session",
                                                                shm::control::role::publisher);
    require(rejected_after_close.is_error(), "closed server control socket should reject new clients");
}

void test_event_loop_dispatches_publication_and_ack() {
    auto options = make_server_options("events");
    const auto segment_size = test_segment_size(options.topics);
    shm::shm_server server(options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "event server should open");

    event::fd_event_handler handler;
    require(server.register_events(handler), "event server should register fd_event_handler events");

    shm::control::client_options client_options;
    client_options.server_name = options.control_socket_name;
    client_options.server_abstract_namespace = options.control_socket_abstract_namespace;

    shm::control::session subscriber_session(client_options);
    require(subscriber_session.is_open(), "event subscriber session should open");
    auto subscriber_future = std::async(std::launch::async, [&]() {
        return subscriber_session.handshake("event-sub", "telemetry/session", shm::control::role::subscriber);
    });
    drive_until_ready(handler, subscriber_future, "subscriber handshake should complete");
    auto subscriber_result = subscriber_future.get();
    require(subscriber_result.is_accepted(), "event subscriber handshake should be accepted");

    shm::control::session publisher_session(client_options);
    require(publisher_session.is_open(), "event publisher session should open");
    auto publisher_future = std::async(std::launch::async, [&]() {
        return publisher_session.handshake("event-pub", "telemetry/session", shm::control::role::publisher);
    });
    drive_until_ready(handler, publisher_future, "publisher handshake should complete");
    auto publisher_result = publisher_future.get();
    require(publisher_result.is_accepted(), "event publisher handshake should be accepted");

    shm::shared_memory mapped(options.shm_name, static_cast<std::size_t>(segment_size), false);
    auto* base = static_cast<std::uint8_t*>(mapped.get_ptr());
    auto subscriber_rb = shm::ring_buffer(base + subscriber_result.accepted->mapping.ring_offset);
    auto publisher_rb = shm::ring_buffer(base + publisher_result.accepted->mapping.ring_offset);

    std::string received_payload;
    auto subscriber = shm::topic::make_subscriber(
        subscriber_rb, std::move(subscriber_result.accepted->fds.broadcast),
        std::move(subscriber_result.accepted->fds.ack), [&](const std::string& payload) { received_payload = payload; },
        shm::topic::subscriber_cursor{subscriber_result.accepted->cursor->write_idx,
                                      subscriber_result.accepted->cursor->sequence},
        subscriber_result.accepted->subscriber_id.value_or(0U));
    auto publisher = shm::topic::make_publisher(publisher_rb, std::move(publisher_result.accepted->fds.publication),
                                                std::move(publisher_result.accepted->fds.release));
    require(subscriber->register_events(handler), "subscriber topic should register broadcast event");

    require(publisher->publish("{\"event\":1}", shm::topic::full_buffer_policy::fail_immediately),
            "publisher should publish test payload");
    poll_until(
        handler, [&]() { return !received_payload.empty(); }, "server publication event should dispatch to subscriber");
    poll_once(handler);
    require(!fd_readable(publisher->get_event_fd()->get_raw_fd()),
            "ACK for a non-current write slot should not wake publisher release fd");

    require(received_payload == "{\"event\":1}", "subscriber should receive published payload");
    require(server.counter_snapshot().subscriber_acks_observed == 1U, "server should account subscriber ACK");
    require(server.counter_snapshot().slots_released == 1U, "server should account released slot");

    require(subscriber->unregister_events(handler), "subscriber topic should unregister broadcast event");
    require(server.unregister_events(handler), "server should unregister fd_event_handler events");
    require_io_ok(server.close(), "event server should close");
}

void test_event_loop_ack_unblocks_current_write_slot_publisher() {
    auto options = make_small_ring_server_options("events-current-release");
    const auto segment_size = test_segment_size(options.topics);
    shm::shm_server server(options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "single-slot event server should open");

    event::fd_event_handler handler;
    require(server.register_events(handler), "single-slot event server should register fd_event_handler events");

    shm::control::client_options client_options;
    client_options.server_name = options.control_socket_name;
    client_options.server_abstract_namespace = options.control_socket_abstract_namespace;

    shm::control::session subscriber_session(client_options);
    require(subscriber_session.is_open(), "single-slot subscriber session should open");
    auto subscriber_future = std::async(std::launch::async, [&]() {
        return subscriber_session.handshake("event-current-sub", "telemetry/session", shm::control::role::subscriber);
    });
    drive_until_ready(handler, subscriber_future, "single-slot subscriber handshake should complete");
    auto subscriber_result = subscriber_future.get();
    require(subscriber_result.is_accepted(), "single-slot subscriber handshake should be accepted");

    shm::control::session publisher_session(client_options);
    require(publisher_session.is_open(), "single-slot publisher session should open");
    auto publisher_future = std::async(std::launch::async, [&]() {
        return publisher_session.handshake("event-current-pub", "telemetry/session", shm::control::role::publisher);
    });
    drive_until_ready(handler, publisher_future, "single-slot publisher handshake should complete");
    auto publisher_result = publisher_future.get();
    require(publisher_result.is_accepted(), "single-slot publisher handshake should be accepted");

    shm::shared_memory mapped(options.shm_name, static_cast<std::size_t>(segment_size), false);
    auto* base = static_cast<std::uint8_t*>(mapped.get_ptr());
    auto subscriber_rb = shm::ring_buffer(base + subscriber_result.accepted->mapping.ring_offset);
    auto publisher_rb = shm::ring_buffer(base + publisher_result.accepted->mapping.ring_offset);

    std::string received_payload;
    auto subscriber = shm::topic::make_subscriber(
        subscriber_rb, std::move(subscriber_result.accepted->fds.broadcast),
        std::move(subscriber_result.accepted->fds.ack), [&](const std::string& payload) { received_payload = payload; },
        shm::topic::subscriber_cursor{subscriber_result.accepted->cursor->write_idx,
                                      subscriber_result.accepted->cursor->sequence},
        subscriber_result.accepted->subscriber_id.value_or(0U));
    auto publisher = shm::topic::make_publisher(publisher_rb, std::move(publisher_result.accepted->fds.publication),
                                                std::move(publisher_result.accepted->fds.release));
    require(subscriber->register_events(handler), "single-slot subscriber topic should register broadcast event");

    require(publisher->publish("first", shm::topic::full_buffer_policy::fail_immediately),
            "first single-slot publish should fill current write slot");
    poll_until(
        handler, [&]() { return received_payload == "first"; }, "single-slot subscriber should receive first payload");

    auto blocked_publish = std::async(std::launch::async, [&]() {
        return publisher->publish("second", shm::topic::full_buffer_policy::block_publisher);
    });
    spin_until([]() {}, [&]() { return publisher->counter_snapshot().blocked_publish_attempts == 1U; },
               "second single-slot publish should block on current write slot");
    require(blocked_publish.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
            "blocked single-slot publish should wait for release");

    drive_until_ready(handler, blocked_publish, "ACK for current write slot should unblock publisher");
    require(blocked_publish.get(), "second single-slot publish should succeed after release");
    require(server.counter_snapshot().subscriber_acks_observed == 1U, "single-slot server should account ACK");
    require(server.counter_snapshot().slots_released == 1U, "single-slot server should account released slot");

    require(subscriber->unregister_events(handler), "single-slot subscriber topic should unregister broadcast event");
    require(server.unregister_events(handler), "single-slot server should unregister fd_event_handler events");
    require_io_ok(server.close(), "single-slot event server should close");
}

void expect_client_status_and_error_callback(shm::client_options options, shm::io_status expected_status,
                                             const char* context) {
    shm::shm_client client(std::move(options));
    bool callback_called = false;
    client.set_error_handler([&](shm::io_status status, std::string_view message) {
        callback_called = status == expected_status && !message.empty();
    });
    const auto result = client.connect();
    require(result.status == expected_status, context);
    require(callback_called, "client error callback should report failed connect");
}

void test_invalid_client_options() {
    auto options = make_client_options(make_server_options("invalid-client"), "client");
    options.client_id.clear();
    expect_client_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                            "missing client_id should fail");

    options = make_client_options(make_server_options("invalid-client-control"), "client");
    options.control.server_name.clear();
    expect_client_status_and_error_callback(std::move(options), shm::io_status::invalid_options,
                                            "missing control server name should fail");
}

void test_client_connect_disconnect_state() {
    auto options = make_client_options(make_server_options("client-state"), "state-client");
    shm::shm_client client(std::move(options));
    require(!client.is_connected(), "client should start disconnected");
    require(client.publish("telemetry/session", "payload").status == shm::io_status::not_open,
            "publish before connect should fail predictably");
    require_io_ok(client.connect(), "client connect should succeed with valid local options");
    require(client.is_connected(), "client should report connected");
    require(client.get_poll_fd() >= 0, "connected client should expose internal poll fd");
    require(client.sync() == event::sync_status::timeout, "idle connected client sync should time out");
    require(client.connect().status == shm::io_status::already_open, "second client connect should fail predictably");
    require_io_ok(client.disconnect(), "client disconnect should succeed");
    require(!client.is_connected(), "client should report disconnected");
    require_io_ok(client.disconnect(), "client disconnect should be idempotent");
}

void test_client_publish_to_server_topic() {
    auto server_options = make_server_options("client-publish");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "client publish server should open");

    shm::shm_client client(make_client_options(server_options, "publisher-client"));
    require_io_ok(client.connect(), "publisher client should connect");
    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, client, "telemetry/session", "{\"value\":1}", publish_options),
                  "client publish should succeed through shm_server");
    require(publish_with_server(server, client, "telemetry/missing", "payload", publish_options).status ==
                shm::io_status::unknown_topic,
            "publish to unknown topic should fail predictably");

    require_io_ok(client.disconnect(), "publisher client should disconnect");
    require_io_ok(server.close(), "client publish server should close");
}

void test_client_subscribe_receives_payload() {
    auto server_options = make_server_options("client-subscribe");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "client subscribe server should open");

    shm::shm_client subscriber(make_client_options(server_options, "subscriber-client"));
    shm::shm_client publisher(make_client_options(server_options, "publisher-client"));
    require_io_ok(subscriber.connect(), "subscriber should connect");
    require_io_ok(publisher.connect(), "publisher should connect");

    std::string received_topic;
    std::string received_payload;
    require_io_ok(subscribe_with_server(server, subscriber, "telemetry/session",
                                        [&](std::string_view topic, std::string_view payload) {
                                            received_topic = std::string(topic);
                                            received_payload = std::string(payload);
                                        }),
                  "subscriber should subscribe");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, publisher, "telemetry/session", "{\"session\":42}", publish_options),
                  "publisher should publish to subscribed topic");
    drive_server_and_client_until(
        server, subscriber, [&]() { return !received_payload.empty(); }, "subscriber callback should receive payload");

    require(received_topic == "telemetry/session", "subscriber callback topic should match");
    require(received_payload == "{\"session\":42}", "subscriber callback payload should match");

    require_io_ok(subscriber.disconnect(), "subscriber should disconnect");
    require_io_ok(publisher.disconnect(), "publisher should disconnect");
    require_io_ok(server.close(), "client subscribe server should close");
}

void test_client_multi_topic_callbacks_are_scoped() {
    auto server_options = make_server_options("client-multi-topic");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "multi-topic server should open");

    shm::shm_client client(make_client_options(server_options, "multi-topic-client"));
    require_io_ok(client.connect(), "multi-topic client should connect");

    std::vector<std::pair<std::string, std::string>> received;
    const std::vector<std::string> topics{"telemetry/session", "telemetry/summary"};
    require_io_ok(subscribe_with_server(
                      server, client, topics,
                      [&](std::string_view topic, std::string_view payload) { received.emplace_back(topic, payload); }),
                  "client should subscribe to two topics");
    require(client.subscribe("telemetry/session", [&](std::string_view, std::string_view) {}).status ==
                shm::io_status::already_open,
            "duplicate subscription should fail predictably");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, client, "telemetry/session", "session", publish_options),
                  "client should publish session topic");
    require_io_ok(publish_with_server(server, client, "telemetry/summary", "summary", publish_options),
                  "client should publish summary topic");
    drive_server_until(
        server, [&]() { return server.counter_snapshot().messages_dispatched >= 2U; },
        "server should dispatch both multi-topic publications before the client drains its shared wake");
    require(client.sync() == event::sync_status::ok, "one shared wake should dispatch every active topic");
    require(received.size() == 2U, "one client wake scan should receive both multi-topic payloads");

    require(received.at(0).first == "telemetry/session", "first callback topic should be session");
    require(received.at(0).second == "session", "first callback payload should be session");
    require(received.at(1).first == "telemetry/summary", "second callback topic should be summary");
    require(received.at(1).second == "summary", "second callback payload should be summary");

    require_io_ok(client.disconnect(), "multi-topic client should disconnect");
    require_io_ok(server.close(), "multi-topic server should close");
}

void test_client_unsubscribe_removes_only_one_topic() {
    auto server_options = make_server_options("client-unsubscribe");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "unsubscribe server should open");

    shm::shm_client client(make_client_options(server_options, "unsubscribe-client"));
    require_io_ok(client.connect(), "unsubscribe client should connect");

    std::vector<std::string> received_topics;
    const std::vector<std::string> topics{"telemetry/session", "telemetry/summary"};
    require_io_ok(
        subscribe_with_server(server, client, topics,
                              [&](std::string_view topic, std::string_view) { received_topics.emplace_back(topic); }),
        "unsubscribe client should subscribe to two topics");
    require_io_ok(unsubscribe_with_server(server, client, "telemetry/session"), "unsubscribe should remove one topic");
    require(client.unsubscribe("telemetry/session").status == shm::io_status::not_open,
            "second unsubscribe should fail predictably");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, client, "telemetry/session", "session", publish_options),
                  "publish to removed topic should still be allowed");
    require_io_ok(publish_with_server(server, client, "telemetry/summary", "summary", publish_options),
                  "publish to retained topic should be allowed");
    drive_server_and_client_until(
        server, client, [&]() { return received_topics.size() == 1U; },
        "remaining subscription should receive payload");
    require(received_topics.front() == "telemetry/summary", "unsubscribe should leave other topic alive");

    require_io_ok(client.disconnect(), "unsubscribe client should disconnect");
    require_io_ok(server.close(), "unsubscribe server should close");
}

void test_client_callback_can_unsubscribe_current_topic() {
    auto server_options = make_server_options("callback-unsubscribe-current");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "callback-unsubscribe server should open");

    shm::shm_client subscriber(make_client_options(server_options, "callback-unsubscribe-sub"));
    shm::shm_client publisher(make_client_options(server_options, "callback-unsubscribe-pub"));
    require_io_ok(subscriber.connect(), "callback-unsubscribe subscriber should connect");
    require_io_ok(publisher.connect(), "callback-unsubscribe publisher should connect");

    int callback_count = 0;
    require_io_ok(
        subscribe_with_server(server, subscriber, "telemetry/session",
                              [&](std::string_view topic, std::string_view) {
                                  ++callback_count;
                                  require(topic == "telemetry/session", "callback-unsubscribe topic should match");
                                  require_io_ok(unsubscribe_with_server(server, subscriber, std::string(topic)),
                                                "callback should be able to unsubscribe its current topic");
                              }),
        "callback-unsubscribe subscriber should subscribe");

    event::fd_event_handler handler;
    require(server.register_events(handler), "callback-unsubscribe server should register external handler");
    require(subscriber.register_events(handler), "callback-unsubscribe subscriber should register external handler");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    auto publish_future = std::async(
        std::launch::async, [&]() { return publisher.publish("telemetry/session", "one-shot", publish_options); });
    drive_until_ready(handler, publish_future, "callback-unsubscribe publish handshake should complete");
    require_io_ok(publish_future.get(), "callback-unsubscribe publish should succeed");
    poll_until(
        handler, [&]() { return callback_count == 1; },
        "subscriber callback should run and unsubscribe without invalidating dispatch iteration");

    require(subscriber.unsubscribe("telemetry/session").status == shm::io_status::not_open,
            "callback-unsubscribed topic should no longer be subscribed");
    require(subscriber.unregister_events(handler),
            "callback-unsubscribe subscriber should unregister external handler");
    require(server.unregister_events(handler), "callback-unsubscribe server should unregister external handler");
    require_io_ok(subscriber.disconnect(), "callback-unsubscribe subscriber should disconnect");
    require_io_ok(publisher.disconnect(), "callback-unsubscribe publisher should disconnect");
    require_io_ok(server.close(), "callback-unsubscribe server should close");
}

void test_client_disconnect_closes_liveness() {
    auto server_options = make_server_options("client-liveness");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "liveness server should open");

    shm::shm_client client(make_client_options(server_options, "liveness-client"));
    require_io_ok(client.connect(), "liveness client should connect");
    require_io_ok(subscribe_with_server(server, client, "telemetry/session", [](std::string_view, std::string_view) {}),
                  "liveness client should subscribe");
    require(server.subscriber_snapshots("telemetry/session").size() == 1U,
            "server should expose subscribed client snapshot");

    require_io_ok(client.disconnect(), "client disconnect should close liveness fds");
    drive_server_until(
        server, [&]() { return server.subscriber_snapshots("telemetry/session").empty(); },
        "server should remove subscriber snapshot after liveness closes");

    require_io_ok(server.close(), "liveness server should close");
}

void test_subscriber_client_detects_server_close_with_sync() {
    auto server_options = make_server_options("server-liveness-sub-sync");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "subscriber liveness sync server should open");

    shm::shm_client client(make_client_options(server_options, "server-liveness-sub-sync-client"));
    int error_count = 0;
    shm::io_status last_status = shm::io_status::ok;
    std::string last_message;
    client.set_error_handler([&](shm::io_status status, std::string_view message) {
        ++error_count;
        last_status = status;
        last_message = std::string(message);
    });

    require_io_ok(client.connect(), "subscriber liveness sync client should connect");
    require_io_ok(subscribe_with_server(server, client, "telemetry/session", [](std::string_view, std::string_view) {}),
                  "subscriber liveness sync client should subscribe");

    require_io_ok(server.close(), "subscriber liveness sync server should close");
    drive_client_until(
        client, [&]() { return !client.is_connected(); }, "subscriber client should detect closed server through sync");
    require(error_count == 1, "subscriber liveness detection should report one error");
    require(last_status == shm::io_status::not_open, "subscriber liveness status should be not_open");
    require(last_message.find("server liveness") != std::string::npos,
            "subscriber liveness error should mention server liveness");
    require(client.publish("telemetry/session", "payload").status == shm::io_status::not_open,
            "publish after subscriber liveness loss should fail not_open");
    require(client.subscribe("telemetry/session", [](std::string_view, std::string_view) {}).status ==
                shm::io_status::not_open,
            "subscribe after subscriber liveness loss should fail not_open");
    require(client.unsubscribe("telemetry/session").status == shm::io_status::not_open,
            "unsubscribe after subscriber liveness loss should fail not_open");
}

void test_publisher_client_detects_server_close_with_sync() {
    auto server_options = make_server_options("server-liveness-pub-sync");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "publisher liveness sync server should open");

    shm::shm_client client(make_client_options(server_options, "server-liveness-pub-sync-client"));
    int error_count = 0;
    client.set_error_handler([&](shm::io_status status, std::string_view message) {
        if (status == shm::io_status::not_open && message.find("server liveness") != std::string_view::npos) {
            ++error_count;
        }
    });

    require_io_ok(client.connect(), "publisher liveness sync client should connect");
    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, client, "telemetry/session", "payload", publish_options),
                  "publisher liveness sync client should publish");

    require_io_ok(server.close(), "publisher liveness sync server should close");
    drive_client_until(
        client, [&]() { return !client.is_connected(); }, "publisher client should detect closed server through sync");
    require(error_count == 1, "publisher liveness detection should report one error");
    require(client.publish("telemetry/session", "payload", publish_options).status == shm::io_status::not_open,
            "publish after publisher liveness loss should fail not_open");
}

void test_pubsub_client_detects_server_close_with_external_handler() {
    auto server_options = make_server_options("server-liveness-pubsub-external");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "pubsub liveness external server should open");

    shm::shm_client client(make_client_options(server_options, "server-liveness-pubsub-external-client"));
    int error_count = 0;
    client.set_error_handler([&](shm::io_status status, std::string_view message) {
        if (status == shm::io_status::not_open && message.find("server liveness") != std::string_view::npos) {
            ++error_count;
        }
    });

    require_io_ok(client.connect(), "pubsub liveness external client should connect");
    require_io_ok(subscribe_with_server(server, client, "telemetry/session", [](std::string_view, std::string_view) {}),
                  "pubsub liveness external client should subscribe");
    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, client, "telemetry/session", "payload", publish_options),
                  "pubsub liveness external client should publish");

    event::fd_event_handler handler;
    require(client.register_events(handler), "pubsub liveness client should register external events");
    require_io_ok(server.close(), "pubsub liveness external server should close");
    poll_until(
        handler, [&]() { return !client.is_connected(); },
        "pubsub client should detect closed server through external handler");
    require(error_count == 1, "pubsub external liveness detection should report one error");
    require(client.unregister_events(handler), "pubsub liveness client unregister should be idempotent after loss");
}

void test_client_publish_fail_policy_when_ring_full() {
    auto server_options = make_small_ring_server_options("client-full-ring");
    const auto segment_size = test_segment_size(server_options.topics);
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "full-ring server should open");

    shm::shm_client subscriber(make_client_options(server_options, "full-ring-sub"));
    shm::shm_client publisher(make_client_options(server_options, "full-ring-pub"));
    require_io_ok(subscriber.connect(), "full-ring subscriber should connect");
    require_io_ok(publisher.connect(), "full-ring publisher should connect");
    require_io_ok(
        subscribe_with_server(server, subscriber, "telemetry/session", [](std::string_view, std::string_view) {}),
        "full-ring subscriber should subscribe");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, publisher, "telemetry/session", "first", publish_options),
                  "first publish should fill the single-slot ring");
    shm::shared_memory mapped(server_options.shm_name, static_cast<std::size_t>(segment_size), false);
    auto* base = static_cast<std::uint8_t*>(mapped.get_ptr());
    auto* header = static_cast<shm::SegmentHeader*>(mapped.get_ptr());
    const auto* entry = shm::topic_registry_entry_at(mapped.get_ptr(), *header, 0U);
    auto rb = shm::ring_buffer(base + entry->ring_offset);
    drive_server_until(
        server, [&]() { return rb.get_slot_header(0U)->target_subscribers.load(std::memory_order_acquire) == 1U; },
        "server should dispatch first full-ring publication");
    require(publisher.publish("telemetry/session", "second", publish_options).status != shm::io_status::ok,
            "fail publish policy should return non-ok when the ring is full");

    require_io_ok(subscriber.disconnect(), "full-ring subscriber should disconnect");
    require_io_ok(publisher.disconnect(), "full-ring publisher should disconnect");
    require_io_ok(server.close(), "full-ring server should close");
}

void test_client_external_event_handler_dispatches_callbacks() {
    auto server_options = make_server_options("client-external-events");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "external event server should open");

    shm::shm_client subscriber(make_client_options(server_options, "external-sub"));
    shm::shm_client publisher(make_client_options(server_options, "external-pub"));
    require_io_ok(subscriber.connect(), "external subscriber should connect");
    require_io_ok(publisher.connect(), "external publisher should connect");

    std::string received_payload;
    require_io_ok(subscribe_with_server(
                      server, subscriber, "telemetry/session",
                      [&](std::string_view, std::string_view payload) { received_payload = std::string(payload); }),
                  "external subscriber should subscribe");

    event::fd_event_handler handler;
    require(server.register_events(handler), "server should register external handler");
    require(subscriber.register_events(handler), "subscriber should register external handler");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    auto publish_future = std::async(
        std::launch::async, [&]() { return publisher.publish("telemetry/session", "external", publish_options); });
    drive_until_ready(handler, publish_future, "external handler publish handshake should complete");
    require_io_ok(publish_future.get(), "external handler publish should succeed");
    poll_until(
        handler, [&]() { return received_payload == "external"; },
        "external event handler should dispatch subscriber callback");

    require(subscriber.unregister_events(handler), "subscriber should unregister external handler");
    require(server.unregister_events(handler), "server should unregister external handler");
    require_io_ok(subscriber.disconnect(), "external subscriber should disconnect");
    require_io_ok(publisher.disconnect(), "external publisher should disconnect");
    require_io_ok(server.close(), "external event server should close");
}

void test_client_external_event_registration_after_async_subscribe() {
    auto server_options = make_server_options("client-external-safe-order");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "safe-order server should open");

    shm::shm_client subscriber(make_client_options(server_options, "safe-order-sub"));
    shm::shm_client publisher(make_client_options(server_options, "safe-order-pub"));
    require_io_ok(subscriber.connect(), "safe-order subscriber should connect");
    require_io_ok(publisher.connect(), "safe-order publisher should connect");

    event::fd_event_handler handler;
    require(server.register_events(handler), "safe-order server should register external handler");

    std::string received_payload;
    auto subscribe_future = std::async(std::launch::async, [&]() {
        return subscriber.subscribe("telemetry/session", [&](std::string_view, std::string_view payload) {
            received_payload = std::string(payload);
        });
    });
    drive_until_ready(handler, subscribe_future, "safe-order subscribe should complete");
    require_io_ok(subscribe_future.get(), "safe-order subscriber should subscribe");

    // Keep fd_event_handler ownership single-threaded: the async subscribe handshake runs while only the server is
    // registered, then client subscriber FDs are registered by the polling thread after the handshake completes.
    require(subscriber.register_events(handler),
            "safe-order subscriber should register external handler after subscribe");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    auto publish_future = std::async(
        std::launch::async, [&]() { return publisher.publish("telemetry/session", "safe-order", publish_options); });
    drive_until_ready(handler, publish_future, "safe-order publish handshake should complete");
    require_io_ok(publish_future.get(), "safe-order publish should succeed");
    poll_until(
        handler, [&]() { return received_payload == "safe-order"; },
        "safe-order external handler should dispatch subscriber callback");

    require(subscriber.unregister_events(handler), "safe-order subscriber should unregister external handler");
    require(server.unregister_events(handler), "safe-order server should unregister external handler");
    require_io_ok(subscriber.disconnect(), "safe-order subscriber should disconnect");
    require_io_ok(publisher.disconnect(), "safe-order publisher should disconnect");
    require_io_ok(server.close(), "safe-order server should close");
}

void test_client_external_unregister_shared_fd_counterpart() {
    auto server_options = make_server_options("client-shared-fd-unregister");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "shared-fd-unregister server should open");

    shm::shm_client client(make_client_options(server_options, "shared-fd-unregister-client"));
    require_io_ok(client.connect(), "shared-fd-unregister client should connect");

    int error_count = 0;
    client.set_error_handler([&](shm::io_status status, std::string_view message) {
        if (status == shm::io_status::resource_error &&
            message.find("Failed to re-register SHM subscriber event fd") != std::string_view::npos) {
            ++error_count;
        }
    });

    const std::vector<std::string> topics{"telemetry/session", "telemetry/summary"};
    require_io_ok(subscribe_with_server(server, client, topics, [](std::string_view, std::string_view) {}),
                  "shared-fd-unregister client should subscribe to two topics");

    event::fd_event_handler handler;
    require(client.register_events(handler), "shared-fd-unregister client should register external handler");
    require_io_ok(unsubscribe_with_server(server, client, "telemetry/summary"),
                  "unregistering a counterpart subscription should not re-register an active shared fd");
    require(error_count == 0, "shared-fd-unregister should not report spurious re-register errors");

    require(client.unregister_events(handler), "shared-fd-unregister client should unregister external handler");
    require_io_ok(client.disconnect(), "shared-fd-unregister client should disconnect");
    require_io_ok(server.close(), "shared-fd-unregister server should close");
}

void test_cross_process_reusable_api_two_clients() {
    auto server_options = make_cross_process_server_options("two-clients");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "cross-process API server should open");

    auto ready_a = make_pipe("client A ready pipe should open");
    auto ready_b = make_pipe("client B ready pipe should open");
    auto start_a = make_pipe("client A start pipe should open");
    auto start_b = make_pipe("client B start pipe should open");

    const cross_process_client_config client_a{"client-a",
                                               "client_b/state",
                                               "client_b/telemetry",
                                               "client_a/state",
                                               "client_a/telemetry",
                                               "{\"client\":\"b\",\"state\":1}",
                                               "{\"client\":\"b\",\"telemetry\":2}",
                                               "{\"client\":\"a\",\"state\":1}",
                                               "{\"client\":\"a\",\"telemetry\":2}"};
    const cross_process_client_config client_b{"client-b",
                                               "client_a/state",
                                               "client_a/telemetry",
                                               "client_b/state",
                                               "client_b/telemetry",
                                               "{\"client\":\"a\",\"state\":1}",
                                               "{\"client\":\"a\",\"telemetry\":2}",
                                               "{\"client\":\"b\",\"state\":1}",
                                               "{\"client\":\"b\",\"telemetry\":2}"};

    const auto child_a = fork_cross_process_client(server_options, client_a, ready_a, start_a);
    const auto child_b = fork_cross_process_client(server_options, client_b, ready_b, start_b);

    wait_ready_while_driving_server(ready_a.read, ready_b.read, server);
    ready_a.read.close();
    ready_b.read.close();

    require(write_byte(start_a.write, 's'), "parent should start client A");
    require(write_byte(start_b.write, 's'), "parent should start client B");
    start_a.write.close();
    start_b.write.close();

    wait_child_success_while_driving_server(child_a, child_b, server);
    wait_cross_process_snapshots_empty(server);
    require_io_ok(server.close(), "cross-process API server should close");
}

void test_cross_process_client_detects_server_process_death() {
    auto server_options = make_server_options("server-liveness-process-death");
    server_options.shm_name = "/everest-shm-api-liveness-death-" + std::to_string(::getpid());
    server_options.control_socket_name = "/tmp/everest-shm-api-liveness-death-" + std::to_string(::getpid());
    server_options.unlink_control_socket_on_close = true;

    auto ready = make_pipe("liveness server ready pipe should open");
    const auto server_child = fork_cross_process_liveness_server(server_options, ready);
    wait_single_ready(ready.read, "liveness server child should become ready");
    ready.read.close();

    shm::shm_client client(make_client_options(server_options, "server-liveness-process-death-client"));
    int error_count = 0;
    client.set_error_handler([&](shm::io_status status, std::string_view message) {
        if (status == shm::io_status::not_open && message.find("server liveness") != std::string_view::npos) {
            ++error_count;
        }
    });
    require_io_ok(client.connect(), "process-death client should connect");
    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(client.publish("telemetry/session", "payload", publish_options),
                  "process-death client should publish through server child");

    ::kill(server_child, SIGKILL);
    int status = 0;
    require(::waitpid(server_child, &status, 0) == server_child, "server child waitpid should complete");
    require(WIFSIGNALED(status), "server child should die from signal");

    drive_client_until(
        client, [&]() { return !client.is_connected(); },
        "client should detect server process death through liveness fd");
    require(error_count == 1, "process-death liveness detection should report one error");
    require(client.publish("telemetry/session", "payload", publish_options).status == shm::io_status::not_open,
            "publish after process-death liveness loss should fail not_open");
    ::unlink(server_options.control_socket_name.c_str());
    ::shm_unlink(server_options.shm_name.c_str());
}

// Count the file descriptors in /proc/self/fd whose readlink target references the
// given POSIX SHM segment. The kernel-exposed name for shm_open()'d FDs is
// "/dev/shm/<name>" (and the inode-target name appears with the leading slash retained),
// so we match by the suffix "<shm_name>" which is unique enough for the per-test names.
int count_shm_segment_fds(const std::string& shm_name) {
    DIR* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) {
        return -1;
    }
    int count = 0;
    while (auto* entry = ::readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);
        char target[512];
        const auto bytes = ::readlink(path, target, sizeof(target) - 1);
        if (bytes <= 0) {
            continue;
        }
        target[bytes] = '\0';
        if (std::string(target).find(shm_name) != std::string::npos) {
            ++count;
        }
    }
    ::closedir(dir);
    return count;
}

void test_client_reuses_segment_mapping_across_topics() {
    auto server_options = make_server_options("shared-fd");
    // Distinct per-PID name to keep /proc/self/fd matching unambiguous.
    server_options.shm_name = "/everest-shm-shared-fd-" + std::to_string(::getpid());
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "segment-sharing server should open");

    shm::shm_client client(make_client_options(server_options, "shared-fd-client"));
    require_io_ok(client.connect(), "segment-sharing client should connect");

    // The shm_server owns one fd to the segment in-process; we measure client-side additions
    // relative to this baseline. (For an out-of-process module the baseline is zero.)
    const auto baseline_fds = count_shm_segment_fds(server_options.shm_name);
    require(baseline_fds >= 0, "/proc/self/fd should be enumerable on Linux");
    require(baseline_fds == 1,
            "baseline should reflect exactly the server-owned segment fd before any client topic handles exist");

    require_io_ok(subscribe_with_server(server, client, "telemetry/session", [](std::string_view, std::string_view) {}),
                  "client should subscribe to first topic");
    const auto after_first_sub = count_shm_segment_fds(server_options.shm_name);
    require(after_first_sub == baseline_fds + 1,
            "first client topic subscribe should open exactly one new SHM segment fd");

    require_io_ok(subscribe_with_server(server, client, "telemetry/summary", [](std::string_view, std::string_view) {}),
                  "client should subscribe to second topic on the same segment");
    const auto after_second_sub = count_shm_segment_fds(server_options.shm_name);
    require(after_second_sub == baseline_fds + 1,
            "second subscribe on same segment must reuse the cached mapping (no duplicate fd)");

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    require_io_ok(publish_with_server(server, client, "telemetry/session", "session-payload", publish_options),
                  "publish on existing topic should reuse cached mapping");
    const auto after_publish = count_shm_segment_fds(server_options.shm_name);
    require(after_publish == baseline_fds + 1,
            "publisher on existing segment must reuse cached mapping (no duplicate fd)");

    require_io_ok(publish_with_server(server, client, "telemetry/summary", "summary-payload", publish_options),
                  "publish on a different topic should reuse cached mapping");
    const auto after_second_publish = count_shm_segment_fds(server_options.shm_name);
    require(after_second_publish == baseline_fds + 1,
            "second publisher on same segment must reuse cached mapping (no duplicate fd)");

    require_io_ok(unsubscribe_with_server(server, client, "telemetry/session"),
                  "client unsubscribe should release one topic but keep cached mapping alive");
    const auto after_one_unsub = count_shm_segment_fds(server_options.shm_name);
    require(after_one_unsub == baseline_fds + 1,
            "unsubscribe of one topic should leave the cached mapping alive for the remaining topic");

    require_io_ok(unsubscribe_with_server(server, client, "telemetry/summary"),
                  "client unsubscribe should release last subscriber");
    // Publishers still hold references to the segment, so the cached mapping must persist.
    const auto after_all_unsub = count_shm_segment_fds(server_options.shm_name);
    require(after_all_unsub == baseline_fds + 1, "remaining publisher handles should keep the segment mapping alive");

    require_io_ok(client.disconnect(), "client disconnect should release the cached segment mapping");
    const auto after_disconnect = count_shm_segment_fds(server_options.shm_name);
    require(after_disconnect == baseline_fds,
            "disconnect should close every client-owned segment fd, leaving only the server fd");

    require_io_ok(server.close(), "segment-sharing server should close");
}

void test_client_registered_topics_lists_registry() {
    auto server_options = make_server_options("registered-topics");
    shm::shm_server server(server_options);
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return;
    }
    require_io_ok(open_result, "registered_topics server should open");

    shm::shm_client client(make_client_options(server_options, "registered-topics-client"));
    require_io_ok(client.connect(), "registered_topics client should connect");

    auto future = std::async(std::launch::async, [&]() { return client.registered_topics(); });
    drive_until_ready(server, future, "registered_topics query should complete");
    auto result = future.get();
    require(static_cast<bool>(result), "registered_topics query should succeed");
    require(result.topics.size() == 2U, "registered_topics should expose both server topics");
    require(result.topics.at(0) == "telemetry/session", "registered_topics should be sorted");
    require(result.topics.at(1) == "telemetry/summary", "registered_topics should be sorted");

    require_io_ok(client.disconnect(), "registered_topics client should disconnect");
    require_io_ok(server.close(), "registered_topics server should close");
}

void test_client_registered_topics_fails_without_connect() {
    auto options = make_client_options(make_server_options("registered-topics-disconnected"),
                                       "registered-topics-disconnected-client");
    shm::shm_client client(std::move(options));
    auto result = client.registered_topics();
    require(!result, "registered_topics on a disconnected client should fail");
    require(result.status == shm::io_status::not_open,
            "registered_topics on a disconnected client should report not_open");
    require(result.topics.empty(), "registered_topics failure should not return topics");
}

void test_client_registered_topics_reports_socket_error_when_server_missing() {
    shm::client_options options;
    options.client_id = "registered-topics-missing-client";
    options.control.server_name = "/tmp/everest-shm-no-server-" + std::to_string(::getpid());
    options.control.server_abstract_namespace = false;

    shm::shm_client client(std::move(options));
    require_io_ok(client.connect(), "registered_topics client without server should still connect locally");
    auto result = client.registered_topics();
    require(!result, "registered_topics without a server should fail");
    require(result.status == shm::io_status::resource_error,
            "registered_topics without a server should map socket error to resource_error");
    require_io_ok(client.disconnect(), "registered_topics client should disconnect after failure");
}

} // namespace

int main() {
    test_invalid_server_options();
    test_invalid_client_options();
    test_open_initializes_two_topic_segment();
    test_server_options_defaults_and_explicit_segment_layout();
    test_server_cleanup_after_partial_open_failure();
    test_control_handshakes_snapshots_and_close_cleanup();
    test_event_loop_dispatches_publication_and_ack();
    test_event_loop_ack_unblocks_current_write_slot_publisher();
    test_client_connect_disconnect_state();
    test_client_publish_to_server_topic();
    test_client_subscribe_receives_payload();
    test_client_multi_topic_callbacks_are_scoped();
    test_client_unsubscribe_removes_only_one_topic();
    test_client_callback_can_unsubscribe_current_topic();
    test_client_disconnect_closes_liveness();
    test_subscriber_client_detects_server_close_with_sync();
    test_publisher_client_detects_server_close_with_sync();
    test_pubsub_client_detects_server_close_with_external_handler();
    test_client_publish_fail_policy_when_ring_full();
    test_client_external_event_handler_dispatches_callbacks();
    test_client_external_event_registration_after_async_subscribe();
    test_client_external_unregister_shared_fd_counterpart();
    test_cross_process_reusable_api_two_clients();
    test_cross_process_client_detects_server_process_death();
    test_client_reuses_segment_mapping_across_topics();
    test_client_registered_topics_lists_registry();
    test_client_registered_topics_fails_without_connect();
    test_client_registered_topics_reports_socket_error_when_server_missing();

    shm::server_options server_options;
    server_options.shm_name = "/everest-shm-api-test";
    server_options.control_socket_name = "/tmp/everest-shm-control-sub-" + std::to_string(::getpid()) + "-api";
    server_options.control_socket_abstract_namespace = false;
    server_options.topics.push_back({"telemetry/session", 8, 4096});

    shm::shm_server server(std::move(server_options));
    event::fd_event_register_interface* server_register = &server;
    event::fd_event_sync_interface* server_sync = &server;
    assert(server_register != nullptr);
    assert(server_sync != nullptr);
    assert(!server.is_open());
    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
    } else {
        assert(open_result.status == shm::io_status::ok);
        assert(server.is_open());
        assert(server.get_poll_fd() >= 0);
        assert(server.sync() == event::sync_status::timeout);
        assert(server.close().status == shm::io_status::ok);
        assert(!server.is_open());
    }

    shm::client_options client_options;
    client_options.client_id = "compile-shape-client";
    client_options.control.server_name = "everest-shm-api-test";

    shm::shm_client client(std::move(client_options));
    event::fd_event_register_interface* client_register = &client;
    event::fd_event_sync_interface* client_sync = &client;
    assert(client_register != nullptr);
    assert(client_sync != nullptr);
    assert(client.client_id() == "compile-shape-client");
    assert(!client.is_connected());

    bool error_reported = false;
    client.set_error_handler([&error_reported](shm::io_status status, std::string_view message) {
        error_reported = status == shm::io_status::resource_error && !message.empty();
    });

    assert(client.connect().status == shm::io_status::ok);
    assert(client.is_connected());

    shm::publish_options publish_options;
    publish_options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
    assert(client.publish("telemetry/session", "{\"value\":1}", publish_options).status ==
           shm::io_status::resource_error);
    assert(error_reported);

    auto callback = [](std::string_view topic, std::string_view payload) {
        assert(!topic.empty());
        assert(!payload.empty());
    };
    assert(client.subscribe("telemetry/session", callback).status == shm::io_status::resource_error);

    const std::vector<std::string> topics{"telemetry/session", "telemetry/summary"};
    assert(client.subscribe(topics, callback).status == shm::io_status::resource_error);
    assert(client.unsubscribe("telemetry/session").status == shm::io_status::not_open);
    assert(client.disconnect().status == shm::io_status::ok);

    assert(static_cast<bool>(shm::io_result{shm::io_status::ok, {}}));
    assert(shm::to_string(shm::io_status::not_implemented) == "not_implemented");
}
