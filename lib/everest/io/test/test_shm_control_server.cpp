// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_client.hpp>
#include <everest/io/shm/control_server.hpp>
#include <everest/io/shm/coordinator.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/structures.hpp>
#include <everest/io/shm/topic.hpp>
#include <everest/io/socket/socket.hpp>

using everest::lib::io::event::unique_fd;
using everest::lib::io::shm::coordinator;
using everest::lib::io::shm::ring_buffer;
using everest::lib::io::shm::shared_memory;
using everest::lib::io::shm::topic;
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

// Drive the server's `handle_next_message` until the caller's future completes. The control
// server accepts connections lazily — until we poll the listener no client request can be
// observed — so tests sit in a tight loop until the response arrives.
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

client_handshake_result perform_handshake(server& control_server, session& sess, const handshake_request& request) {
    auto future = std::async(std::launch::async, [&sess, request]() { return sess.handshake(request); });
    drive_until_ready(control_server, future);
    return future.get();
}

topic_list_result perform_list_topics(server& control_server, session& sess, const list_topics_request& request) {
    auto future = std::async(std::launch::async, [&sess, request]() { return sess.list_topics(request); });
    drive_until_ready(control_server, future);
    return future.get();
}

std::size_t count_open_fds() {
    auto* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) {
        return 0;
    }
    std::size_t count = 0;
    while (auto* entry = ::readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        ++count;
    }
    ::closedir(dir);
    return count;
}

void test_subscriber_handshake_returns_broadcast_and_ack_fds() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    const std::string shm_name = "/everest_shm_control_server_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-sub-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open");
    control_server.register_topic("topic/a", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    require(sess.is_open(), "client session should open");
    const auto result = perform_handshake(control_server, sess,
                                          handshake_request{protocol_version, "module-a", "topic/a", role::subscriber});

    require(result.is_accepted(), "subscriber handshake should be accepted");
    const auto& accepted = result.accepted.value();
    require(accepted.mapping.shm_name == shm_name, "subscriber handshake should include shm name");
    require(accepted.state == join_state::active, "subscriber handshake should report active join state");
    require(accepted.cursor.has_value(), "subscriber handshake should include join cursor");
    require(accepted.cursor->write_idx == 0, "initial subscriber cursor should start at write index zero");
    require(accepted.cursor->sequence == 1, "initial subscriber cursor should start at next sequence one");
    require(accepted.response.fds.has_value(), "subscriber handshake should include fd bundle");
    require(!accepted.response.fds->publication.has_value(), "subscriber handshake should omit publication fd");
    require(!accepted.response.fds->release.has_value(), "subscriber handshake should omit release fd");
    require(accepted.fds.broadcast.is_fd(), "subscriber broadcast fd should be valid");
    require(accepted.fds.ack.is_fd(), "subscriber ack fd should be valid");

    const auto subscribers = control_server.subscribers_for_topic("topic/a");
    require(subscribers.size() == 1U, "subscriber handshake should create one visible subscriber");
    require(subscribers[0].topic == "topic/a", "visible subscriber should include topic");
    require(subscribers[0].client_id == "module-a", "visible subscriber should include client id");
    require(subscribers[0].subscriber == 0U, "visible subscriber should include coordinator subscriber id");
    require(subscribers[0].state == join_state::active, "visible subscriber should include join state");
    const auto active_fds = control_server.active_client_fds();
    require(active_fds.size() == 1U, "subscriber session should create one server-side connection fd");
    require(subscribers[0].connection_fd == active_fds[0],
            "visible subscriber should reference the server-side persistent connection fd");
    require(subscribers[0].session_id != 0U, "visible subscriber should expose a stable session id");
    require(subscribers[0].topic_coordinator == &manager, "visible subscriber should include topic coordinator owner");
}

