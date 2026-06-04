// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_client.hpp>
#include <everest/io/shm/control_server.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/structures.hpp>
#include <everest/io/shm/topic.hpp>
#include <everest/io/shm/topic_runtime.hpp>
#include <everest/io/socket/socket.hpp>

using everest::lib::io::event::unique_fd;
using everest::lib::io::shm::align_shm_topic_offset;
using everest::lib::io::shm::coordinator;
using everest::lib::io::shm::first_topic_ring_offset_after_registry;
using everest::lib::io::shm::initialize_segment_header;
using everest::lib::io::shm::initialize_topic_registry_entry;
using everest::lib::io::shm::ring_buffer;
using everest::lib::io::shm::RingbufferMetadata;
using everest::lib::io::shm::SegmentHeader;
using everest::lib::io::shm::topic;
using everest::lib::io::shm::topic_registry_entry_at;
using everest::lib::io::shm::topic_runtime_registry;
using everest::lib::io::shm::TopicRegistryEntry;
using namespace everest::lib::io::shm::control;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

struct control_response {
    handshake_response response;
    std::vector<unique_fd> fds;
};

struct TestSegment {
    static constexpr std::uint64_t segment_size = 16384U;
    static constexpr std::uint64_t registry_offset = sizeof(SegmentHeader);
    static constexpr std::uint32_t registry_capacity = 4U;

    alignas(RingbufferMetadata) std::array<std::uint8_t, segment_size> bytes{};

    TestSegment() {
        const auto status = initialize_segment_header(bytes.data(), bytes.size(), registry_offset, registry_capacity);
        require(static_cast<std::uint32_t>(status) == 0U, "segment header initialization should succeed");
        header()->registry_entry_count = 2U;

        const auto ring_a_offset = first_topic_ring_offset_after_registry(*header());
        const auto ring_b_offset =
            align_shm_topic_offset(ring_a_offset + ring_buffer::calculate_required_size(1U, 128U));

        require(static_cast<std::uint32_t>(
                    initialize_topic_registry_entry(entry(0U), "topic/a", ring_a_offset, 1U, 128U)) == 0U,
                "topic A registry entry should initialize");
        require(static_cast<std::uint32_t>(
                    initialize_topic_registry_entry(entry(1U), "topic/b", ring_b_offset, 1U, 256U)) == 0U,
                "topic B registry entry should initialize");

        coordinator::initialize_ring_buffer(ring_buffer(bytes.data() + entry(0U)->ring_offset), entry(0U)->total_slots,
                                            entry(0U)->slot_size, 0U);
        coordinator::initialize_ring_buffer(ring_buffer(bytes.data() + entry(1U)->ring_offset), entry(1U)->total_slots,
                                            entry(1U)->slot_size, 0U);
    }

    SegmentHeader* header() {
        return reinterpret_cast<SegmentHeader*>(bytes.data());
    }

    TopicRegistryEntry* entry(std::uint32_t index) {
        return topic_registry_entry_at(bytes.data(), *header(), index);
    }
};

void send_control_request(int socket_fd, const handshake_request& request, const std::vector<int>& fds_to_send = {}) {
    const auto payload = nlohmann::json(request).dump();

    iovec iov{};
    iov.iov_base = const_cast<char*>(payload.data());
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    std::array<char, CMSG_SPACE(sizeof(int) * 4)> control{};
    if (!fds_to_send.empty()) {
        message.msg_control = control.data();
        message.msg_controllen = CMSG_SPACE(sizeof(int) * fds_to_send.size());
        auto* cmsg = CMSG_FIRSTHDR(&message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds_to_send.size());
        std::memcpy(CMSG_DATA(cmsg), fds_to_send.data(), sizeof(int) * fds_to_send.size());
    }

    require(::sendmsg(socket_fd, &message, 0) == static_cast<ssize_t>(payload.size()),
            "client should send handshake request");
}

control_response receive_control_response(int socket_fd) {
    std::array<char, 4096> payload{};
    std::array<char, CMSG_SPACE(sizeof(int) * 8)> control{};

    iovec iov{};
    iov.iov_base = payload.data();
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();

    const auto bytes = ::recvmsg(socket_fd, &message, 0);
    require(bytes > 0, "client should receive a control response");
    require((message.msg_flags & MSG_TRUNC) == 0, "control response payload should not truncate");
    require((message.msg_flags & MSG_CTRUNC) == 0, "control response fds should not truncate");

    control_response result;
    result.response =
        nlohmann::json::parse(std::string(payload.data(), static_cast<std::size_t>(bytes))).get<handshake_response>();

    for (auto* cmsg = CMSG_FIRSTHDR(&message); cmsg != nullptr; cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const auto fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const auto* raw_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (std::size_t i = 0; i < fd_count; ++i) {
            result.fds.emplace_back(raw_fds[i]);
        }
    }

    return result;
}

