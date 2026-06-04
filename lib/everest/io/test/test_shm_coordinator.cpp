// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/coordinator.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/topic.hpp>

namespace event = everest::lib::io::event;
using everest::lib::io::shm::coordinator;
using everest::lib::io::shm::ring_buffer;
using everest::lib::io::shm::shared_memory;
using everest::lib::io::shm::topic;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

template <typename Func> void require_throws(Func&& func, const char* message) {
    try {
        func();
    } catch (const std::exception&) {
        return;
    }

    std::cerr << "FAILED: " << message << "\n";
    std::exit(1);
}

void require_child_success(pid_t pid, const char* message) {
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {
        std::cerr << "FAILED: " << message << " (waitpid failed)\n";
        std::exit(1);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "FAILED: " << message << " (child status " << status << ")\n";
        std::exit(1);
    }
}

template <typename Func> pid_t fork_child(Func&& func) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "FAILED: fork failed\n";
        std::exit(1);
    }
    if (pid == 0) {
        try {
            func();
        } catch (const std::exception& e) {
            std::cerr << "FAILED: child threw exception: " << e.what() << "\n";
            std::exit(1);
        }
        std::exit(0);
    }

    return pid;
}

void test_back_pressure_release_flow() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_coordinator_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;

    std::vector<std::string> received_a;
    std::vector<std::string> received_b;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto sub_a = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_a_id), manager.make_ack_fd(sub_a_id),
        [&](const std::string& data) { received_a.push_back(data); }, static_cast<std::uint32_t>(sub_a_id));
    auto sub_b = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_b_id), manager.make_ack_fd(sub_b_id),
        [&](const std::string& data) { received_b.push_back(data); }, static_cast<std::uint32_t>(sub_b_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "first publish should succeed");
    require(!publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "second publish should hit back-pressure");

    require(manager.handle_publication() == 1, "manager should dispatch first slot");
    sub_a->handle_event();
    require(!manager.handle_ack(sub_a_id), "first ACK should not release slot");
    require(!publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "slot should remain blocked until all ACKs arrive");

    sub_b->handle_event();
    require(manager.handle_ack(sub_b_id), "second ACK should release slot");
    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "publish after release should succeed");

    require(manager.handle_publication() == 1, "manager should dispatch second slot");
    sub_a->handle_event();
    sub_b->handle_event();
    require(!manager.handle_ack(sub_a_id), "first ACK for second slot should not release slot");
    require(manager.handle_ack(sub_b_id), "second ACK for second slot should release slot");

    require((received_a == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "subscriber A should receive both messages");
    require((received_b == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "subscriber B should receive both messages");
}

void test_observability_counters_track_publish_dispatch_ack_and_release() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_observability_success_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "observability publish should succeed");
    require(manager.handle_publication() == 1, "observability publish should dispatch");
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "observability ACK should release slot");

    const auto publisher_counters = publisher->counter_snapshot();
    require(publisher_counters.messages_published == 1, "publisher should count successful publish");
    require(publisher_counters.failed_publish_attempts == 0, "publisher should not count failed publish");

    const auto subscriber_counters = subscriber->counter_snapshot();
    require(subscriber_counters.messages_dispatched == 1, "subscriber should count delivered dispatch");
    require(subscriber_counters.subscriber_acks_observed == 1, "subscriber should count ACK writes");

    const auto coordinator_counters = manager.counter_snapshot();
    require(coordinator_counters.messages_published == 1, "coordinator should count publication notifications");
    require(coordinator_counters.messages_dispatched == 1, "coordinator should count subscriber dispatches");
    require(coordinator_counters.subscriber_acks_observed == 1, "coordinator should count observed ACKs");
    require(coordinator_counters.slots_released == 1, "coordinator should count released slots");
    require(coordinator_counters.subscriber_joins == 1, "coordinator should count subscriber joins");
    require(coordinator_counters.subscriber_removals == 0, "coordinator should not count removals");
    require((received == std::vector<std::string>{"{\"value\":1}"}), "subscriber should receive observed message");

    publisher->reset_counters();
    subscriber->reset_counters();
    manager.reset_counters();
    require(publisher->counter_snapshot().messages_published == 0, "publisher reset should clear counters");
    require(subscriber->counter_snapshot().messages_dispatched == 0, "subscriber reset should clear counters");
    require(manager.counter_snapshot().messages_dispatched == 0, "coordinator reset should clear counters");
}

void test_observability_counters_track_full_buffer_failure_and_reuse() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_observability_full_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [](const std::string&) {}, static_cast<std::uint32_t>(sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "first observed publish should succeed");
    require(!publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "second observed publish should fail on full buffer");
    require(manager.handle_publication() == 1, "first observed publish should dispatch");
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "observed ACK should release full buffer");
    require(publisher->publish("{\"value\":3}", topic::full_buffer_policy::fail_immediately),
            "observed publish after release should reuse slot");

    const auto publisher_counters = publisher->counter_snapshot();
    require(publisher_counters.messages_published == 2, "publisher should count two successful publishes");
    require(publisher_counters.blocked_publish_attempts == 1, "publisher should count full-buffer attempt");
    require(publisher_counters.failed_publish_attempts == 1, "publisher should count failed full-buffer publish");
    require(publisher_counters.slots_reused == 1, "publisher should count slot reuse after release");

    require(manager.remove_subscriber(sub_id), "observability remove should succeed");
    const auto coordinator_counters = manager.counter_snapshot();
    require(coordinator_counters.subscriber_joins == 1, "coordinator should count join before removal");
    require(coordinator_counters.subscriber_removals == 1, "coordinator should count subscriber removal");
}

void test_slow_consumer_backpressure_snapshots_are_per_subscriber() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_slow_consumer_snapshot_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto fast_sub_id = manager.add_subscriber().id;
    const auto slow_sub_id = manager.add_subscriber().id;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto fast_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(fast_sub_id), manager.make_ack_fd(fast_sub_id),
        [](const std::string&) {}, static_cast<std::uint32_t>(fast_sub_id));
    auto slow_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(slow_sub_id), manager.make_ack_fd(slow_sub_id),
        [](const std::string&) {}, static_cast<std::uint32_t>(slow_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "slow consumer snapshot first publish should succeed");
    require(manager.handle_publication() == 1, "slow consumer snapshot first publish should dispatch");

    auto snapshots = manager.subscriber_backpressure_snapshots();
    require(snapshots.size() == 2U, "slow consumer snapshots should include both subscribers");
    auto snapshot_for = [&](coordinator::subscriber_id id) -> const coordinator::subscriber_backpressure_snapshot& {
        const auto found =
            std::find_if(snapshots.begin(), snapshots.end(), [id](const auto& snapshot) { return snapshot.id == id; });
        require(found != snapshots.end(), "subscriber snapshot should exist");
        return *found;
    };

    require(snapshot_for(fast_sub_id).outstanding_slots == 1, "fast subscriber should initially hold one slot");
    require(snapshot_for(slow_sub_id).outstanding_slots == 1, "slow subscriber should initially hold one slot");

    fast_subscriber->handle_event();
    require(!manager.handle_ack(fast_sub_id), "fast subscriber ACK alone should not release slot");
    require(!publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "slow consumer should keep the one-slot ring full");

    snapshots = manager.subscriber_backpressure_snapshots();
    require(snapshot_for(fast_sub_id).outstanding_slots == 0,
            "fast subscriber outstanding slots should clear after ACK");
    require(snapshot_for(fast_sub_id).acked_slots == 1, "fast subscriber ACK progress should be counted");
    require(snapshot_for(slow_sub_id).outstanding_slots == 1,
            "slow subscriber should remain visible as holding the slot");
    require(snapshot_for(slow_sub_id).acked_slots == 0, "slow subscriber should not show ACK progress yet");
    require(snapshot_for(slow_sub_id).max_observed_outstanding_slots == 1,
            "slow subscriber maximum outstanding slots should be tracked");
    require(publisher->counter_snapshot().blocked_publish_attempts == 1,
            "publisher should count full-buffer back-pressure caused by slow subscriber");

    slow_subscriber->handle_event();
    require(manager.handle_ack(slow_sub_id), "slow subscriber ACK should release slot");

    snapshots = manager.subscriber_backpressure_snapshots();
    require(snapshot_for(slow_sub_id).outstanding_slots == 0,
            "slow subscriber outstanding slots should clear after ACK");
    require(snapshot_for(slow_sub_id).acked_slots == 1, "slow subscriber ACK progress should be counted");
    require(snapshot_for(slow_sub_id).last_ack_progress_sequence > snapshot_for(fast_sub_id).last_ack_progress_sequence,
            "last ACK progress sequence should advance when the slow subscriber releases");
}