void test_publisher_handshake_returns_publication_and_release_fds() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    const std::string shm_name = "/everest_shm_control_server_publisher_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-pub-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open for publisher");
    control_server.register_topic("topic/b", server::topic_endpoint{shm_name, 4096, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    const auto result = perform_handshake(control_server, sess,
                                          handshake_request{protocol_version, "module-b", "topic/b", role::publisher});

    require(result.is_accepted(), "publisher handshake should be accepted");
    const auto& accepted = result.accepted.value();
    require(accepted.mapping.ring_offset == 4096, "publisher handshake should include ring offset");
    require(accepted.fds.publication.is_fd(), "publisher publication fd should be valid");
    require(accepted.fds.release.is_fd(), "publisher release fd should be valid");
    require(!accepted.fds.broadcast.is_fd(), "publisher handshake should omit broadcast fd");
    require(!accepted.fds.ack.is_fd(), "publisher handshake should omit ack fd");
    require(control_server.subscribers_for_topic("topic/b").empty(),
            "publisher-only handshakes should not create visible subscribers");
}

void test_publisher_subscriber_handshake_returns_all_fds() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    const std::string shm_name = "/everest_shm_control_server_pubsub_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-pubsub-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open for publisher-subscriber");
    control_server.register_topic("topic/pubsub", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    const auto result = perform_handshake(
        control_server, sess,
        handshake_request{protocol_version, "module-pubsub", "topic/pubsub", role::publisher_subscriber});

    require(result.is_accepted(), "publisher-subscriber handshake should be accepted");
    const auto& accepted = result.accepted.value();
    require(accepted.fds.publication.is_fd(), "publisher-subscriber publication fd should be valid");
    require(accepted.fds.release.is_fd(), "publisher-subscriber release fd should be valid");
    require(accepted.fds.broadcast.is_fd(), "publisher-subscriber broadcast fd should be valid");
    require(accepted.fds.ack.is_fd(), "publisher-subscriber ack fd should be valid");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "publisher-subscriber should register one subscriber");

    const auto subscribers = control_server.subscribers_for_topic("topic/pubsub");
    require(subscribers.size() == 1U, "publisher-subscriber should create one visible subscriber");
    require(subscribers[0].client_id == "module-pubsub", "publisher-subscriber visible entry should keep client id");
    require(subscribers[0].subscriber == 0U, "publisher-subscriber visible entry should keep subscriber id");
}

void test_dynamic_subscriber_handshake_returns_pending_cursor() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_control_server_pending_cursor_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto first_subscriber_id = manager.add_subscriber().id;
    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "pending cursor test first publish should succeed");
    require(manager.handle_publication() == 1, "pending cursor test first publish should dispatch");

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-pending-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open for pending cursor test");
    control_server.register_topic("topic/pending", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    const auto result = perform_handshake(
        control_server, sess, handshake_request{protocol_version, "module-pending", "topic/pending", role::subscriber});

    require(result.is_accepted(), "pending subscriber handshake should be accepted");
    const auto& accepted = result.accepted.value();
    require(accepted.state == join_state::pending, "dynamic subscriber should report pending join state");
    const auto pending_subscribers = control_server.subscribers_for_topic("topic/pending");
    require(pending_subscribers.size() == 1U, "pending subscriber should create one visible subscriber");
    require(pending_subscribers[0].state == join_state::pending,
            "visible pending subscriber should keep known join state");
    require(accepted.cursor.has_value(), "dynamic subscriber should include join cursor");
    require(accepted.cursor->write_idx == 1, "dynamic subscriber cursor should start at current write index");
    require(accepted.cursor->sequence == 2, "dynamic subscriber cursor should start at next sequence");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == subscribers,
            "pending subscriber should not change active target before old slot is released");

    std::vector<std::string> received;
    auto first_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(first_subscriber_id),
        manager.make_ack_fd(first_subscriber_id), [&](const std::string& data) { received.push_back(data); },
        static_cast<std::uint32_t>(first_subscriber_id));
    first_subscriber->handle_event();
    require(manager.handle_ack(first_subscriber_id), "first subscriber ACK should activate pending subscriber");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 2,
            "pending subscriber should become active after old slot release");
    const auto active_subscribers = control_server.subscribers_for_topic("topic/pending");
    require(active_subscribers.size() == 1U, "activated pending subscriber should remain visible");
    require(active_subscribers[0].state == join_state::active,
            "visible subscriber snapshot should reflect coordinator activation");
}

