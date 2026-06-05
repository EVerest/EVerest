// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_client.hpp>
#include <everest/io/shm/control_server.hpp>
#include <everest/io/shm/coordinator.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/socket/socket.hpp>

using everest::lib::io::event::unique_fd;
using everest::lib::io::shm::coordinator;
using everest::lib::io::shm::ring_buffer;
using everest::lib::io::shm::shared_memory;
using namespace everest::lib::io::shm::control;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

constexpr auto handshake_timeout_ms = 5000;

client_options local_client_options(const std::string& server_name) {
    client_options opts;
    opts.server_name = server_name;
    opts.server_abstract_namespace = false;
    return opts;
}

template <typename Future> void drive_until_ready(server& control_server, Future& future) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(handshake_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return;
        }
        (void)control_server.handle_next_message();
    }
    require(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready,
            "server-driving loop should observe client response");
}

struct test_topic {
    std::string shm_name;
    shared_memory shm;
    ring_buffer rb;
    std::unique_ptr<coordinator> manager;

    test_topic(std::string name, std::uint32_t slots, std::uint32_t slot_size) :
        shm_name(std::move(name)),
        shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true),
        rb(shm.get_ptr()) {
        shm.unlink();
        coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
        manager = std::make_unique<coordinator>(rb);
    }
};

client_handshake_result handshake_against_server(server& control_server, session& sess,
                                                 const handshake_request& request) {
    auto future = std::async(std::launch::async, [&sess, request]() { return sess.handshake(request); });
    drive_until_ready(control_server, future);
    return future.get();
}

topic_list_result list_topics_against_server(server& control_server, session& sess,
                                             const list_topics_request& request) {
    auto future = std::async(std::launch::async, [&sess, request]() { return sess.list_topics(request); });
    drive_until_ready(control_server, future);
    return future.get();
}

void test_publisher_client_handshake() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    test_topic topic("/everest_shm_control_client_publisher_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-pub-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "publisher client test server should open");
    control_server.register_topic("topic/pub",
                                  server::topic_endpoint{topic.shm_name, 64, slots, slot_size, topic.manager.get()});

    session sess(local_client_options(server_name));
    const auto result = handshake_against_server(
        control_server, sess, handshake_request{protocol_version, "module-pub", "topic/pub", role::publisher});

    require(result.is_accepted(), "publisher client handshake should be accepted");
    require(result.accepted->mapping.shm_name == topic.shm_name, "publisher client should expose mapping");
    require(result.accepted->mapping.ring_offset == 64, "publisher client should expose ring offset");
    require(result.accepted->fds.publication.is_fd(), "publisher client should expose publication fd");
    require(result.accepted->fds.release.is_fd(), "publisher client should expose release fd");
    require(!result.accepted->fds.broadcast.is_fd(), "publisher client should not expose broadcast fd");
    require(!result.accepted->fds.ack.is_fd(), "publisher client should not expose ack fd");
}