void test_blocking_publish_waits_for_slow_subscriber_ack() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_slow_subscriber_policy_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto fast_sub_id = manager.add_subscriber().id;
    const auto slow_sub_id = manager.add_subscriber().id;

    std::vector<std::string> fast_received;
    std::vector<std::string> slow_received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto fast_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(fast_sub_id), manager.make_ack_fd(fast_sub_id),
        [&](const std::string& data) { fast_received.push_back(data); }, static_cast<std::uint32_t>(fast_sub_id));
    auto slow_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(slow_sub_id), manager.make_ack_fd(slow_sub_id),
        [&](const std::string& data) { slow_received.push_back(data); }, static_cast<std::uint32_t>(slow_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "slow subscriber test first publish should succeed");
    require(manager.handle_publication() == 1, "slow subscriber test should dispatch first slot");

    auto blocked_publish = std::async(std::launch::async, [&publisher]() {
        return publisher->publish("{\"value\":2}", topic::full_buffer_policy::block_publisher);
    });
    require(blocked_publish.wait_for(20ms) == std::future_status::timeout,
            "blocking publish should wait while all ACKs are missing");

    fast_subscriber->handle_event();
    require(!manager.handle_ack(fast_sub_id), "fast subscriber alone must not release the slot");
    require(blocked_publish.wait_for(20ms) == std::future_status::timeout,
            "blocking publish should still wait for slow subscriber ACK");

    slow_subscriber->handle_event();
    require(manager.handle_ack(slow_sub_id), "slow subscriber ACK should release the slot");
    require(blocked_publish.wait_for(1s) == std::future_status::ready,
            "blocking publish should complete after all subscribers ACK");
    require(blocked_publish.get(), "blocking publish should report success after release");

    require(manager.handle_publication() == 1, "second message should be published after release");
    fast_subscriber->handle_event();
    slow_subscriber->handle_event();
    manager.handle_ack(fast_sub_id);
    manager.handle_ack(slow_sub_id);

    require((fast_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "fast subscriber should receive both messages");
    require((slow_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "slow subscriber should receive both messages");
}

void test_batched_ack_only_notifies_release_for_writer_slot() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_release_notify_batch_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    auto release_fd = manager.make_release_fd();
    std::vector<std::string> received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "first batched release publish should succeed");
    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "second batched release publish should succeed");
    require(manager.handle_publication() == 2, "manager should dispatch both queued publications");

    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "batched ACK should release at least the writer-blocking slot");

    pollfd release_poll{};
    release_poll.fd = release_fd;
    release_poll.events = POLLIN;
    require(::poll(&release_poll, 1, 0) == 1 && (release_poll.revents & POLLIN) != 0,
            "release eventfd should be readable after batched ACK");
    eventfd_t release_count = 0;
    require(::eventfd_read(release_fd, &release_count) == 0, "release eventfd should read after batched ACK");
    require(release_count == 1, "batched ACK should notify release only for the current writer slot");
    require((received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "subscriber should receive both batched messages");
}