void test_disconnect_releases_pending_publisher_slot() {
    // The core T-4.009 acceptance criterion: when a client process disconnects from the
    // persistent control connection, every subscriber registered through that session is
    // removed and any publisher that was blocked on those subscribers' acks is released.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    const std::string shm_name = "/everest_shm_control_server_disconnect_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-disconnect-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open for disconnect test");
    control_server.register_topic("topic/disconnect", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    {
        session sess(local_client_options(server_name));
        require(sess.is_open(), "session should open");
        const auto result = perform_handshake(
            control_server, sess,
            handshake_request{protocol_version, "module-disconnect", "topic/disconnect", role::subscriber});
        require(result.is_accepted(), "disconnect-test subscriber handshake should be accepted");
        require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
                "subscriber should be active after handshake");

        auto publisher =
            topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
        require(publisher->publish("{\"value\":42}", topic::full_buffer_policy::fail_immediately),
                "publisher should be able to publish into the only slot");
        require(manager.handle_publication() == 1, "publication should dispatch");
        require(rb.get_slot_header(0)->ack_count.load(std::memory_order_acquire) == 0,
                "publication should be waiting for subscriber ack");
        require(rb.get_slot_header(0)->target_subscribers.load(std::memory_order_acquire) == 1,
                "publication should target the one subscriber");
    }

    // Session destructor closed the SEQPACKET connection. The server detects EOF on its
    // sweep and releases the subscriber slot — which in turn unblocks any publisher that
    // would otherwise wait on its ack.
    std::vector<int> removed;
    for (int attempt = 0; attempt < 50 && removed.empty(); ++attempt) {
        removed = control_server.cleanup_disconnected_clients();
        if (removed.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    require(removed.size() == 1U, "client disconnect should remove exactly one session FD");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "subscriber should be removed from active targets");
    require(rb.get_slot_header(0)->target_subscribers.load(std::memory_order_acquire) == 0,
            "pending slot target should be cleared so the publisher can reuse it");
    require(control_server.subscribers_for_topic("topic/disconnect").empty(),
            "all visible subscriber state for the closed session should be released");
}

void test_one_session_owns_multiple_topic_registrations() {
    // T-4.009 acceptance: one persistent client connection can carry many handshakes for
    // distinct topics, and disconnecting it cleans up every registration in one shot.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_a_name = "/everest_shm_control_server_multi_topic_a_test";
    const std::string shm_b_name = "/everest_shm_control_server_multi_topic_b_test";

    shared_memory shm_a(shm_a_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shared_memory shm_b(shm_b_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm_a.unlink();
    shm_b.unlink();
    ring_buffer rb_a(shm_a.get_ptr());
    ring_buffer rb_b(shm_b.get_ptr());
    coordinator::initialize_ring_buffer(rb_a, slots, slot_size, 0);
    coordinator::initialize_ring_buffer(rb_b, slots, slot_size, 0);
    coordinator manager_a(rb_a);
    coordinator manager_b(rb_b);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-multi-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "multi-topic server should open");
    control_server.register_topic("topic/a", server::topic_endpoint{shm_a_name, 0, slots, slot_size, &manager_a});
    control_server.register_topic("topic/b", server::topic_endpoint{shm_b_name, 0, slots, slot_size, &manager_b});

    {
        session sess(local_client_options(server_name));
        const auto first = perform_handshake(
            control_server, sess, handshake_request{protocol_version, "module-multi", "topic/a", role::subscriber});
        require(first.is_accepted(), "first multi-topic handshake should be accepted");
        const auto second = perform_handshake(
            control_server, sess, handshake_request{protocol_version, "module-multi", "topic/b", role::subscriber});
        require(second.is_accepted(), "second multi-topic handshake should be accepted");

        const auto topic_a = control_server.subscribers_for_topic("topic/a");
        const auto topic_b = control_server.subscribers_for_topic("topic/b");
        require(topic_a.size() == 1U && topic_b.size() == 1U,
                "multi-topic session should produce one subscriber per topic");
        const auto fds = control_server.active_client_fds();
        require(fds.size() == 1U, "one client process should map to exactly one server-side connection FD");
        require(topic_a[0].connection_fd == fds[0] && topic_b[0].connection_fd == fds[0],
                "both subscribers should share the same server-side connection fd");
        require(topic_a[0].session_id == topic_b[0].session_id, "both subscribers should share the same session id");
    }

    std::vector<int> removed;
    for (int attempt = 0; attempt < 50 && removed.empty(); ++attempt) {
        removed = control_server.cleanup_disconnected_clients();
        if (removed.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    require(removed.size() == 1U, "closing the session should remove exactly one server connection FD");
    require(control_server.subscribers_for_topic("topic/a").empty() &&
                control_server.subscribers_for_topic("topic/b").empty(),
            "every registration owned by the closed session should be torn down");
    require(rb_a.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0 &&
                rb_b.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "all coordinator subscriber slots should be freed after session disconnect");
}

void test_session_reconnect_drops_previous_registrations() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name = "/everest_shm_control_server_reconnect_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-reconnect-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "reconnect server should open");
    control_server.register_topic("topic/reconnect", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    auto first_session = std::make_unique<session>(local_client_options(server_name));
    const auto first =
        perform_handshake(control_server, *first_session,
                          handshake_request{protocol_version, "module-reconnect", "topic/reconnect", role::subscriber});
    require(first.is_accepted(), "first reconnect handshake should be accepted");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "first session should leave one subscriber");

    first_session.reset();
    // Sweep until the disconnect is observed.
    for (int attempt = 0; attempt < 50 && rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) != 0;
         ++attempt) {
        (void)control_server.cleanup_disconnected_clients();
        if (rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "first session's subscriber should be released on disconnect");
    require(control_server.subscribers_for_topic("topic/reconnect").empty(),
            "first session's visible subscriber should be cleared");

    // Reconnect with a fresh session — the second session must be accepted and tracked
    // independently of the (now-gone) first session.
    session second_session(local_client_options(server_name));
    const auto second =
        perform_handshake(control_server, second_session,
                          handshake_request{protocol_version, "module-reconnect", "topic/reconnect", role::subscriber});
    require(second.is_accepted(), "reconnected session handshake should be accepted");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "reconnected session should register a fresh subscriber");
    const auto subscribers = control_server.subscribers_for_topic("topic/reconnect");
    require(subscribers.size() == 1U, "reconnected session should have one visible subscriber");
    const auto active_fds = control_server.active_client_fds();
    require(active_fds.size() == 1U, "reconnected session should create one server-side connection fd");
    require(subscribers[0].connection_fd == active_fds[0],
            "visible subscriber connection FD should match the new server-side session");
}

void test_version_mismatch_is_rejected_before_registration() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name = "/everest_shm_control_server_version_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-version-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "version-mismatch server should open");
    control_server.register_topic("topic/v", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    handshake_request stale;
    stale.version = protocol_version - 1U;
    stale.client_id = "stale-module";
    stale.topic = "topic/v";
    stale.topic_role = role::subscriber;
    const auto result = perform_handshake(control_server, sess, stale);
    require(result.is_rejected(), "stale-version handshake should be rejected, not crash the server");
    require(result.rejected->error == error_code::incompatible_version,
            "stale-version rejection should carry incompatible_version");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "version-rejected handshake should not allocate a subscriber slot");
    require(control_server.subscribers_for_topic("topic/v").empty(),
            "version-rejected handshake should not leave a visible subscriber");

    list_topics_request stale_list;
    stale_list.version = protocol_version - 1U;
    stale_list.client_id = "stale-module";
    const auto list_result = perform_list_topics(control_server, sess, stale_list);
    require(list_result.is_rejected(), "list_topics with stale version should be rejected");
    require(list_result.rejected->error == error_code::incompatible_version,
            "list_topics rejection should carry incompatible_version");
}

void test_unknown_topic_returns_error_without_fds() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    const std::string shm_name = "/everest_shm_control_server_error_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-error-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open for error test");
    control_server.register_topic("topic/c", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    const auto result = perform_handshake(
        control_server, sess, handshake_request{protocol_version, "module-c", "missing-topic", role::subscriber});

    require(result.is_rejected(), "unknown topic handshake should be rejected");
    require(result.rejected->error == error_code::unknown_topic, "unknown topic error should roundtrip");
    require(control_server.subscribers_for_topic("topic/c").empty(),
            "unknown topic handshake should not create visible subscribers for registered topics");
    require(control_server.subscribers_for_topic("missing-topic").empty(),
            "unknown topic handshake should not create visible subscribers for missing topics");
}