void test_subscriber_client_handshake_and_liveness_lifetime() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    test_topic topic("/everest_shm_control_client_subscriber_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-sub-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "subscriber client test server should open");
    control_server.register_topic("topic/sub",
                                  server::topic_endpoint{topic.shm_name, 0, slots, slot_size, topic.manager.get()});

    {
        session sess(local_client_options(server_name));
        const auto result = handshake_against_server(
            control_server, sess, handshake_request{protocol_version, "module-sub", "topic/sub", role::subscriber});

        require(result.is_accepted(), "subscriber client handshake should be accepted");
        require(result.accepted->fds.broadcast.is_fd(), "subscriber client should expose broadcast fd");
        require(result.accepted->fds.ack.is_fd(), "subscriber client should expose ack fd");
        require(!result.accepted->fds.publication.is_fd(), "subscriber client should not expose publication fd");
        require(!result.accepted->fds.release.is_fd(), "subscriber client should not expose release fd");
        require(result.accepted->state == join_state::active, "subscriber client should expose join state");
        require(result.accepted->cursor.has_value(), "subscriber client should expose join cursor");
        require(control_server.cleanup_disconnected_clients().empty(),
                "open session should keep server-side subscriber alive");
        require(topic.rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
                "subscriber should be active while the session is open");
    }

    // Destroying the session closed the SEQPACKET connection. The server must observe EOF
    // and tear down the subscriber registration.
    for (int attempt = 0;
         attempt < 50 && topic.rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) != 0; ++attempt) {
        (void)control_server.cleanup_disconnected_clients();
        if (topic.rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    require(topic.rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "closing the session should remove the subscriber on the server side");
}

void test_publisher_subscriber_client_handshake() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    test_topic topic("/everest_shm_control_client_pubsub_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-pubsub-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "publisher-subscriber client test server should open");
    control_server.register_topic("topic/pubsub",
                                  server::topic_endpoint{topic.shm_name, 0, slots, slot_size, topic.manager.get()});

    session sess(local_client_options(server_name));
    const auto result = handshake_against_server(
        control_server, sess,
        handshake_request{protocol_version, "module-pubsub", "topic/pubsub", role::publisher_subscriber});

    require(result.is_accepted(), "publisher-subscriber client handshake should be accepted");
    require(result.accepted->fds.publication.is_fd(), "publisher-subscriber client should expose publication fd");
    require(result.accepted->fds.release.is_fd(), "publisher-subscriber client should expose release fd");
    require(result.accepted->fds.broadcast.is_fd(), "publisher-subscriber client should expose broadcast fd");
    require(result.accepted->fds.ack.is_fd(), "publisher-subscriber client should expose ack fd");
    require(result.accepted->state == join_state::active, "publisher-subscriber client should expose join state");
    require(result.accepted->cursor.has_value(), "publisher-subscriber client should expose join cursor");
}

void test_unknown_topic_rejection_preserves_server_error() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    test_topic topic("/everest_shm_control_client_unknown_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-unknown-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "unknown topic client test server should open");
    control_server.register_topic("topic/known",
                                  server::topic_endpoint{topic.shm_name, 0, slots, slot_size, topic.manager.get()});

    session sess(local_client_options(server_name));
    const auto result = handshake_against_server(
        control_server, sess, handshake_request{protocol_version, "module-unknown", "missing", role::subscriber});

    require(result.is_rejected(), "unknown topic client handshake should be a structured rejection");
    require(result.rejected->error == error_code::unknown_topic, "unknown topic rejection should preserve error code");
    require(result.rejected->message == "SHM topic is not registered",
            "unknown topic rejection should preserve server message");
}

void test_session_reconnect_after_server_closes_peer() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    test_topic topic("/everest_shm_control_client_reconnect_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-reconnect-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "reconnect client test server should open");
    control_server.register_topic("topic/r",
                                  server::topic_endpoint{topic.shm_name, 0, slots, slot_size, topic.manager.get()});

    session sess(local_client_options(server_name));
    const auto first = handshake_against_server(
        control_server, sess, handshake_request{protocol_version, "module-r", "topic/r", role::subscriber});
    require(first.is_accepted(), "first handshake should be accepted");

    // Server closes the session — equivalent to a manager restart or admin teardown.
    const auto active_fds = control_server.active_client_fds();
    require(active_fds.size() == 1U, "server should track one accepted client connection");
    require(control_server.close_client(active_fds[0]),
            "server should be able to close the accepted client connection by FD");
    require(topic.rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "server-side subscriber should be removed when the server closes the connection");

    // The session's fd is now half-closed; the next handshake must surface a socket error
    // (server-loss is the documented signal).
    const auto stranded = sess.handshake(handshake_request{protocol_version, "module-r", "topic/r", role::subscriber});
    require(stranded.is_error(), "handshake on a half-closed session should surface a client error");
    require(stranded.error->code == client_error_code::socket_error, "post-close handshake should report socket_error");

    // Caller reconnects with a fresh session — the server accepts it as a new client process.
    session reconnected(local_client_options(server_name));
    const auto retry = handshake_against_server(
        control_server, reconnected, handshake_request{protocol_version, "module-r", "topic/r", role::subscriber});
    require(retry.is_accepted(), "reconnected handshake should be accepted");
    require(topic.rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "reconnected session should register a fresh subscriber");
}

void test_topic_list_client_returns_registered_topics() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    test_topic topic_a("/everest_shm_control_client_list_a_test", slots, slot_size);
    test_topic topic_b("/everest_shm_control_client_list_b_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-list-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "topic list client test server should open");
    control_server.register_topic("everest/module_a/state",
                                  server::topic_endpoint{topic_a.shm_name, 0, slots, slot_size, topic_a.manager.get()});
    control_server.register_topic("everest/module_b/state",
                                  server::topic_endpoint{topic_b.shm_name, 0, slots, slot_size, topic_b.manager.get()});

    session sess(local_client_options(server_name));
    const auto result =
        list_topics_against_server(control_server, sess, list_topics_request{protocol_version, "observer"});
    require(result.is_accepted(), "topic list client should be accepted");
    require(result.accepted->topics.size() == 2U, "topic list client should receive two topics");
    require(result.accepted->topics[0] == "everest/module_a/state", "topic list client should receive sorted topics");
    require(result.accepted->topics[1] == "everest/module_b/state", "topic list client should receive sorted topics");
}

void test_topic_list_client_returns_empty_for_empty_registry() {
    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-list-empty-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "empty topic list client test server should open");

    session sess(local_client_options(server_name));
    const auto result =
        list_topics_against_server(control_server, sess, list_topics_request{protocol_version, "observer"});
    require(result.is_accepted(), "empty topic list client should be accepted");
    require(result.accepted->topics.empty(), "empty topic list client should receive no topics");
}