void test_publishers_track_release_count_without_consuming_shared_wake() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_a_name = "/everest_shm_shared_release_a_test";
    const std::string shm_b_name = "/everest_shm_shared_release_b_test";

    shared_memory shm_a(shm_a_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shared_memory shm_b(shm_b_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm_a.unlink();
    shm_b.unlink();

    ring_buffer rb_a(shm_a.get_ptr());
    ring_buffer rb_b(shm_b.get_ptr());
    coordinator::initialize_ring_buffer(rb_a, slots, slot_size, subscribers);
    coordinator::initialize_ring_buffer(rb_b, slots, slot_size, subscribers);

    auto shared_release = std::make_shared<event::event_fd>();
    coordinator manager_a(rb_a, coordinator::notification_fds{std::make_shared<event::event_fd>(), shared_release,
                                                              std::make_shared<event::event_fd>(),
                                                              std::make_shared<event::semaphore_fd>()});
    coordinator manager_b(rb_b, coordinator::notification_fds{std::make_shared<event::event_fd>(), shared_release,
                                                              std::make_shared<event::event_fd>(),
                                                              std::make_shared<event::semaphore_fd>()});

    const auto sub_a_id = manager_a.add_subscriber().id;
    const auto sub_b_id = manager_b.add_subscriber().id;
    std::vector<std::string> received_a;
    std::vector<std::string> received_b;

    auto publisher_a = topic::make_publisher(ring_buffer(shm_a.get_ptr()), manager_a.make_publication_fd(),
                                             manager_a.make_release_fd());
    auto publisher_b = topic::make_publisher(ring_buffer(shm_b.get_ptr()), manager_b.make_publication_fd(),
                                             manager_b.make_release_fd());
    auto subscriber_a = topic::make_subscriber(
        ring_buffer(shm_a.get_ptr()), manager_a.make_broadcast_fd(sub_a_id), manager_a.make_ack_fd(sub_a_id),
        [&](const std::string& data) { received_a.push_back(data); }, static_cast<std::uint32_t>(sub_a_id));
    auto subscriber_b = topic::make_subscriber(
        ring_buffer(shm_b.get_ptr()), manager_b.make_broadcast_fd(sub_b_id), manager_b.make_ack_fd(sub_b_id),
        [&](const std::string& data) { received_b.push_back(data); }, static_cast<std::uint32_t>(sub_b_id));

    require(publisher_a->publish("{\"topic\":\"a\",\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "topic A first publish should succeed");
    require(publisher_b->publish("{\"topic\":\"b\",\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "topic B first publish should succeed");
    require(manager_a.handle_publication() == 1U, "topic A first publication should dispatch");
    require(manager_b.handle_publication() == 1U, "topic B first publication should dispatch");
    subscriber_a->handle_event();
    subscriber_b->handle_event();

    auto blocked_a = std::async(std::launch::async, [&]() {
        return publisher_a->publish("{\"topic\":\"a\",\"value\":2}", topic::full_buffer_policy::block_publisher);
    });
    require(blocked_a.wait_for(50ms) == std::future_status::timeout,
            "topic A second publish should block before topic A release");

    require(manager_b.handle_topic_ack().released, "topic B ACK should release only topic B");
    require(publisher_a->handle_pending_count() == 0U,
            "topic A publisher should not observe topic B release-count progress");
    eventfd_t shared_release_count = 0;
    require(::eventfd_read(shared_release->get_raw_fd(), &shared_release_count) == 0,
            "topic A blocked publisher must not consume topic B shared release wake");
    require(shared_release_count == 1U, "topic B release should leave exactly one shared wake credit");
    require(blocked_a.wait_for(50ms) == std::future_status::timeout,
            "topic A publish should remain blocked after topic B release");
    require(publisher_b->publish("{\"topic\":\"b\",\"value\":2}", topic::full_buffer_policy::block_publisher),
            "topic B publisher should publish after its own release even though topic A is blocked");

    require(manager_a.handle_topic_ack().released, "topic A ACK should release topic A");
    require(blocked_a.get(), "topic A blocked publish should complete after topic A release");
    require((received_a == std::vector<std::string>{"{\"topic\":\"a\",\"value\":1}"}),
            "topic A subscriber should receive first topic A payload");
    require((received_b == std::vector<std::string>{"{\"topic\":\"b\",\"value\":1}"}),
            "topic B subscriber should receive first topic B payload");
}

void test_subscriber_batches_ack_counter_for_drained_slots() {
    constexpr auto slots = 3U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_subscriber_ack_batch_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));

    require(publisher->publish("one", topic::full_buffer_policy::fail_immediately), "first batched ACK publish");
    require(publisher->publish("two", topic::full_buffer_policy::fail_immediately), "second batched ACK publish");
    require(publisher->publish("three", topic::full_buffer_policy::fail_immediately), "third batched ACK publish");
    require(manager.handle_publication() == slots, "manager should dispatch all batched ACK slots");

    subscriber->handle_event();

    eventfd_t ack_count = 0;
    const auto ack_fd = manager.ack_event_fd(sub_id)->get_raw_fd();
    require(::eventfd_read(ack_fd, &ack_count) == 0, "batched subscriber ACK should be readable");
    require(ack_count == slots, "subscriber should emit one ACK counter covering all drained slots");
    require(::eventfd_write(ack_fd, ack_count) == 0, "test should restore consumed batched ACK counter");
    require(manager.handle_ack(sub_id), "manager should process restored batched ACK counter");

    require((received == std::vector<std::string>{"one", "two", "three"}),
            "subscriber should receive all messages from batched ACK drain");
    for (std::uint32_t slot_idx = 0; slot_idx < slots; ++slot_idx) {
        require(rb.get_slot_header(slot_idx)->ack_count.load(std::memory_order_acquire) == subscribers,
                "batched ACK should preserve per-slot accounting");
    }
}

void test_removing_slow_subscriber_releases_blocked_slot() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_remove_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto active_sub_id = manager.add_subscriber().id;
    const auto removed_sub_id = manager.add_subscriber().id;

    std::vector<std::string> active_received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto active_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(active_sub_id), manager.make_ack_fd(active_sub_id),
        [&](const std::string& data) { active_received.push_back(data); }, static_cast<std::uint32_t>(active_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "remove subscriber test first publish should succeed");
    require(manager.handle_publication() == 1, "remove subscriber test should dispatch first slot");

    active_subscriber->handle_event();
    require(!manager.handle_ack(active_sub_id), "active subscriber alone should not release before removal");
    require(!publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "publisher should be blocked by the slow subscriber before removal");

    require(manager.remove_subscriber(removed_sub_id), "slow subscriber removal should succeed");
    require(!manager.remove_subscriber(removed_sub_id), "second removal should be a no-op");
    require(!manager.handle_ack(removed_sub_id), "removed subscriber ACK should be ignored");
    require_throws([&]() { manager.make_broadcast_fd(removed_sub_id); },
                   "removed subscriber should not receive new broadcast FDs");
    require_throws([&]() { manager.remove_subscriber(subscribers + 10); }, "invalid subscriber removal should throw");
    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "removing the slow subscriber should release the blocked slot");

    require(manager.handle_publication() == 1, "second publish should dispatch only to active subscriber");
    active_subscriber->handle_event();
    require(manager.handle_ack(active_sub_id), "active subscriber should release second slot");

    require((active_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "active subscriber should receive both messages");
}

void test_removed_subscriber_slot_reuse_does_not_deliver_stale_data() {
    // Since T-4.008 the broadcast eventfd is shared per topic. Stale credits left over from
    // before a subscriber slot was reused are harmless: each subscriber's read_idx is sourced
    // from the coordinator-issued join cursor, and the per-subscriber SHM dispatched_count
    // (not the eventfd counter) decides how many slots to consume on each wake. This test
    // exercises the worst case — a stale credit and an out-of-date in-memory subscriber slot
    // — and confirms that the reused subscriber does not deliver any stale payload to its
    // callback.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_reuse_removed_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto removed_id = manager.add_subscriber().id;
    require(manager.remove_subscriber(removed_id), "subscriber removal before reuse should succeed");
    // Inject a stale credit on the shared topic broadcast eventfd to simulate an in-flight
    // wake-up that the kernel had not yet propagated to userspace when the subscriber left.
    auto topic_broadcast_fd = manager.make_topic_broadcast_fd();
    require(::eventfd_write(topic_broadcast_fd, 1) == 0,
            "test should inject a stale broadcast credit onto the shared per-topic eventfd");

    const auto reused_registration = manager.add_subscriber();
    require(reused_registration.id == removed_id, "new subscriber should reuse the removed subscriber slot");

    std::vector<std::string> reused_received;
    auto reused_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(reused_registration.id),
        manager.make_ack_fd(reused_registration.id), [&](const std::string& data) { reused_received.push_back(data); },
        topic::subscriber_cursor{reused_registration.cursor.write_idx, reused_registration.cursor.sequence},
        static_cast<std::uint32_t>(reused_registration.id));

    // Drain the stale credit. Because no dispatch has happened the per-subscriber
    // dispatched_count is back to zero (add_subscriber reset it) and matches the subscriber's
    // local acked-count, so handle_event() must return zero processed slots and not invoke
    // the callback even though the eventfd is readable.
    require(reused_subscriber->handle_event_count() == 0,
            "stale broadcast credit must not deliver any payload to the reused subscriber");
    require(reused_received.empty(), "reused subscriber callback must not be invoked from a stale broadcast credit");
}

void test_blocked_publisher_wakes_when_slow_subscriber_is_removed() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_blocked_publish_remove_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto active_sub_id = manager.add_subscriber().id;
    const auto removed_sub_id = manager.add_subscriber().id;

    std::vector<std::string> active_received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto active_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(active_sub_id), manager.make_ack_fd(active_sub_id),
        [&](const std::string& data) { active_received.push_back(data); }, static_cast<std::uint32_t>(active_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "blocked publisher removal test first publish should succeed");
    require(manager.handle_publication() == 1, "blocked publisher removal test first publish should dispatch");

    auto blocked_publish = std::async(std::launch::async, [&publisher]() {
        return publisher->publish("{\"value\":2}", topic::full_buffer_policy::block_publisher);
    });
    require(blocked_publish.wait_for(20ms) == std::future_status::timeout,
            "publisher should block while first slot is held");

    active_subscriber->handle_event();
    require(!manager.handle_ack(active_sub_id), "active subscriber alone should not release before removal");
    require(blocked_publish.wait_for(20ms) == std::future_status::timeout,
            "publisher should still block until slow subscriber is removed");

    require(manager.remove_subscriber(removed_sub_id), "slow subscriber removal should release blocked publisher");
    require(blocked_publish.wait_for(1s) == std::future_status::ready,
            "blocked publisher should wake after slow subscriber removal");
    require(blocked_publish.get(), "blocked publisher should report success after slow subscriber removal");

    require(manager.handle_publication() == 1, "second publish should dispatch after slow subscriber removal");
    active_subscriber->handle_event();
    require(manager.handle_ack(active_sub_id), "active subscriber should release second slot after removal");
    require((active_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "active subscriber should receive both messages after slow subscriber removal");
}

void test_dynamic_subscriber_addition_queues_until_stable() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_dynamic_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto first_sub_id = manager.add_subscriber().id;
    std::vector<std::string> first_received;
    std::vector<std::string> second_received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto first_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(first_sub_id), manager.make_ack_fd(first_sub_id),
        [&](const std::string& data) { first_received.push_back(data); }, static_cast<std::uint32_t>(first_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "dynamic subscriber first publish should succeed");
    require(manager.handle_publication() == 1, "dynamic subscriber first publish should dispatch");

    const auto second_sub_id = manager.add_subscriber().id;
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "queued dynamic subscriber should not change in-flight target subscribers");
    auto second_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(second_sub_id), manager.make_ack_fd(second_sub_id),
        [&](const std::string& data) { second_received.push_back(data); }, static_cast<std::uint32_t>(second_sub_id));

    first_subscriber->handle_event();
    require(manager.handle_ack(first_sub_id), "first subscriber ACK should release the in-flight slot");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 2,
            "queued dynamic subscriber should activate after old slot is released");

    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "publish after dynamic subscriber add should succeed");
    require(manager.handle_publication() == 1, "dynamic subscriber second publish should dispatch");
    require(rb.get_slot_header(0)->target_subscribers.load(std::memory_order_acquire) == 2,
            "coordinator should stamp the active subscriber target before dispatch");
    first_subscriber->handle_event();
    second_subscriber->handle_event();
    require(!manager.handle_ack(first_sub_id), "first ACK with dynamic subscriber should not release slot");
    require(manager.handle_ack(second_sub_id), "second ACK with dynamic subscriber should release slot");

    require((first_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "original subscriber should receive both dynamic subscriber messages");
    require((second_received == std::vector<std::string>{"{\"value\":2}"}),
            "dynamic subscriber should only receive messages published after it was added");
}

void test_dynamic_subscriber_uses_join_cursor_after_publication_race() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_dynamic_subscriber_cursor_race_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto first_sub_id = manager.add_subscriber().id;
    std::vector<std::string> first_received;
    std::vector<std::string> second_received;
    std::vector<topic::sequence_validation_result> second_sequence_errors;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto first_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(first_sub_id), manager.make_ack_fd(first_sub_id),
        [&](const std::string& data) { first_received.push_back(data); }, static_cast<std::uint32_t>(first_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "cursor race first publish should succeed");
    require(manager.handle_publication() == 1, "cursor race first publish should dispatch");
    first_subscriber->handle_event();
    require(manager.handle_ack(first_sub_id), "cursor race first ACK should release the first slot");

    const auto second_registration = manager.add_subscriber();
    require(second_registration.cursor.write_idx == 1, "cursor race dynamic subscriber should join at second slot");
    require(second_registration.cursor.sequence == 2, "cursor race dynamic subscriber should expect second sequence");

    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "cursor race second publish should succeed before subscriber topic construction");
    require(manager.handle_publication() == 1, "cursor race second publish should dispatch");
    require(rb.get_metadata()->write_idx.load(std::memory_order_acquire) == 0,
            "cursor race publication should advance write index past the join cursor");

    auto second_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(second_registration.id),
        manager.make_ack_fd(second_registration.id), [&](const std::string& data) { second_received.push_back(data); },
        topic::subscriber_cursor{second_registration.cursor.write_idx, second_registration.cursor.sequence},
        [&](const topic::sequence_validation_result& result) { second_sequence_errors.push_back(result); },
        static_cast<std::uint32_t>(second_registration.id));

    second_subscriber->handle_event();
    require(!manager.handle_ack(second_registration.id),
            "cursor race dynamic subscriber ACK should wait for original subscriber");
    first_subscriber->handle_event();
    require(manager.handle_ack(first_sub_id), "cursor race original subscriber ACK should release second slot");

    require((first_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "cursor race original subscriber should receive both messages");
    require((second_received == std::vector<std::string>{"{\"value\":2}"}),
            "cursor race dynamic subscriber should read from the join cursor");
    require(second_sequence_errors.empty(), "cursor race dynamic subscriber should preserve sequence alignment");
}