void test_list_topics_response_lists_registered_topics() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name_a = "/everest_shm_control_server_list_a_test";
    const std::string shm_name_b = "/everest_shm_control_server_list_b_test";

    shared_memory shm_a(shm_name_a, ring_buffer::calculate_required_size(slots, slot_size), true);
    shared_memory shm_b(shm_name_b, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm_a.unlink();
    shm_b.unlink();
    ring_buffer rb_a(shm_a.get_ptr());
    ring_buffer rb_b(shm_b.get_ptr());
    coordinator::initialize_ring_buffer(rb_a, slots, slot_size, 0);
    coordinator::initialize_ring_buffer(rb_b, slots, slot_size, 0);
    coordinator manager_a(rb_a);
    coordinator manager_b(rb_b);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-list-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "list_topics test server should open");
    control_server.register_topic("topic/zeta", server::topic_endpoint{shm_name_a, 0, slots, slot_size, &manager_a});
    control_server.register_topic("topic/alpha", server::topic_endpoint{shm_name_b, 0, slots, slot_size, &manager_b});

    session sess(local_client_options(server_name));
    const auto result =
        perform_list_topics(control_server, sess, list_topics_request{protocol_version, "module-listener"});
    require(result.is_accepted(), "list_topics response should be accepted");
    require(result.accepted->topics.size() == 2U, "list_topics response should contain both topics");
    require(result.accepted->topics[0] == "topic/alpha", "list_topics response should be sorted");
    require(result.accepted->topics[1] == "topic/zeta", "list_topics response should be sorted");
}