control_response request_handshake(server& control_server, const std::string& server_name,
                                   const handshake_request& request) {
    client_options options;
    options.server_name = server_name;
    options.server_abstract_namespace = false;
    session sess(options);
    require(sess.is_open(), "client session should open");

    auto future = std::async(std::launch::async, [&sess, request]() { return sess.handshake(request); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::optional<handshake_response> server_response;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!server_response.has_value()) {
            server_response = control_server.handle_next();
        }
        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    require(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready,
            "server-driving loop should observe client response");
    auto result = future.get();
    require(result.is_accepted(), "client handshake should be accepted");
    require(server_response.has_value(), "server should report handled handshake");
    require(server_response->accepted == result.accepted->response.accepted,
            "server task should report same accepted state");
    require(server_response->error == result.accepted->response.error, "server task should report same error state");

    control_response response;
    response.response = result.accepted->response;
    if (result.accepted->fds.publication.is_fd()) {
        response.fds.push_back(std::move(result.accepted->fds.publication));
    }
    if (result.accepted->fds.release.is_fd()) {
        response.fds.push_back(std::move(result.accepted->fds.release));
    }
    if (result.accepted->fds.broadcast.is_fd()) {
        response.fds.push_back(std::move(result.accepted->fds.broadcast));
    }
    if (result.accepted->fds.ack.is_fd()) {
        response.fds.push_back(std::move(result.accepted->fds.ack));
    }
    return response;
}

bool fd_is_readable(int fd) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
}