void test_dynamic_subscriber_skips_publication_committed_before_join() {
    constexpr auto slots = 2U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_dynamic_subscriber_prejoin_publish_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto first_sub_id = manager.add_subscriber().id;
    std::vector<std::string> first_received;
    std::vector<std::string> second_received;
    std::vector<topic::sequence_validation_result> second_sequence_errors;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto first_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(first_sub_id), manager.make_ack_fd(first_sub_id),
        [&](const std::string& data) { first_received.push_back(data); }, static_cast<std::uint32_t>(first_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "prejoin publication should be committed before the second subscriber joins");

    const auto second_registration = manager.add_subscriber();
    require(second_registration.cursor.write_idx == 1, "prejoin subscriber should start after committed publication");
    require(second_registration.cursor.sequence == 2, "prejoin subscriber should expect the next sequence");
    auto second_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(second_registration.id),
        manager.make_ack_fd(second_registration.id), [&](const std::string& data) { second_received.push_back(data); },
        topic::subscriber_cursor{second_registration.cursor.write_idx, second_registration.cursor.sequence},
        [&](const topic::sequence_validation_result& result) { second_sequence_errors.push_back(result); },
        static_cast<std::uint32_t>(second_registration.id));

    require(manager.handle_publication() == 1, "manager should dispatch the prejoin publication");
    first_subscriber->handle_event();
    require(second_subscriber->handle_pending_count() == 0,
            "late subscriber should have no pending dispatch for prejoin publication");
    require(manager.handle_ack(first_sub_id), "first subscriber should release prejoin publication");

    require((first_received == std::vector<std::string>{"{\"value\":1}"}),
            "original subscriber should receive prejoin publication");
    require(second_received.empty(), "late subscriber must not receive publication committed before it joined");
    require(second_sequence_errors.empty(), "late subscriber should keep its sequence cursor for future publications");
}

void test_removed_pending_subscriber_does_not_activate_after_release() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_removed_pending_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto first_sub_id = manager.add_subscriber().id;
    std::vector<std::string> first_received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto first_subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(first_sub_id), manager.make_ack_fd(first_sub_id),
        [&](const std::string& data) { first_received.push_back(data); }, static_cast<std::uint32_t>(first_sub_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "removed pending subscriber test first publish should succeed");
    require(manager.handle_publication() == 1, "removed pending subscriber test first publish should dispatch");

    const auto pending_registration = manager.add_subscriber();
    require(pending_registration.state == coordinator::subscriber_join_state::pending,
            "dynamic subscriber should wait while an old slot is in flight");
    require(manager.remove_subscriber(pending_registration.id), "pending subscriber removal should succeed");

    first_subscriber->handle_event();
    require(manager.handle_ack(first_sub_id), "first subscriber ACK should release the in-flight slot");
    require(rb.get_metadata()->target_subscribers.load(std::memory_order_acquire) == 1,
            "removed pending subscriber should not be counted after release");

    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "publish after removing pending subscriber should succeed");
    require(manager.handle_publication() == 1, "second publish should dispatch after pending removal");
    require(rb.get_slot_header(0)->target_subscribers.load(std::memory_order_acquire) == 1,
            "coordinator should keep the original target after pending removal");
    first_subscriber->handle_event();
    require(manager.handle_ack(first_sub_id), "first subscriber should release second slot");

    require((first_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "original subscriber should receive both messages after pending removal");
}

void test_multiple_publishers_share_topic_sequence_numbers() {
    constexpr auto slots = 4U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_multi_publisher_sequence_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;
    std::vector<topic::sequence_validation_result> sequence_errors;

    auto publisher_a =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto publisher_b =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); },
        [&](const topic::sequence_validation_result& result) { sequence_errors.push_back(result); },
        static_cast<std::uint32_t>(sub_id));

    require(publisher_a->publish("{\"publisher\":\"a\"}", topic::full_buffer_policy::fail_immediately),
            "first publisher should publish");
    require(publisher_b->publish("{\"publisher\":\"b\"}", topic::full_buffer_policy::fail_immediately),
            "second publisher should publish with the next shared sequence");

    require(rb.get_slot_header(0)->sequence == 1, "first slot should receive sequence one");
    require(rb.get_slot_header(1)->sequence == 2, "second slot should receive sequence two");
    require(manager.handle_publication() == 2, "manager should dispatch both publisher slots");
    require(subscriber->handle_event_count() == 2, "subscriber should consume both publisher slots");
    require(manager.handle_topic_ack().ack_count == 2, "coordinator should observe both ACKs");

    require((received == std::vector<std::string>{"{\"publisher\":\"a\"}", "{\"publisher\":\"b\"}"}),
            "subscriber should receive both publisher payloads in order");
    require(sequence_errors.empty(), "multi-publisher sequence allocation should not report stale or gap slots");
}

void test_oversized_payload_is_rejected() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 8U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_oversized_payload_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));

    require(!publisher->publish("too-large", topic::full_buffer_policy::fail_immediately),
            "oversized payload should be rejected");
    require(rb.get_metadata()->write_idx.load(std::memory_order_acquire) == 0,
            "oversized payload should leave the write index unchanged");
    require(rb.get_slot_header(0)->ack_count.load(std::memory_order_acquire) == subscribers,
            "oversized payload should leave ACK accounting unchanged");

    require(publisher->publish("fits", topic::full_buffer_policy::fail_immediately),
            "payload within slot size should publish");
    require(manager.handle_publication() == 1, "valid payload should notify the coordinator");
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "valid payload ACK should release slot");
    require((received == std::vector<std::string>{"fits"}), "subscriber should only receive valid payload");
}

void test_subscriber_view_callback_uses_slot_payload_for_callback_duration() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_view_callback_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::string received;
    const char* callback_data = nullptr;
    std::size_t callback_size = 0;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber_view(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](std::string_view data) {
            callback_data = data.data();
            callback_size = data.size();
            received.assign(data);
        },
        static_cast<std::uint32_t>(sub_id));

    constexpr std::string_view payload{"{\"value\":42}"};
    require(publisher->publish(payload, topic::full_buffer_policy::fail_immediately),
            "string_view payload publish should succeed");
    require(manager.handle_publication() == 1, "view callback test should dispatch one slot");
    subscriber->handle_event();

    require(received == payload, "view callback should receive the published payload");
    require(callback_size == payload.size(), "view callback should expose the payload length");
    require(callback_data == static_cast<const char*>(rb.get_slot_payload(0)),
            "view callback should point at the shared-memory slot during callback dispatch");
    require(manager.handle_ack(sub_id), "view callback ACK should release the slot");
}

void test_zero_subscriber_publish_is_noop_success() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 0U;
    const std::string shm_name = "/everest_shm_zero_subscriber_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "zero-subscriber publish should report success");
    require(rb.get_metadata()->write_idx.load(std::memory_order_acquire) == 0,
            "zero-subscriber publish should leave the ring unchanged");
    require(rb.get_slot_header(0)->ack_count.load(std::memory_order_acquire) == 0,
            "zero-subscriber publish should leave ACK accounting unchanged");

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));

    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "publish after adding first subscriber should succeed");
    require(manager.handle_publication() == 1, "publish after adding first subscriber should dispatch");
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "first subscriber ACK should release slot");
    require((received == std::vector<std::string>{"{\"value\":2}"}),
            "first subscriber should only receive messages published after it was added");
}