void test_subscriber_handshake_returns_topic_ack_fd_shared_across_subscribers() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name = "/everest_shm_control_server_shared_ack_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-shared-ack-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "shared ACK test server should open");
    control_server.register_topic("topic/ack", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session first_session(local_client_options(server_name));
    const auto first =
        perform_handshake(control_server, first_session,
                          handshake_request{protocol_version, "module-shared-ack-1", "topic/ack", role::subscriber});
    require(first.is_accepted(), "first shared ACK subscriber handshake should be accepted");
    require(first.accepted->subscriber_id == 0U, "first subscriber should receive coordinator subscriber id 0");

    session second_session(local_client_options(server_name));
    const auto second =
        perform_handshake(control_server, second_session,
                          handshake_request{protocol_version, "module-shared-ack-2", "topic/ack", role::subscriber});
    require(second.is_accepted(), "second shared ACK subscriber handshake should be accepted");
    require(second.accepted->subscriber_id == 1U, "second subscriber should receive coordinator subscriber id 1");

    const auto first_ack = static_cast<int>(first.accepted->fds.ack);
    const auto second_ack = static_cast<int>(second.accepted->fds.ack);
    require(first_ack != second_ack, "duplicates of the same eventfd should have distinct FD numbers");

    require(::eventfd_write(first_ack, 1) == 0, "writing to first ACK dup should succeed");
    require(::eventfd_write(second_ack, 1) == 0, "writing to second ACK dup should succeed");
    eventfd_t aggregated{};
    require(::eventfd_read(manager.topic_ack_event_fd()->get_raw_fd(), &aggregated) == 0,
            "shared topic ACK eventfd should be readable after both subscribers write");
    require(aggregated == 2, "shared topic ACK eventfd must aggregate writes from every subscriber duplicate");
}