std::string wake_fd_key(int fd) {
    std::ifstream fdinfo("/proc/self/fdinfo/" + std::to_string(fd));
    std::string line;
    while (std::getline(fdinfo, line)) {
        constexpr std::string_view prefix = "eventfd-id:";
        if (line.rfind(prefix.data(), 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return {};
}

void register_runtime_topics(topic_runtime_registry& runtimes, TestSegment& segment) {
    require(runtimes.register_active_topics(segment.bytes.data(), segment.bytes.size()) == 2U,
            "two active topic entries should register");
}

void test_two_active_entries_produce_distinct_runtime_lookups() {
    TestSegment segment;
    topic_runtime_registry runtimes("/everest_topic_runtime_lookup_test");
    register_runtime_topics(runtimes, segment);

    require(runtimes.size() == 2U, "runtime registry should contain two topics");
    auto* runtime_a = runtimes.find("topic/a");
    auto* runtime_b = runtimes.find("topic/b");
    require(runtime_a != nullptr, "topic A lookup should succeed");
    require(runtime_b != nullptr, "topic B lookup should succeed");
    require(runtime_a != runtime_b, "topic lookups should return distinct runtimes");
    require(&runtime_a->topic_coordinator() != &runtime_b->topic_coordinator(),
            "topic lookups should return distinct coordinators");
    require(runtimes.find("topic/missing") == nullptr, "missing topic lookup should fail");
}

void test_runtime_topics_share_wake_descriptors() {
    TestSegment segment;
    topic_runtime_registry runtimes("/everest_topic_runtime_shared_wake_test");
    register_runtime_topics(runtimes, segment);

    auto* runtime_a = runtimes.find("topic/a");
    auto* runtime_b = runtimes.find("topic/b");
    require(runtime_a != nullptr && runtime_b != nullptr, "both topic runtimes should exist for shared wake test");

    const auto sub_a_id = runtime_a->topic_coordinator().add_subscriber().id;
    const auto sub_b_id = runtime_b->topic_coordinator().add_subscriber().id;
    auto pub_a = runtime_a->topic_coordinator().make_publication_fd();
    auto pub_b = runtime_b->topic_coordinator().make_publication_fd();
    auto release_a = runtime_a->topic_coordinator().make_release_fd();
    auto release_b = runtime_b->topic_coordinator().make_release_fd();
    auto ack_a = runtime_a->topic_coordinator().make_ack_fd(sub_a_id);
    auto ack_b = runtime_b->topic_coordinator().make_ack_fd(sub_b_id);
    auto broadcast_a = runtime_a->topic_coordinator().make_broadcast_fd(sub_a_id);
    auto broadcast_b = runtime_b->topic_coordinator().make_broadcast_fd(sub_b_id);

    require(!wake_fd_key(static_cast<int>(pub_a)).empty(), "publication fd should expose eventfd id");
    require(wake_fd_key(static_cast<int>(pub_a)) == wake_fd_key(static_cast<int>(pub_b)),
            "publication wake fd should be shared");
    require(wake_fd_key(static_cast<int>(release_a)) == wake_fd_key(static_cast<int>(release_b)),
            "release wake fd should be shared");
    require(wake_fd_key(static_cast<int>(ack_a)) == wake_fd_key(static_cast<int>(ack_b)),
            "ACK wake fd should be shared");
    require(wake_fd_key(static_cast<int>(broadcast_a)) == wake_fd_key(static_cast<int>(broadcast_b)),
            "broadcast wake fd should be shared");
}

void test_control_server_registers_two_topic_endpoints_with_matching_metadata() {
    TestSegment segment;
    const std::string shm_name = "/everest_topic_runtime_control_test";
    topic_runtime_registry runtimes(shm_name);
    register_runtime_topics(runtimes, segment);

    server control_server;
    const auto server_name = "/tmp/everest-shm-topic-runtime-" + std::to_string(::getpid());
    ::unlink(server_name.c_str());
    require(control_server.open(server_name, false), "control server should open");
    runtimes.register_with_control_server(control_server);

    const auto response_a = request_handshake(
        control_server, server_name, handshake_request{protocol_version, "module-a", "topic/a", role::subscriber});
    const auto response_b = request_handshake(
        control_server, server_name, handshake_request{protocol_version, "module-b", "topic/b", role::subscriber});

    require(response_a.response.accepted, "topic A subscriber handshake should be accepted");
    require(response_b.response.accepted, "topic B subscriber handshake should be accepted");
    require(response_a.response.mapping->shm_name == shm_name, "topic A mapping should use caller shm name");
    require(response_b.response.mapping->shm_name == shm_name, "topic B mapping should use caller shm name");
    require(response_a.response.mapping->ring_offset == segment.entry(0U)->ring_offset,
            "topic A mapping should use topic A ring offset");
    require(response_b.response.mapping->ring_offset == segment.entry(1U)->ring_offset,
            "topic B mapping should use topic B ring offset");
    require(response_a.response.mapping->total_slots == segment.entry(0U)->total_slots,
            "topic A mapping should use topic A slot count");
    require(response_b.response.mapping->slot_size == segment.entry(1U)->slot_size,
            "topic B mapping should use topic B slot size");
    require(response_a.response.fds.has_value(), "topic A subscriber handshake should include fd bundle");
    require(response_b.response.fds.has_value(), "topic B subscriber handshake should include fd bundle");
    require(response_a.response.fds->broadcast == 0U, "topic A subscriber broadcast fd index should be first");
    require(response_b.response.fds->broadcast == 0U, "topic B subscriber broadcast fd index should be first");
    require(response_a.response.fds->ack == 1U, "topic A subscriber ack fd index should follow broadcast");
    require(response_b.response.fds->ack == 1U, "topic B subscriber ack fd index should follow broadcast");
    require(response_a.fds.size() == 2U, "topic A subscriber handshake should pass two fds");
    require(response_b.fds.size() == 2U, "topic B subscriber handshake should pass two fds");
    require(response_a.fds[0].is_fd(), "topic A subscriber broadcast fd should be valid");
    require(response_b.fds[0].is_fd(), "topic B subscriber broadcast fd should be valid");
    require(response_a.fds[1].is_fd(), "topic A subscriber ack fd should be valid");
    require(response_b.fds[1].is_fd(), "topic B subscriber ack fd should be valid");
}

void test_publish_dispatch_and_ack_accounting_stay_independent_per_topic() {
    TestSegment segment;
    topic_runtime_registry runtimes("/everest_topic_runtime_publish_test");
    register_runtime_topics(runtimes, segment);
    auto* runtime_a = runtimes.find("topic/a");
    auto* runtime_b = runtimes.find("topic/b");
    require(runtime_a != nullptr && runtime_b != nullptr, "both topic runtimes should exist");

    const auto sub_a_id = runtime_a->topic_coordinator().add_subscriber().id;
    const auto sub_b_id = runtime_b->topic_coordinator().add_subscriber().id;

    std::vector<std::string> received_a;
    std::vector<std::string> received_b;

    auto publisher_a = topic::make_publisher(runtime_a->ring(), runtime_a->topic_coordinator().make_publication_fd(),
                                             runtime_a->topic_coordinator().make_release_fd());
    auto subscriber_a = topic::make_subscriber(
        runtime_a->ring(), runtime_a->topic_coordinator().make_broadcast_fd(sub_a_id),
        runtime_a->topic_coordinator().make_ack_fd(sub_a_id),
        [&](const std::string& data) { received_a.push_back(data); }, static_cast<std::uint32_t>(sub_a_id));

    auto publisher_b = topic::make_publisher(runtime_b->ring(), runtime_b->topic_coordinator().make_publication_fd(),
                                             runtime_b->topic_coordinator().make_release_fd());
    auto subscriber_b = topic::make_subscriber(
        runtime_b->ring(), runtime_b->topic_coordinator().make_broadcast_fd(sub_b_id),
        runtime_b->topic_coordinator().make_ack_fd(sub_b_id),
        [&](const std::string& data) { received_b.push_back(data); }, static_cast<std::uint32_t>(sub_b_id));

    require(publisher_a->publish("{\"topic\":\"a\",\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "topic A first publish should succeed");
    require(!publisher_a->publish("{\"topic\":\"a\",\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "topic A should apply back-pressure before topic A ACK");
    require(publisher_b->publish("{\"topic\":\"b\",\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "topic B publish should remain independent while topic A is blocked");
    require(!publisher_b->publish("{\"topic\":\"b\",\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "topic B should apply back-pressure before topic B ACK");

    require(runtime_a->topic_coordinator().handle_pending_publication() == 1U,
            "topic A publication should dispatch once");
    require(fd_is_readable(subscriber_a->get_event_fd()->get_raw_fd()), "topic A subscriber wake should be notified");
    const auto received_b_before_topic_a_dispatch = received_b.size();
    require(subscriber_b->handle_pending_count() == 0U, "topic A publication should not dispatch topic B work");
    require(received_b.size() == received_b_before_topic_a_dispatch,
            "topic A publication should not deliver to topic B subscriber");

    subscriber_a->handle_event();
    require(runtime_a->topic_coordinator().handle_ack(sub_a_id), "topic A ACK should release only topic A");
    require(publisher_a->publish("{\"topic\":\"a\",\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "topic A should publish again after topic A ACK");
    require(!publisher_b->publish("{\"topic\":\"b\",\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "topic B should remain blocked until topic B ACK");

    require(runtime_b->topic_coordinator().handle_pending_publication() == 1U,
            "topic B publication should dispatch once");
    require(fd_is_readable(subscriber_b->get_event_fd()->get_raw_fd()), "topic B subscriber should be notified");

    subscriber_b->handle_event();
    require(runtime_b->topic_coordinator().handle_ack(sub_b_id), "topic B ACK should release only topic B");
    require(publisher_b->publish("{\"topic\":\"b\",\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "topic B should publish again after topic B ACK");
    require(!publisher_a->publish("{\"topic\":\"a\",\"value\":3}", topic::full_buffer_policy::fail_immediately),
            "topic A should remain blocked until topic A second ACK");

    require(runtime_a->topic_coordinator().handle_pending_publication() == 1U,
            "topic A second publication should dispatch once");
    subscriber_a->handle_event();
    require(runtime_a->topic_coordinator().handle_ack(sub_a_id), "topic A second ACK should release topic A");

    require(runtime_b->topic_coordinator().handle_pending_publication() == 1U,
            "topic B second publication should dispatch once");
    subscriber_b->handle_event();
    require(runtime_b->topic_coordinator().handle_ack(sub_b_id), "topic B second ACK should release topic B");

    require((received_a == std::vector<std::string>{"{\"topic\":\"a\",\"value\":1}", "{\"topic\":\"a\",\"value\":2}"}),
            "topic A subscriber should receive only topic A messages");
    require((received_b == std::vector<std::string>{"{\"topic\":\"b\",\"value\":1}", "{\"topic\":\"b\",\"value\":2}"}),
            "topic B subscriber should receive only topic B messages");
}

int main() {
    test_two_active_entries_produce_distinct_runtime_lookups();
    test_runtime_topics_share_wake_descriptors();
    test_control_server_registers_two_topic_endpoints_with_matching_metadata();
    test_publish_dispatch_and_ack_accounting_stay_independent_per_topic();
    return 0;
}