void test_subscriber_topic_fd_event_handler_dispatches_and_acks() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_topic_fd_event_handler_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));
    everest::lib::io::event::fd_event_handler event_handler;

    require(event_handler.register_event_handler(subscriber.get()),
            "subscriber topic should register with fd_event_handler");
    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "fd event handler test publish should succeed");
    require(manager.handle_publication() == 1, "fd event handler test manager should dispatch");
    require(event_handler.poll(1s), "fd event handler should receive subscriber topic readiness");
    require(manager.handle_ack(sub_id), "fd event handler subscriber callback should send ACK");

    require((received == std::vector<std::string>{"{\"value\":1}"}),
            "fd event handler subscriber should receive the published payload");
}

void test_subscriber_topic_fd_event_handler_unregisters() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_topic_fd_event_unregister_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));
    everest::lib::io::event::fd_event_handler event_handler;

    require(event_handler.register_event_handler(subscriber.get()),
            "subscriber topic should register before unregister test");
    require(event_handler.unregister_event_handler(subscriber.get()),
            "subscriber topic should unregister from fd_event_handler");
    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "unregister test publish should succeed");
    require(manager.handle_publication() == 1, "unregister test manager should dispatch");
    require(!event_handler.poll(20ms), "unregistered subscriber topic should not be polled");
    require(received.empty(), "unregistered subscriber topic should not handle payload from event loop");

    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "manual topic handling should still ACK after unregister");
    require((received == std::vector<std::string>{"{\"value\":1}"}),
            "manual topic handling should continue to receive payloads after unregister");
}

void test_cross_process_publication_ack_release_flow() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_cross_process_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);
    const auto sub_id = manager.add_subscriber().id;

    const pid_t coordinator_pid = fork_child([&]() {
        require(manager.handle_publication() == 1, "cross-process coordinator should receive publication");
        require(manager.handle_topic_ack().released, "cross-process coordinator should receive ACK and release slot");
    });

    const pid_t subscriber_pid = fork_child([&]() {
        std::vector<std::string> received;
        auto subscriber = topic::make_subscriber(
            ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
            [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));
        subscriber->handle_event();
        require((received == std::vector<std::string>{"{\"value\":1}"}),
                "cross-process subscriber should receive first payload");
    });

    const pid_t publisher_pid = fork_child([&]() {
        auto publisher =
            topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
        require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
                "cross-process publisher should publish first payload");
        require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::block_publisher),
                "cross-process publisher should wake after subscriber ACK releases slot");
    });

    require_child_success(publisher_pid, "cross-process publisher child should succeed");
    require_child_success(coordinator_pid, "cross-process coordinator child should succeed");
    require_child_success(subscriber_pid, "cross-process subscriber child should succeed");
}

void test_sequence_validation_reports_gaps_and_stale_slots() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_sequence_validation_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    std::vector<std::string> received;
    std::vector<topic::sequence_validation_result> sequence_errors;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto subscriber = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
        [&](const std::string& data) { received.push_back(data); },
        [&](const topic::sequence_validation_result& result) { sequence_errors.push_back(result); },
        static_cast<std::uint32_t>(sub_id));

    require(publisher->publish("{\"value\":1}", false), "sequence test first publish should succeed");
    require(manager.handle_publication() == 1, "sequence test manager should dispatch first slot");
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "sequence test first ACK should release slot");
    require((received == std::vector<std::string>{"{\"value\":1}"}), "first sequence should be delivered");

    require(publisher->publish("{\"value\":2}", false), "sequence test second publish should succeed");
    require(manager.handle_publication() == 1, "sequence test manager should dispatch second slot");
    rb.get_slot_header(0)->sequence = 4;
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "sequence test gap ACK should release slot");
    require(sequence_errors.size() == 1, "sequence gap should be reported");
    require(sequence_errors[0].status == topic::sequence_status::gap, "sequence error should be a gap");
    require(sequence_errors[0].expected_sequence == 2, "gap should report expected sequence");
    require(sequence_errors[0].actual_sequence == 4, "gap should report actual sequence");
    require((received == std::vector<std::string>{"{\"value\":1}"}), "gap payload should not be delivered");

    require(publisher->publish("{\"value\":3}", false), "sequence test third publish should succeed");
    require(manager.handle_publication() == 1, "sequence test manager should dispatch third slot");
    subscriber->handle_event();
    require(manager.handle_ack(sub_id), "sequence test stale ACK should release slot");
    require(sequence_errors.size() == 2, "sequence stale should be reported");
    require(sequence_errors[1].status == topic::sequence_status::stale, "sequence error should be stale");
    require(sequence_errors[1].expected_sequence == 5, "stale should report expected sequence");
    require(sequence_errors[1].actual_sequence == 3, "stale should report actual sequence");
    require((received == std::vector<std::string>{"{\"value\":1}"}), "stale payload should not be delivered");
}

void test_extra_ack_without_pending_slot_is_ignored() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_extra_ack_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_id = manager.add_subscriber().id;
    // Per T-4.007 a subscriber signals an ACK by bumping its per-topic progress atomic in shared
    // memory and writing to the shared per-topic eventfd. Simulate that pairing here for an
    // "extra" ACK with no pending slot to drain.
    rb.subscriber_acked_count(static_cast<std::uint32_t>(sub_id)).fetch_add(1, std::memory_order_acq_rel);
    require(manager.ack_event_fd(sub_id)->notify(), "test should inject extra ACK notification");
    require(!manager.handle_ack(sub_id), "extra ACK without pending slot should be ignored");

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    require(publisher->publish("{\"value\":1}", false), "publish after ignored extra ACK should succeed");
    require(manager.handle_publication() == 1, "manager should dispatch after ignored extra ACK");
    rb.subscriber_acked_count(static_cast<std::uint32_t>(sub_id)).fetch_add(1, std::memory_order_acq_rel);
    require(manager.ack_event_fd(sub_id)->notify(), "test should inject valid ACK notification");
    require(manager.handle_ack(sub_id), "valid ACK after ignored extra ACK should release slot");
}

void test_topic_ack_fd_is_shared_across_subscribers() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    constexpr auto subscribers = 3U;
    const std::string shm_name = "/everest_shm_topic_ack_shared_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;
    const auto sub_c_id = manager.add_subscriber().id;

    // Per T-4.007 all subscribers on a topic share the same per-topic ACK eventfd. Each call to
    // make_ack_fd() must return a fresh duplicate (distinct FD number) pointing at the same kernel
    // object. The shared topic_ack_event_fd() is the underlying owner-side instance.
    auto fd_a = manager.make_ack_fd(sub_a_id);
    auto fd_b = manager.make_ack_fd(sub_b_id);
    auto fd_c = manager.make_ack_fd(sub_c_id);
    require(static_cast<int>(fd_a) != static_cast<int>(fd_b), "ack dups should have distinct fd numbers");
    require(static_cast<int>(fd_a) != static_cast<int>(fd_c), "ack dups should have distinct fd numbers");
    require(static_cast<int>(fd_b) != static_cast<int>(fd_c), "ack dups should have distinct fd numbers");

    // Writing to one subscriber's duplicate must be observable by the shared coordinator-side
    // eventfd; this proves all duplicates point at the same kernel object.
    require(::eventfd_write(fd_a, 1) == 0, "write to first subscriber's ACK dup should succeed");
    require(::eventfd_write(fd_b, 1) == 0, "write to second subscriber's ACK dup should succeed");
    eventfd_t observed{};
    require(::eventfd_read(manager.topic_ack_event_fd()->get_raw_fd(), &observed) == 0,
            "topic ACK eventfd should be readable after subscriber writes");
    require(observed == 2, "topic ACK eventfd should aggregate writes from all duplicates");
}