void test_subscriber_handshake_returns_client_owned_broadcast_fds() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name = "/everest_shm_control_server_client_broadcast_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-broadcast-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "client broadcast test server should open");
    control_server.register_topic("topic/broadcast", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session first_session(local_client_options(server_name));
    const auto first = perform_handshake(
        control_server, first_session,
        handshake_request{protocol_version, "module-client-broadcast-1", "topic/broadcast", role::subscriber});
    require(first.is_accepted(), "first client broadcast subscriber handshake should be accepted");

    session second_session(local_client_options(server_name));
    const auto second = perform_handshake(
        control_server, second_session,
        handshake_request{protocol_version, "module-client-broadcast-2", "topic/broadcast", role::subscriber});
    require(second.is_accepted(), "second client broadcast subscriber handshake should be accepted");

    const auto first_b = static_cast<int>(first.accepted->fds.broadcast);
    const auto second_b = static_cast<int>(second.accepted->fds.broadcast);
    require(first_b != second_b, "different client sessions should receive different broadcast FD numbers");

    require(control_server.wake_subscriber_clients("topic/broadcast", {first.accepted->subscriber_id.value(),
                                                                       second.accepted->subscriber_id.value()}) == 2U,
            "control server should wake both affected client sessions");
    eventfd_t observed_first{};
    eventfd_t observed_second{};
    require(::eventfd_read(first_b, &observed_first) == 0,
            "first client session should read its broadcast wake credit");
    require(::eventfd_read(second_b, &observed_second) == 0,
            "second client session should read its broadcast wake credit");
    require(observed_first == 1 && observed_second == 1, "each client wake should receive one credit");
}

void test_one_client_reuses_broadcast_fd_across_topics() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name_a = "/everest_shm_control_server_client_broadcast_a";
    const std::string shm_name_b = "/everest_shm_control_server_client_broadcast_b";

    shared_memory shm_a(shm_name_a, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm_a.unlink();
    ring_buffer rb_a(shm_a.get_ptr());
    coordinator::initialize_ring_buffer(rb_a, slots, slot_size, 0);
    coordinator manager_a(rb_a);

    shared_memory shm_b(shm_name_b, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm_b.unlink();
    ring_buffer rb_b(shm_b.get_ptr());
    coordinator::initialize_ring_buffer(rb_b, slots, slot_size, 0);
    coordinator manager_b(rb_b);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-client-multitopic-broadcast-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "client multi-topic broadcast test server should open");
    control_server.register_topic("topic/broadcast/a",
                                  server::topic_endpoint{shm_name_a, 0, slots, slot_size, &manager_a});
    control_server.register_topic("topic/broadcast/b",
                                  server::topic_endpoint{shm_name_b, 0, slots, slot_size, &manager_b});

    session client_session(local_client_options(server_name));
    const auto first = perform_handshake(
        control_server, client_session,
        handshake_request{protocol_version, "module-client-broadcast", "topic/broadcast/a", role::subscriber});
    require(first.is_accepted(), "first topic subscriber handshake should be accepted");
    const auto second = perform_handshake(
        control_server, client_session,
        handshake_request{protocol_version, "module-client-broadcast", "topic/broadcast/b", role::subscriber});
    require(second.is_accepted(), "second topic subscriber handshake should be accepted");

    const auto first_b = static_cast<int>(first.accepted->fds.broadcast);
    const auto second_b = static_cast<int>(second.accepted->fds.broadcast);
    require(first_b != second_b, "one client receives separate duplicate FD numbers per topic handshake");

    require(control_server.wake_subscriber_clients("topic/broadcast/a", {first.accepted->subscriber_id.value()}) == 1U,
            "first topic dispatch should wake the client session");
    require(control_server.wake_subscriber_clients("topic/broadcast/b", {second.accepted->subscriber_id.value()}) == 1U,
            "second topic dispatch should wake the same client session");

    eventfd_t observed{};
    require(::eventfd_read(first_b, &observed) == 0, "client wake should aggregate both topic wake credits");
    require(observed == 2, "one client-owned broadcast wake should aggregate credits across its topic subscriptions");
    require(::eventfd_read(second_b, &observed) == -1 && errno == EAGAIN,
            "duplicate client wake FD should be nonblocking after another duplicate drains the shared credits");
}