void test_subscriber_handshake_exposes_coordinator_subscriber_id() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    test_topic topic("/everest_shm_control_client_subscriber_id_test", slots, slot_size);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-sub-id-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "subscriber id client test server should open");
    control_server.register_topic("topic/sub-id",
                                  server::topic_endpoint{topic.shm_name, 0, slots, slot_size, topic.manager.get()});

    session first_session(local_client_options(server_name));
    const auto first = handshake_against_server(
        control_server, first_session,
        handshake_request{protocol_version, "module-sub-id-1", "topic/sub-id", role::subscriber});
    require(first.is_accepted(), "first subscriber handshake should be accepted");
    require(first.accepted->subscriber_id.has_value(),
            "subscriber handshake should expose subscriber id to the client");
    require(first.accepted->subscriber_id.value() == 0U, "first subscriber should receive coordinator subscriber id 0");

    session second_session(local_client_options(server_name));
    const auto second = handshake_against_server(
        control_server, second_session,
        handshake_request{protocol_version, "module-sub-id-2", "topic/sub-id", role::subscriber});
    require(second.is_accepted(), "second subscriber handshake should be accepted");
    require(second.accepted->subscriber_id == 1U, "second subscriber should receive coordinator subscriber id 1");

    session publisher_session(local_client_options(server_name));
    const auto publisher_only = handshake_against_server(
        control_server, publisher_session,
        handshake_request{protocol_version, "module-pub-only", "topic/sub-id", role::publisher});
    require(publisher_only.is_accepted(), "publisher handshake should be accepted");
    require(!publisher_only.accepted->subscriber_id.has_value(),
            "publisher handshake should not carry a subscriber id");
}

void test_topic_list_client_reports_socket_error() {
    const auto missing_server_name = "/tmp/everest-shm-control-client-list-missing-" + std::to_string(::getpid());
    ::unlink(missing_server_name.c_str());
    const auto result = request_topic_list(client_options{missing_server_name, false}, "observer");
    require(result.is_error(), "topic list against missing server should report client error");
    require(result.error->code == client_error_code::socket_error,
            "topic list against missing server should report socket error");
}

} // namespace

int main() {
    test_publisher_client_handshake();
    test_subscriber_client_handshake_and_liveness_lifetime();
    test_publisher_subscriber_client_handshake();
    test_unknown_topic_rejection_preserves_server_error();
    test_session_reconnect_after_server_closes_peer();
    test_topic_list_client_returns_registered_topics();
    test_topic_list_client_returns_empty_for_empty_registry();
    test_topic_list_client_reports_socket_error();
    test_subscriber_handshake_exposes_coordinator_subscriber_id();

    return 0;
}