void test_topic_ack_fan_in_releases_slot_after_all_subscriber_progress() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 3U;
    const std::string shm_name = "/everest_shm_topic_ack_fan_in_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;
    const auto sub_c_id = manager.add_subscriber().id;

    std::vector<std::string> a_received;
    std::vector<std::string> b_received;
    std::vector<std::string> c_received;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto sub_a = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_a_id), manager.make_ack_fd(sub_a_id),
        [&](const std::string& data) { a_received.push_back(data); }, static_cast<std::uint32_t>(sub_a_id));
    auto sub_b = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_b_id), manager.make_ack_fd(sub_b_id),
        [&](const std::string& data) { b_received.push_back(data); }, static_cast<std::uint32_t>(sub_b_id));
    auto sub_c = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_c_id), manager.make_ack_fd(sub_c_id),
        [&](const std::string& data) { c_received.push_back(data); }, static_cast<std::uint32_t>(sub_c_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "topic ACK fan-in publish should succeed");
    require(manager.handle_publication() == 1, "manager should dispatch publication");
    require(!publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "back-pressure should hold while subscribers process the first slot");

    sub_a->handle_event();
    sub_b->handle_event();
    sub_c->handle_event();

    // A single drain via the shared per-topic ACK eventfd handler must aggregate every subscriber's
    // ack and release the slot. The shared eventfd's drained counter is 3 (one per subscriber),
    // even though only one wake-up reaches the Manager.
    const auto result = manager.handle_topic_ack();
    require(result.ack_count == 3, "handle_topic_ack should aggregate acks across subscribers");
    require(result.released, "handle_topic_ack should release the slot after the third ack");

    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "publisher should publish again once all subscribers have acked");
    require(a_received == std::vector<std::string>{"{\"value\":1}"}, "subscriber A should receive first payload");
    require(b_received == std::vector<std::string>{"{\"value\":1}"}, "subscriber B should receive first payload");
    require(c_received == std::vector<std::string>{"{\"value\":1}"}, "subscriber C should receive first payload");

    // Confirm per-subscriber accounting was preserved even though the wake-up was shared.
    const auto snapshots = manager.subscriber_backpressure_snapshots();
    auto snapshot_for = [&](coordinator::subscriber_id id) -> const coordinator::subscriber_backpressure_snapshot& {
        const auto found =
            std::find_if(snapshots.begin(), snapshots.end(), [id](const auto& snapshot) { return snapshot.id == id; });
        require(found != snapshots.end(), "snapshot should be available for every subscriber");
        return *found;
    };
    require(snapshot_for(sub_a_id).acked_slots == 1, "subscriber A should be credited with one ack");
    require(snapshot_for(sub_b_id).acked_slots == 1, "subscriber B should be credited with one ack");
    require(snapshot_for(sub_c_id).acked_slots == 1, "subscriber C should be credited with one ack");
    require(snapshot_for(sub_a_id).outstanding_slots == 0,
            "subscriber A should have no outstanding slots after release");
    require(snapshot_for(sub_b_id).outstanding_slots == 0,
            "subscriber B should have no outstanding slots after release");
    require(snapshot_for(sub_c_id).outstanding_slots == 0,
            "subscriber C should have no outstanding slots after release");
}

void test_topic_ack_handles_partial_progress_without_releasing_slot() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_topic_ack_partial_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto fast_id = manager.add_subscriber().id;
    const auto slow_id = manager.add_subscriber().id;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto fast = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(fast_id), manager.make_ack_fd(fast_id),
        [](const std::string&) {}, static_cast<std::uint32_t>(fast_id));
    auto slow = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(slow_id), manager.make_ack_fd(slow_id),
        [](const std::string&) {}, static_cast<std::uint32_t>(slow_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "partial-progress publish should succeed");
    require(manager.handle_publication() == 1, "publication should dispatch");

    fast->handle_event();
    auto partial = manager.handle_topic_ack();
    require(partial.ack_count == 1, "handle_topic_ack should observe the fast subscriber's ack");
    require(!partial.released, "slot must not release while the slow subscriber still owes an ack");

    slow->handle_event();
    auto completion = manager.handle_topic_ack();
    require(completion.ack_count == 1, "handle_topic_ack should observe the slow subscriber's ack");
    require(completion.released, "slot should release once the slow subscriber acks");
}

void test_topic_ack_disambiguates_subscriber_progress_via_shm() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_topic_ack_disambiguation_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;
    require(rb.subscriber_acked_count(static_cast<std::uint32_t>(sub_a_id)).load(std::memory_order_acquire) == 0U,
            "newly added subscriber A should start at zero SHM ack progress");
    require(rb.subscriber_acked_count(static_cast<std::uint32_t>(sub_b_id)).load(std::memory_order_acquire) == 0U,
            "newly added subscriber B should start at zero SHM ack progress");

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto sub_a_ack = manager.make_ack_fd(sub_a_id);
    auto sub_b_ack = manager.make_ack_fd(sub_b_id);

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "disambiguation publish should succeed");
    require(manager.handle_publication() == 1, "disambiguation publication should dispatch");

    // Simulate the subscriber-side topic flow: each subscriber bumps its own SHM ack progress and
    // writes one to the shared per-topic eventfd. Only subscriber B writes here; subscriber A is
    // still processing.
    rb.subscriber_acked_count(static_cast<std::uint32_t>(sub_b_id)).fetch_add(1, std::memory_order_acq_rel);
    require(::eventfd_write(sub_b_ack, 1) == 0, "subscriber B should signal the shared ACK eventfd");

    require(!manager.handle_topic_ack().released,
            "slot must not release because subscriber A has not yet bumped its SHM ack progress");

    // Subscriber A now follows up; the coordinator must distinguish A's ack from B's via the
    // per-subscriber SHM atomic, not via the shared eventfd's count.
    rb.subscriber_acked_count(static_cast<std::uint32_t>(sub_a_id)).fetch_add(1, std::memory_order_acq_rel);
    require(::eventfd_write(sub_a_ack, 1) == 0, "subscriber A should signal the shared ACK eventfd");
    require(manager.handle_topic_ack().released,
            "slot should release once both subscribers' SHM progress has been observed");

    const auto snapshots = manager.subscriber_backpressure_snapshots();
    const auto& a_snapshot = snapshots.at(static_cast<std::size_t>(sub_a_id));
    const auto& b_snapshot = snapshots.at(static_cast<std::size_t>(sub_b_id));
    require(a_snapshot.acked_slots == 1, "subscriber A snapshot should reflect one ack");
    require(b_snapshot.acked_slots == 1, "subscriber B snapshot should reflect one ack");
}

void test_topic_ack_fd_count_scales_per_topic_not_per_subscriber() {
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    constexpr auto subscribers = 8U;
    const std::string shm_name = "/everest_shm_topic_ack_fd_scaling_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    // Before any subscribers join, the coordinator already owns exactly three eventfds for this
    // topic: publication, release, and the shared per-topic ack. Their raw FDs cover that set.
    const int publication_fd = manager.publication_event_fd()->get_raw_fd();
    const int topic_ack_fd = manager.topic_ack_event_fd()->get_raw_fd();
    require(publication_fd != everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL,
            "publication eventfd should be allocated");
    require(topic_ack_fd != everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL,
            "topic ACK eventfd should be allocated");
    require(publication_fd != topic_ack_fd, "publication and topic ACK eventfds should be distinct");

    // Adding subscribers must not change the coordinator's set of *manager-owned* eventfds for the
    // topic. The handler-side FD identity must remain the same for every added subscriber.
    for (std::uint32_t i = 0; i < subscribers; ++i) {
        const auto reg = manager.add_subscriber();
        (void)reg;
        require(manager.topic_ack_event_fd()->get_raw_fd() == topic_ack_fd,
                "topic ACK eventfd FD identity should not depend on subscriber count");
    }

    // Validate scaling against the FD-budget invariant for T-4.007: at most one ACK eventfd per
    // topic, independent of the active subscriber count. We use the coordinator's
    // ack_event_fd(subscriber_id) backwards-compatible alias to confirm every subscriber resolves
    // to the same kernel eventfd identity (same FD number returned for the owner-side handle).
    for (std::uint32_t i = 0; i < subscribers; ++i) {
        require(manager.ack_event_fd(i)->get_raw_fd() == topic_ack_fd,
                "ack_event_fd(subscriber_id) should resolve to the single per-topic eventfd");
    }
}

void test_topic_broadcast_fd_is_shared_across_subscribers() {
    // T-4.008: every subscriber on a topic receives a duplicate of the same shared per-topic
    // broadcast eventfd. The duplicates must have distinct FD numbers (so each subscriber can
    // close its own) but point at the same kernel object.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    constexpr auto subscribers = 3U;
    const std::string shm_name = "/everest_shm_topic_broadcast_shared_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;
    const auto sub_c_id = manager.add_subscriber().id;

    auto fd_a = manager.make_broadcast_fd(sub_a_id);
    auto fd_b = manager.make_broadcast_fd(sub_b_id);
    auto fd_c = manager.make_broadcast_fd(sub_c_id);
    require(static_cast<int>(fd_a) != static_cast<int>(fd_b), "broadcast dups should have distinct fd numbers");
    require(static_cast<int>(fd_a) != static_cast<int>(fd_c), "broadcast dups should have distinct fd numbers");
    require(static_cast<int>(fd_b) != static_cast<int>(fd_c), "broadcast dups should have distinct fd numbers");

    // Writing to the shared owner-side eventfd must become readable on every duplicate; this
    // proves all duplicates share the underlying kernel object. Semaphore mode means three
    // writes leave three credits to drain across the duplicates.
    require(manager.topic_broadcast_event_fd()->write(3), "writing to the shared broadcast eventfd should succeed");
    eventfd_t observed_a{};
    eventfd_t observed_b{};
    eventfd_t observed_c{};
    require(::eventfd_read(fd_a, &observed_a) == 0, "subscriber A should read one credit from shared eventfd");
    require(::eventfd_read(fd_b, &observed_b) == 0, "subscriber B should read one credit from shared eventfd");
    require(::eventfd_read(fd_c, &observed_c) == 0, "subscriber C should read one credit from shared eventfd");
    require(observed_a == 1 && observed_b == 1 && observed_c == 1,
            "EFD_SEMAPHORE shared broadcast eventfd should hand out one credit per read");
}