void test_subscriber_handshake_rejects_cleanly_when_cap_exhausted() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name = "/everest_shm_control_server_cap_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-cap-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "subscriber cap test server should open");
    control_server.register_topic("topic/cap", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    std::vector<std::unique_ptr<session>> session_keepalive;
    session_keepalive.reserve(everest::lib::io::shm::shm_max_subscribers_per_topic);
    for (std::uint32_t i = 0; i < everest::lib::io::shm::shm_max_subscribers_per_topic; ++i) {
        auto sess = std::make_unique<session>(local_client_options(server_name));
        const auto accepted = perform_handshake(
            control_server, *sess,
            handshake_request{protocol_version, "module-cap-" + std::to_string(i), "topic/cap", role::subscriber});
        require(accepted.is_accepted(), "subscriber under the cap should be accepted");
        session_keepalive.push_back(std::move(sess));
    }

    session overflow_session(local_client_options(server_name));
    const auto rejected =
        perform_handshake(control_server, overflow_session,
                          handshake_request{protocol_version, "module-cap-overflow", "topic/cap", role::subscriber});

    require(rejected.is_rejected(), "subscriber past the cap must be rejected");
    require(rejected.rejected->error == error_code::resource_error, "cap exhaustion must surface as resource_error");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) ==
                everest::lib::io::shm::shm_max_subscribers_per_topic,
            "rejected handshake must not change the active subscriber count");
    require(control_server.subscribers_for_topic("topic/cap").size() ==
                everest::lib::io::shm::shm_max_subscribers_per_topic,
            "rejected handshake must not register a visible subscriber");
}

void test_fd_count_scales_with_clients_not_handshakes() {
    // The motivating measurement for T-4.009: a single client process that registers many
    // topic handshakes adds *one* server-side liveness descriptor (the accepted connection
    // socket), not one per handshake. We compare the server-side FD count across an
    // increasing number of handshakes on the same persistent session and against the FD
    // delta produced by adding a *second* client process.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    const std::string shm_name = "/everest_shm_control_server_fd_count_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-fdcount-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "fd-count server should open");

    constexpr std::uint32_t topic_count = 16U;
    for (std::uint32_t i = 0; i < topic_count; ++i) {
        const auto topic_name = "topic/fdcount/" + std::to_string(i);
        control_server.register_topic(topic_name, server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});
    }

    const auto baseline_fds = count_open_fds();

    session sess_a(local_client_options(server_name));
    const auto after_connect_fds = count_open_fds();

    // Up to T-4.008 each handshake created its own subscriber liveness FD pair (server +
    // client side). The T-4.009 SEQPACKET design ships every handshake on the existing
    // accepted connection, so subsequent handshakes from the same client should NOT add a
    // new liveness descriptor on the server side.
    for (std::uint32_t i = 0; i < topic_count; ++i) {
        const auto topic_name = "topic/fdcount/" + std::to_string(i);
        const auto result =
            perform_handshake(control_server, sess_a,
                              handshake_request{protocol_version, "module-fdcount-a", topic_name, role::publisher});
        require(result.is_accepted(), "fd-count handshakes should be accepted");
    }
    const auto after_many_handshakes_fds = count_open_fds();

    // The handshake delta must not scale linearly with the number of handshakes — adding 16
    // handshakes should add far fewer than 16 server-side liveness FDs (the per-topic
    // publication+release eventfds are owned by the coordinator and shared across clients
    // when the topic is shared; the test budgets for incidental probes by comparing against
    // the per-handshake liveness scaling the pre-T-4.009 design exhibited).
    const auto handshake_delta =
        static_cast<long long>(after_many_handshakes_fds) - static_cast<long long>(after_connect_fds);
    require(handshake_delta < static_cast<long long>(topic_count),
            "handshakes on a persistent session must not add one liveness FD per handshake");

    // Adding a second client process produces one more accepted-connection FD on the server.
    session sess_b(local_client_options(server_name));
    const auto list_result =
        perform_list_topics(control_server, sess_b, list_topics_request{protocol_version, "module-fdcount-b"});
    require(list_result.is_accepted(), "second fd-count session should be accepted by the server");
    const auto two_clients_fds = count_open_fds();
    const auto session_delta =
        static_cast<long long>(two_clients_fds) - static_cast<long long>(after_many_handshakes_fds);
    require(session_delta >= 2 && session_delta <= 4,
            "adding a second client should add roughly one server-side accepted FD plus the client-side socket");
    (void)baseline_fds;

    require(control_server.active_client_fds().size() == 2U,
            "two distinct sessions should map to exactly two server-side connection FDs");
}

void test_close_clears_visible_subscriber_state() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    const std::string shm_name = "/everest_shm_control_server_close_snapshot_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();
    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, 0);
    coordinator manager(rb);

    server control_server;
    const auto server_name = "/tmp/everest-shm-control-close-snapshot-" + std::to_string(::getpid());
    require(control_server.open(server_name, false), "control server should open for close snapshot test");
    control_server.register_topic("topic/close", server::topic_endpoint{shm_name, 0, slots, slot_size, &manager});

    session sess(local_client_options(server_name));
    const auto result = perform_handshake(
        control_server, sess, handshake_request{protocol_version, "module-close", "topic/close", role::subscriber});
    require(result.is_accepted(), "close snapshot handshake should be accepted");
    require(control_server.subscribers_for_topic("topic/close").size() == 1U,
            "close snapshot should start with one visible subscriber");

    control_server.close();
    require(control_server.subscribers_for_topic("topic/close").empty(), "close should clear visible subscriber state");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 0,
            "close should remove coordinator subscriber");
}

} // namespace

int main() {
    test_subscriber_handshake_returns_broadcast_and_ack_fds();
    test_publisher_handshake_returns_publication_and_release_fds();
    test_publisher_subscriber_handshake_returns_all_fds();
    test_dynamic_subscriber_handshake_returns_pending_cursor();
    test_disconnect_releases_pending_publisher_slot();
    test_one_session_owns_multiple_topic_registrations();
    test_session_reconnect_drops_previous_registrations();
    test_version_mismatch_is_rejected_before_registration();
    test_unknown_topic_returns_error_without_fds();
    test_list_topics_response_lists_registered_topics();
    test_subscriber_handshake_returns_topic_ack_fd_shared_across_subscribers();
    test_subscriber_handshake_returns_client_owned_broadcast_fds();
    test_one_client_reuses_broadcast_fd_across_topics();
    test_subscriber_handshake_rejects_cleanly_when_cap_exhausted();
    test_fd_count_scales_with_clients_not_handshakes();
    test_close_clears_visible_subscriber_state();

    return 0;
}