void test_topic_broadcast_wakes_every_active_subscriber() {
    // T-4.008 multi-reader wake-up safety: a single dispatch must wake every active
    // subscriber on the topic without the eventfd race that would arise if the shared FD only
    // had one credit and one reader drained it before the others observed the event.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    constexpr auto subscribers = 4U;
    const std::string shm_name = "/everest_shm_topic_broadcast_wake_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    std::vector<coordinator::subscriber_id> ids;
    std::vector<event::unique_fd> broadcast_dups;
    for (std::uint32_t i = 0; i < subscribers; ++i) {
        ids.push_back(manager.add_subscriber().id);
        broadcast_dups.push_back(manager.make_broadcast_fd(ids.back()));
    }

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "wake-up test publish should succeed");
    require(manager.handle_publication() == 1, "wake-up test should dispatch one slot");

    // Every broadcast duplicate must report POLLIN — proving the coordinator credited the
    // shared eventfd with one wake credit per active subscriber.
    for (std::uint32_t i = 0; i < subscribers; ++i) {
        pollfd poll_fd{};
        poll_fd.fd = broadcast_dups[i];
        poll_fd.events = POLLIN;
        require(::poll(&poll_fd, 1, 0) > 0 && (poll_fd.revents & POLLIN) != 0,
                "every active subscriber must observe a wake credit after dispatch");
        eventfd_t credit{};
        require(::eventfd_read(broadcast_dups[i], &credit) == 0, "each subscriber should drain one wake credit");
        require(credit == 1, "EFD_SEMAPHORE wake credit should be exactly 1 per read");
    }

    // After every subscriber drained its credit, the shared eventfd must be empty again.
    pollfd shared{};
    shared.fd = manager.topic_broadcast_event_fd()->get_raw_fd();
    shared.events = POLLIN;
    require(::poll(&shared, 1, 0) == 0,
            "shared broadcast eventfd should be empty once every active subscriber consumed its credit");
}

void test_topic_broadcast_fan_out_drives_dispatch_via_shm() {
    // T-4.008: subscribers must consume slots from the per-subscriber SHM dispatched_count
    // delta, not from the eventfd counter. Two dispatches in quick succession leave more
    // credits than dispatched slots; the subscriber loop must still process exactly the
    // dispatched slots and not over- or under-deliver. This test pairs that behaviour with
    // the per-topic ACK fan-in.
    constexpr auto slots = 4U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_topic_broadcast_dispatch_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;

    std::vector<std::string> a_received;
    std::vector<std::string> b_received;
    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto sub_a = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_a_id), manager.make_ack_fd(sub_a_id),
        [&](const std::string& data) { a_received.push_back(data); }, static_cast<std::uint32_t>(sub_a_id));
    auto sub_b = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_b_id), manager.make_ack_fd(sub_b_id),
        [&](const std::string& data) { b_received.push_back(data); }, static_cast<std::uint32_t>(sub_b_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "first dispatch publish should succeed");
    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "second dispatch publish should succeed");
    require(manager.handle_publication() == 2, "manager should dispatch two slots");

    // Each handle_event drains exactly one wake credit but processes all available slots up
    // to the per-subscriber dispatched_count — that's the SHM-driven dispatch loop.
    // Subscriber A consumes both payloads from a single wake; subscriber B does the same.
    require(sub_a->handle_event_count() == 2, "subscriber A should consume both dispatched slots in one wake");
    require(sub_b->handle_event_count() == 2, "subscriber B should consume both dispatched slots in one wake");
    require((a_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "subscriber A should receive both payloads");
    require((b_received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
            "subscriber B should receive both payloads");

    // The coordinator must observe both subscribers' ACKs via SHM progress.
    const auto result = manager.handle_topic_ack();
    require(result.ack_count == 4, "handle_topic_ack should aggregate two acks per subscriber");
}

void test_multi_slot_wraparound_preserves_order_across_independent_ack_progress() {
    constexpr auto slots = 3U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 2U;
    const std::string shm_name = "/everest_shm_multi_slot_wraparound_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const auto sub_a_id = manager.add_subscriber().id;
    const auto sub_b_id = manager.add_subscriber().id;

    std::vector<std::string> a_received;
    std::vector<std::string> b_received;
    std::vector<topic::sequence_validation_result> a_sequence_errors;
    std::vector<topic::sequence_validation_result> b_sequence_errors;

    auto publisher =
        topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
    auto sub_a = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_a_id), manager.make_ack_fd(sub_a_id),
        [&](const std::string& data) { a_received.push_back(data); },
        [&](const topic::sequence_validation_result& result) { a_sequence_errors.push_back(result); },
        static_cast<std::uint32_t>(sub_a_id));
    auto sub_b = topic::make_subscriber(
        ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_b_id), manager.make_ack_fd(sub_b_id),
        [&](const std::string& data) { b_received.push_back(data); },
        [&](const topic::sequence_validation_result& result) { b_sequence_errors.push_back(result); },
        static_cast<std::uint32_t>(sub_b_id));

    require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
            "wrap test first publish should succeed");
    require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::fail_immediately),
            "wrap test second publish should succeed");
    require(publisher->publish("{\"value\":3}", topic::full_buffer_policy::fail_immediately),
            "wrap test third publish should succeed");
    require(rb.get_metadata()->write_idx.load(std::memory_order_acquire) == 0,
            "write_idx should wrap to slot zero after filling the ring");
    require(rb.get_slot_header(0)->sequence == 1, "slot zero should hold the first sequence before reuse");
    require(rb.get_slot_header(1)->sequence == 2, "slot one should hold the second sequence before reuse");
    require(rb.get_slot_header(2)->sequence == 3, "slot two should hold the third sequence before reuse");
    require(!publisher->publish("{\"value\":4}", topic::full_buffer_policy::fail_immediately),
            "full wrapped ring should apply back-pressure before subscriber ACKs");

    require(manager.handle_publication() == slots, "manager should dispatch the full wrapped batch");
    require(sub_a->handle_event_count() == slots, "subscriber A should consume the full first batch");
    auto partial_first_batch = manager.handle_topic_ack();
    require(partial_first_batch.ack_count == slots, "coordinator should observe subscriber A's first-batch ACKs");
    require(!partial_first_batch.released, "subscriber A alone must not release slots held for subscriber B");
    require(!publisher->publish("{\"value\":4}", topic::full_buffer_policy::fail_immediately),
            "subscriber B's missing ACKs should keep the wrapped ring full");
    require(sub_a->handle_pending_count() == 0, "stale dispatch checks must not duplicate subscriber A delivery");

    require(sub_b->handle_event_count() == slots, "subscriber B should independently consume the first batch");
    auto complete_first_batch = manager.handle_topic_ack();
    require(complete_first_batch.ack_count == slots, "coordinator should observe subscriber B's first-batch ACKs");
    require(complete_first_batch.released, "subscriber B should complete release of the wrapped ring");
    require(sub_b->handle_pending_count() == 0, "stale dispatch checks must not duplicate subscriber B delivery");

    require(publisher->publish("{\"value\":4}", topic::full_buffer_policy::fail_immediately),
            "publisher should reuse slot zero after all first-batch ACKs");
    require(publisher->publish("{\"value\":5}", topic::full_buffer_policy::fail_immediately),
            "publisher should continue through slot one after wraparound reuse");
    require(rb.get_metadata()->write_idx.load(std::memory_order_acquire) == 2,
            "write_idx should advance from wrapped slot zero to slot two after two reuses");
    require(rb.get_slot_header(0)->sequence == 4, "slot zero should hold the fourth sequence after reuse");
    require(rb.get_slot_header(1)->sequence == 5, "slot one should hold the fifth sequence after reuse");
    require(rb.get_slot_header(2)->sequence == 3, "slot two should not be rewritten by two wrapped publishes");

    require(manager.handle_publication() == 2, "manager should dispatch the reused wrapped slots");
    require(sub_b->handle_event_count() == 2, "subscriber B should consume the reused slots first");
    auto partial_second_batch = manager.handle_topic_ack();
    require(partial_second_batch.ack_count == 2, "coordinator should observe subscriber B's second-batch ACKs");
    require(!partial_second_batch.released, "subscriber B alone must not release reused slots held for subscriber A");

    require(sub_a->handle_event_count() == 2, "subscriber A should consume the reused slots after subscriber B");
    auto complete_second_batch = manager.handle_topic_ack();
    require(complete_second_batch.ack_count == 2, "coordinator should observe subscriber A's second-batch ACKs");
    require(complete_second_batch.released, "subscriber A should complete release of the reused slots");
    require(sub_a->handle_pending_count() == 0,
            "post-wrap stale dispatch checks must not duplicate subscriber A delivery");
    require(sub_b->handle_pending_count() == 0,
            "post-wrap stale dispatch checks must not duplicate subscriber B delivery");

    const std::vector<std::string> expected{"{\"value\":1}", "{\"value\":2}", "{\"value\":3}", "{\"value\":4}",
                                            "{\"value\":5}"};
    require(a_received == expected, "subscriber A should receive each wrapped payload exactly once in order");
    require(b_received == expected, "subscriber B should receive each wrapped payload exactly once in order");
    require(a_sequence_errors.empty(), "subscriber A should not report sequence gaps or stale slots across wrap");
    require(b_sequence_errors.empty(), "subscriber B should not report sequence gaps or stale slots across wrap");

    const auto counters = publisher->counter_snapshot();
    require(counters.messages_published == 5, "publisher should count all successful wraparound publishes");
    require(counters.slots_reused == 2, "publisher should count reuse of wrapped slots zero and one");
}

void test_topic_broadcast_fd_count_scales_per_topic_not_per_subscriber() {
    // T-4.008 FD-budget regression: adding subscribers to a topic must not allocate any new
    // Manager-side eventfds. The coordinator owns publication, release, topic_ack, and
    // topic_broadcast — four eventfds total, independent of subscriber count.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 64U;
    constexpr auto subscribers = 8U;
    const std::string shm_name = "/everest_shm_topic_broadcast_fd_scaling_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);

    const int publication_fd = manager.publication_event_fd()->get_raw_fd();
    const int topic_ack_fd = manager.topic_ack_event_fd()->get_raw_fd();
    const int topic_broadcast_fd = manager.topic_broadcast_event_fd()->get_raw_fd();
    require(publication_fd != everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL,
            "publication eventfd should be allocated");
    require(topic_ack_fd != everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL,
            "topic ACK eventfd should be allocated");
    require(topic_broadcast_fd != everest::lib::io::event::unique_fd::NO_DESCRIPTOR_SENTINEL,
            "topic broadcast eventfd should be allocated");
    require(topic_broadcast_fd != publication_fd && topic_broadcast_fd != topic_ack_fd,
            "topic broadcast eventfd should be distinct from publication/ack eventfds");

    for (std::uint32_t i = 0; i < subscribers; ++i) {
        const auto reg = manager.add_subscriber();
        (void)reg;
        require(manager.topic_broadcast_event_fd()->get_raw_fd() == topic_broadcast_fd,
                "topic broadcast eventfd identity should not depend on subscriber count");
        require(manager.topic_ack_event_fd()->get_raw_fd() == topic_ack_fd,
                "topic ACK eventfd identity should not depend on subscriber count");
    }
}

void test_topic_broadcast_cross_process_publication_dispatch_and_ack() {
    // T-4.008 cross-process happy path: publisher and subscriber live in separate processes
    // and exchange data over the new per-topic shared broadcast eventfd plus the SHM
    // dispatch_idx + per-subscriber ack progress fan-in. Two dispatched payloads must be
    // received by the subscriber and acknowledged through the coordinator without any
    // per-(topic, subscriber) FDs being involved.
    constexpr auto slots = 1U;
    constexpr auto slot_size = 128U;
    constexpr auto subscribers = 1U;
    const std::string shm_name = "/everest_shm_topic_broadcast_cross_process_test";

    shared_memory shm(shm_name, ring_buffer::calculate_required_size(slots, slot_size), true);
    shm.unlink();

    ring_buffer rb(shm.get_ptr());
    coordinator::initialize_ring_buffer(rb, slots, slot_size, subscribers);
    coordinator manager(rb);
    const auto sub_id = manager.add_subscriber().id;

    const pid_t coordinator_pid = fork_child([&]() {
        require(manager.handle_publication() == 1, "cross-process coordinator should receive first publication");
        require(manager.handle_topic_ack().released, "coordinator should release first slot via topic ack fan-in");
        require(manager.handle_publication() == 1, "cross-process coordinator should receive second publication");
        require(manager.handle_topic_ack().released, "coordinator should release second slot via topic ack fan-in");
    });

    const pid_t subscriber_pid = fork_child([&]() {
        std::vector<std::string> received;
        auto subscriber = topic::make_subscriber(
            ring_buffer(shm.get_ptr()), manager.make_broadcast_fd(sub_id), manager.make_ack_fd(sub_id),
            [&](const std::string& data) { received.push_back(data); }, static_cast<std::uint32_t>(sub_id));
        subscriber->handle_event();
        subscriber->handle_event();
        require((received == std::vector<std::string>{"{\"value\":1}", "{\"value\":2}"}),
                "cross-process subscriber should receive both payloads via shared broadcast eventfd");
    });

    const pid_t publisher_pid = fork_child([&]() {
        auto publisher =
            topic::make_publisher(ring_buffer(shm.get_ptr()), manager.make_publication_fd(), manager.make_release_fd());
        require(publisher->publish("{\"value\":1}", topic::full_buffer_policy::fail_immediately),
                "cross-process publisher should publish first payload");
        require(publisher->publish("{\"value\":2}", topic::full_buffer_policy::block_publisher),
                "cross-process publisher should wake after subscriber ACK releases first slot");
    });

    require_child_success(publisher_pid, "cross-process publisher child should succeed");
    require_child_success(coordinator_pid, "cross-process coordinator child should succeed");
    require_child_success(subscriber_pid, "cross-process subscriber child should succeed");
}

int main() {
    test_back_pressure_release_flow();
    test_observability_counters_track_publish_dispatch_ack_and_release();
    test_observability_counters_track_full_buffer_failure_and_reuse();
    test_slow_consumer_backpressure_snapshots_are_per_subscriber();
    test_blocking_publish_waits_for_slow_subscriber_ack();
    test_batched_ack_only_notifies_release_for_writer_slot();
    test_publishers_track_release_count_without_consuming_shared_wake();
    test_subscriber_batches_ack_counter_for_drained_slots();
    test_removing_slow_subscriber_releases_blocked_slot();
    test_removed_subscriber_slot_reuse_does_not_deliver_stale_data();
    test_blocked_publisher_wakes_when_slow_subscriber_is_removed();
    test_dynamic_subscriber_addition_queues_until_stable();
    test_dynamic_subscriber_uses_join_cursor_after_publication_race();
    test_dynamic_subscriber_skips_publication_committed_before_join();
    test_removed_pending_subscriber_does_not_activate_after_release();
    test_oversized_payload_is_rejected();
    test_subscriber_view_callback_uses_slot_payload_for_callback_duration();
    test_zero_subscriber_publish_is_noop_success();
    test_subscriber_topic_fd_event_handler_dispatches_and_acks();
    test_subscriber_topic_fd_event_handler_unregisters();
    test_cross_process_publication_ack_release_flow();
    test_sequence_validation_reports_gaps_and_stale_slots();
    test_multiple_publishers_share_topic_sequence_numbers();
    test_extra_ack_without_pending_slot_is_ignored();
    test_topic_ack_fd_is_shared_across_subscribers();
    test_topic_ack_fan_in_releases_slot_after_all_subscriber_progress();
    test_topic_ack_handles_partial_progress_without_releasing_slot();
    test_topic_ack_disambiguates_subscriber_progress_via_shm();
    test_topic_ack_fd_count_scales_per_topic_not_per_subscriber();
    test_topic_broadcast_fd_is_shared_across_subscribers();
    test_topic_broadcast_wakes_every_active_subscriber();
    test_topic_broadcast_fan_out_drives_dispatch_via_shm();
    test_multi_slot_wraparound_preserves_order_across_independent_ack_progress();
    test_topic_broadcast_fd_count_scales_per_topic_not_per_subscriber();
    test_topic_broadcast_cross_process_publication_dispatch_and_ack();

    return 0;
}
