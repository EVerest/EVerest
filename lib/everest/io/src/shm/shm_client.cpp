// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/shm_client.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/structures.hpp>
#include <everest/io/shm/topic.hpp>

namespace everest::lib::io::shm {

namespace {

io_result make_result(io_status status, std::string message) {
    return {status, std::move(message)};
}

bool add_overflows(std::uint64_t lhs, std::uint64_t rhs) {
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
}

std::uint64_t monotonic_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void report_if_set(const shm_client::error_callback& callback, const io_result& result) {
    if (callback && result.status != io_status::ok) {
        callback(result.status, result.message);
    }
}

io_result validate_options(const client_options& options) {
    if (options.client_id.empty()) {
        return make_result(io_status::invalid_options, "SHM client option client_id must not be empty");
    }
    if (options.control.server_name.empty()) {
        return make_result(io_status::invalid_options, "SHM client control server_name must not be empty");
    }
    return {};
}

io_status map_control_error(control::error_code error) {
    switch (error) {
    case control::error_code::unknown_topic:
        return io_status::unknown_topic;
    case control::error_code::incompatible_version:
    case control::error_code::invalid_role:
        return io_status::protocol_error;
    case control::error_code::resource_error:
        return io_status::resource_error;
    }
    return io_status::protocol_error;
}

io_status map_client_error(control::client_error_code error) {
    switch (error) {
    case control::client_error_code::socket_error:
        return io_status::resource_error;
    case control::client_error_code::malformed_response:
    case control::client_error_code::invalid_response:
    case control::client_error_code::fd_mismatch:
        return io_status::protocol_error;
    }
    return io_status::protocol_error;
}

io_result map_handshake_result(const control::client_handshake_result& result, std::string_view topic) {
    if (result.is_rejected()) {
        auto message = result.rejected->message.empty()
                           ? "SHM control handshake rejected for topic " + std::string(topic)
                           : result.rejected->message;
        return make_result(map_control_error(result.rejected->error), std::move(message));
    }
    if (result.is_error()) {
        return make_result(map_client_error(result.error->code), result.error->message);
    }
    return make_result(io_status::protocol_error, "SHM control handshake did not return an accepted response");
}

bool mapping_size(const control::topic_mapping& mapping, std::size_t& size) {
    if (shm_ring_buffer_required_size_overflows(mapping.total_slots, mapping.slot_size)) {
        return false;
    }
    const auto ring_size = shm_ring_buffer_required_size_u64(mapping.total_slots, mapping.slot_size);
    if (add_overflows(mapping.ring_offset, ring_size)) {
        return false;
    }
    const auto total = mapping.ring_offset + ring_size;
    if (total > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    size = static_cast<std::size_t>(total);
    return true;
}

ring_buffer ring_from_mapping(const shared_memory& memory, const control::topic_mapping& mapping) {
    auto* bytes = static_cast<std::uint8_t*>(memory.get_ptr());
    return ring_buffer(bytes + mapping.ring_offset);
}

std::string wake_fd_key(int fd) {
    std::ifstream fdinfo("/proc/self/fdinfo/" + std::to_string(fd));
    std::string line;
    while (std::getline(fdinfo, line)) {
        constexpr std::string_view prefix = "eventfd-id:";
        if (line.rfind(prefix.data(), 0) == 0) {
            return "eventfd:" + line.substr(prefix.size());
        }
    }
    throw std::runtime_error("Failed to read eventfd-id from /proc/self/fdinfo/" + std::to_string(fd));
}

// Probe the SHM segment so the cached mapping can cover the entire server-truncated
// size, allowing later topics that live at higher offsets to reuse the same mapping
// without remapping.
std::size_t probe_segment_size(const std::string& shm_name, std::size_t minimum) {
    const int fd = ::shm_open(shm_name.c_str(), O_RDWR, 0);
    if (fd == -1) {
        return minimum;
    }
    struct stat st {};
    std::size_t size = minimum;
    if (::fstat(fd, &st) == 0 && st.st_size > 0) {
        const auto reported = static_cast<std::size_t>(st.st_size);
        if (reported > size) {
            size = reported;
        }
    }
    ::close(fd);
    return size;
}

} // namespace

struct shm_client::impl {
    explicit impl(client_options client_options_) : options(std::move(client_options_)) {
    }

    struct publisher_state {
        std::shared_ptr<shared_memory> memory;
        std::unique_ptr<topic> publisher;
    };

    struct subscriber_state {
        std::string topic_name;
        std::string fd_key;
        std::shared_ptr<shared_memory> memory;
        std::shared_ptr<topic> subscriber;
    };

    struct subscriber_fd_registration {
        std::string topic_name;
        int fd{event::unique_fd::NO_DESCRIPTOR_SENTINEL};
        std::string fd_key;
        bool is_registered{false};
    };

    struct handler_registration {
        event::fd_event_handler* handler{nullptr};
        int control_session_fd{event::unique_fd::NO_DESCRIPTOR_SENTINEL};
        std::vector<subscriber_fd_registration> subscriber_fds;
    };

    io_result connect();
    io_result disconnect();
    io_result publish(std::string_view topic_name, std::string_view payload, publish_options publish_options);
    io_result subscribe(std::string_view topic_name, message_callback callback, sequence_error_callback sequence_error,
                        subscribe_options subscribe_options);
    io_result unsubscribe(std::string_view topic_name);
    bool register_events(event::fd_event_handler& handler);
    bool unregister_events(event::fd_event_handler& handler);
    void report(const io_result& result) const;

    io_result create_publisher(std::string_view topic_name);
    io_result create_subscriber(std::string_view topic_name, message_callback callback,
                                sequence_error_callback sequence_error, subscribe_options subscribe_options);
    std::shared_ptr<shared_memory> open_or_attach_segment(const std::string& shm_name, std::size_t min_size);
    void prune_segment_cache();
    io_result ensure_control_session();
    bool register_control_session(handler_registration& registration);
    void unregister_all_events();
    bool register_subscriber(handler_registration& registration, subscriber_state& subscriber);
    void unregister_subscriber_from_all(std::string_view topic_name);
    void handle_subscriber_event(const std::string& topic_name);
    void handle_subscriber_fd(const std::string& fd_key);
    void handle_control_session_event(const event::fd_event_handler::event_list& events);
    void mark_server_disconnected();
    handler_registration* find_registration(event::fd_event_handler& handler);
    transport_counter_snapshot counter_snapshot() const;
    void reset_counters();

    client_options options;
    error_callback error_handler;
    event::fd_event_handler internal_handler;
    // Persistent control-plane session (UDS SEQPACKET) over which every handshake and
    // list_topics call for this client flows. EOF/POLLHUP on its FD is the server-loss
    // signal, and closing it is what triggers manager-side cleanup of all registrations
    // owned by this client.
    control::session control;
    std::vector<handler_registration> handler_registrations;
    std::unordered_map<std::string, publisher_state> publishers;
    std::unordered_map<std::string, subscriber_state> subscribers;
    std::vector<std::string> subscriber_order;
    // Cached SHM segment mappings keyed by shm_name. Topic handles hold a strong reference;
    // the cache only retains a weak reference so a mapping is dropped once every topic that
    // shared it has been removed.
    std::unordered_map<std::string, std::weak_ptr<shared_memory>> segment_mappings;
    std::atomic<std::uint64_t> failed_publish_attempts{0};
    std::atomic<std::uint64_t> subscriber_joins{0};
    std::atomic<std::uint64_t> subscriber_removals{0};
    std::atomic<std::uint64_t> liveness_disconnects{0};
    bool connected{false};
};

void shm_client::impl::report(const io_result& result) const {
    report_if_set(error_handler, result);
}

shm_client::impl::handler_registration* shm_client::impl::find_registration(event::fd_event_handler& handler) {
    const auto found = std::find_if(handler_registrations.begin(), handler_registrations.end(),
                                    [&handler](const auto& item) { return item.handler == &handler; });
    return found == handler_registrations.end() ? nullptr : &*found;
}

io_result shm_client::impl::connect() {
    if (connected) {
        return make_result(io_status::already_open, "SHM shm_client is already connected");
    }

    auto validation = validate_options(options);
    if (!validation) {
        return validation;
    }

    connected = true;
    handler_registration internal_registration;
    internal_registration.handler = &internal_handler;
    handler_registrations.push_back(std::move(internal_registration));
    return {};
}

io_result shm_client::impl::disconnect() {
    if (!connected && publishers.empty() && subscribers.empty() && !control.is_open()) {
        return {};
    }

    unregister_all_events();
    subscribers.clear();
    subscriber_order.clear();
    publishers.clear();
    segment_mappings.clear();
    control.close();
    connected = false;
    return {};
}

void shm_client::impl::unregister_all_events() {
    for (auto& registration : handler_registrations) {
        if (registration.handler == nullptr) {
            continue;
        }
        if (registration.control_session_fd != event::unique_fd::NO_DESCRIPTOR_SENTINEL) {
            registration.handler->unregister_event_handler(registration.control_session_fd);
            registration.control_session_fd = event::unique_fd::NO_DESCRIPTOR_SENTINEL;
        }
        for (const auto& subscriber_fd : registration.subscriber_fds) {
            if (subscriber_fd.is_registered) {
                registration.handler->unregister_event_handler(subscriber_fd.fd);
            }
        }
    }
    handler_registrations.clear();
}

io_result shm_client::impl::create_publisher(std::string_view topic_name) {
    const std::string topic_key(topic_name);
    if (publishers.find(topic_key) != publishers.end()) {
        return {};
    }

    auto session_result = ensure_control_session();
    if (!session_result) {
        return session_result;
    }

    auto handshake = control.handshake(options.client_id, topic_key, control::role::publisher);
    if (!handshake.is_accepted()) {
        return map_handshake_result(handshake, topic_name);
    }

    auto accepted = std::move(handshake.accepted.value());

    std::size_t mapped_size = 0;
    if (!mapping_size(accepted.mapping, mapped_size)) {
        return make_result(io_status::protocol_error, "SHM publisher mapping size is invalid for topic " + topic_key);
    }

    try {
        publisher_state state;
        state.memory = open_or_attach_segment(accepted.mapping.shm_name, mapped_size);
        state.publisher = topic::make_publisher(ring_from_mapping(*state.memory, accepted.mapping),
                                                std::move(accepted.fds.publication), std::move(accepted.fds.release));
        publishers.emplace(topic_key, std::move(state));
    } catch (const std::exception& e) {
        return make_result(io_status::resource_error, e.what());
    }

    return {};
}

io_result shm_client::impl::ensure_control_session() {
    if (control.is_open()) {
        return {};
    }
    if (!control.open(options.control)) {
        auto message = control.error_message();
        if (message.empty()) {
            message = "Failed to open SHM control session";
        }
        return make_result(io_status::resource_error, std::move(message));
    }
    for (auto& registration : handler_registrations) {
        if (!register_control_session(registration)) {
            unregister_all_events();
            control.close();
            return make_result(io_status::resource_error, "Failed to register SHM control session fd");
        }
    }
    return {};
}

bool shm_client::impl::register_control_session(handler_registration& registration) {
    if (registration.handler == nullptr || !control.is_open()) {
        return true;
    }
    const auto fd = control.fd();
    if (registration.control_session_fd == fd) {
        return true;
    }
    if (!registration.handler->register_event_handler(
            fd, [this](const auto& events) { handle_control_session_event(events); },
            event::fd_event_handler::event_list{event::poll_events::read, event::poll_events::hungup,
                                                event::poll_events::error})) {
        return false;
    }
    registration.control_session_fd = fd;
    return true;
}

io_result shm_client::impl::publish(std::string_view topic_name, std::string_view payload,
                                    publish_options publish_options) {
    if (!connected) {
        failed_publish_attempts.fetch_add(1, std::memory_order_relaxed);
        return make_result(io_status::not_open, "Cannot publish with a disconnected SHM shm_client");
    }
    if (topic_name.empty()) {
        failed_publish_attempts.fetch_add(1, std::memory_order_relaxed);
        return make_result(io_status::invalid_options, "SHM publish topic must not be empty");
    }

    auto setup = create_publisher(topic_name);
    if (!setup) {
        failed_publish_attempts.fetch_add(1, std::memory_order_relaxed);
        return setup;
    }

    const std::string topic_key(topic_name);
    auto publisher = publishers.find(topic_key);
    if (publisher == publishers.end() || publisher->second.publisher == nullptr) {
        failed_publish_attempts.fetch_add(1, std::memory_order_relaxed);
        return make_result(io_status::resource_error, "SHM publisher was not created for topic " + topic_key);
    }

    const auto policy = publish_options.full_buffer_behavior == publish_full_buffer_behavior::block
                            ? topic::full_buffer_policy::block_publisher
                            : topic::full_buffer_policy::fail_immediately;
    const auto profile_start = options.profile ? monotonic_ns() : 0U;
    const auto published =
        publisher->second.publisher->publish(payload, policy, publish_options.retain, publish_options.clear_retained);
    if (options.profile) {
        options.profile(profile_stage::publish_call, monotonic_ns() - profile_start);
    }
    if (!published) {
        return make_result(io_status::resource_error, "SHM publish failed for topic " + topic_key);
    }

    return {};
}

bool shm_client::impl::register_subscriber(handler_registration& registration, subscriber_state& subscriber) {
    if (registration.handler == nullptr || subscriber.subscriber == nullptr) {
        return false;
    }

    const auto fd = subscriber.subscriber->get_event_fd()->get_raw_fd();
    const auto& fd_key = subscriber.fd_key;
    const auto already_registered_topic =
        std::find_if(registration.subscriber_fds.begin(), registration.subscriber_fds.end(),
                     [&](const auto& item) { return item.topic_name == subscriber.topic_name; });
    if (already_registered_topic != registration.subscriber_fds.end()) {
        return true;
    }

    const auto already_registered_fd =
        std::find_if(registration.subscriber_fds.begin(), registration.subscriber_fds.end(),
                     [&](const auto& item) { return item.fd_key == fd_key; });
    const auto register_fd = already_registered_fd == registration.subscriber_fds.end();
    if (register_fd) {
        if (!registration.handler->register_event_handler(
                fd, [this, fd_key](const auto&) { handle_subscriber_fd(fd_key); }, event::poll_events::read)) {
            return false;
        }
    }
    registration.subscriber_fds.push_back({subscriber.topic_name, fd, fd_key, register_fd});
    return true;
}

io_result shm_client::impl::create_subscriber(std::string_view topic_name, message_callback callback,
                                              sequence_error_callback sequence_error,
                                              subscribe_options subscribe_options) {
    const std::string topic_key(topic_name);
    if (subscribers.find(topic_key) != subscribers.end()) {
        return make_result(io_status::already_open, "SHM shm_client is already subscribed to topic " + topic_key);
    }
    if (!callback) {
        return make_result(io_status::invalid_options, "SHM subscribe callback must be set");
    }
    if (!subscribe_options.use_join_cursor) {
        return make_result(io_status::invalid_options, "SHM subscribe requires the manager-provided join cursor");
    }

    auto session_result = ensure_control_session();
    if (!session_result) {
        return session_result;
    }

    auto handshake = control.handshake(options.client_id, topic_key, control::role::subscriber);
    if (!handshake.is_accepted()) {
        return map_handshake_result(handshake, topic_name);
    }

    auto accepted = std::move(handshake.accepted.value());

    if (!accepted.cursor.has_value()) {
        return make_result(io_status::protocol_error,
                           "SHM subscriber handshake is missing join cursor for topic " + topic_key);
    }
    if (!accepted.subscriber_id.has_value()) {
        return make_result(io_status::protocol_error,
                           "SHM subscriber handshake is missing subscriber id for topic " + topic_key);
    }

    std::size_t mapped_size = 0;
    if (!mapping_size(accepted.mapping, mapped_size)) {
        return make_result(io_status::protocol_error, "SHM subscriber mapping size is invalid for topic " + topic_key);
    }

    try {
        subscriber_state state;
        state.topic_name = topic_key;
        state.memory = open_or_attach_segment(accepted.mapping.shm_name, mapped_size);
        auto retained_callback = callback;
        auto dispatch_callback = [topic_key, callback = std::move(callback)](std::string_view payload) {
            callback(topic_key, payload);
        };
        state.subscriber = topic::make_subscriber_view(
            ring_from_mapping(*state.memory, accepted.mapping), std::move(accepted.fds.broadcast),
            std::move(accepted.fds.ack), std::move(dispatch_callback),
            topic::subscriber_cursor{accepted.cursor->write_idx, accepted.cursor->sequence}, std::move(sequence_error),
            accepted.subscriber_id.value());
        state.fd_key = wake_fd_key(state.subscriber->get_event_fd()->get_raw_fd());

        auto [inserted, ok] = subscribers.emplace(topic_key, std::move(state));
        if (!ok) {
            return make_result(io_status::already_open, "SHM shm_client is already subscribed to topic " + topic_key);
        }
        for (auto& registration : handler_registrations) {
            if (!register_subscriber(registration, inserted->second)) {
                unsubscribe(topic_key);
                return make_result(io_status::resource_error, "Failed to register SHM subscriber event fd");
            }
        }
        subscriber_order.push_back(topic_key);
        if (accepted.retained_payload.has_value()) {
            retained_callback(topic_key, accepted.retained_payload.value());
        }
        subscriber_joins.fetch_add(1, std::memory_order_relaxed);
    } catch (const std::exception& e) {
        return make_result(io_status::resource_error, e.what());
    }

    return {};
}

io_result shm_client::impl::subscribe(std::string_view topic_name, message_callback callback,
                                      sequence_error_callback sequence_error, subscribe_options subscribe_options) {
    if (!connected) {
        return make_result(io_status::not_open, "Cannot subscribe with a disconnected SHM shm_client");
    }
    if (topic_name.empty()) {
        return make_result(io_status::invalid_options, "SHM subscribe topic must not be empty");
    }

    return create_subscriber(topic_name, std::move(callback), std::move(sequence_error), subscribe_options);
}

void shm_client::impl::unregister_subscriber_from_all(std::string_view topic_name) {
    const std::string topic_key(topic_name);
    for (auto& registration : handler_registrations) {
        if (registration.handler == nullptr) {
            continue;
        }

        auto item = registration.subscriber_fds.begin();
        while (item != registration.subscriber_fds.end()) {
            if (item->topic_name == topic_key) {
                const auto fd = item->fd;
                const auto fd_key = item->fd_key;
                const auto was_registered = item->is_registered;
                item = registration.subscriber_fds.erase(item);
                const auto fd_still_used =
                    std::find_if(registration.subscriber_fds.begin(), registration.subscriber_fds.end(),
                                 [&](const auto& other) { return other.fd_key == fd_key; });
                if (!was_registered) {
                    continue;
                }
                if (fd_still_used == registration.subscriber_fds.end()) {
                    registration.handler->unregister_event_handler(fd);
                } else {
                    registration.handler->unregister_event_handler(fd);
                    if (!registration.handler->register_event_handler(
                            fd_still_used->fd, [this, fd_key](const auto&) { handle_subscriber_fd(fd_key); },
                            event::poll_events::read)) {
                        report(make_result(io_status::resource_error, "Failed to re-register SHM subscriber event fd"));
                    } else {
                        fd_still_used->is_registered = true;
                    }
                }
            } else {
                ++item;
            }
        }
    }
}

io_result shm_client::impl::unsubscribe(std::string_view topic_name) {
    if (!connected) {
        return make_result(io_status::not_open, "Cannot unsubscribe with a disconnected SHM shm_client");
    }

    const std::string topic_key(topic_name);
    auto subscriber = subscribers.find(topic_key);
    if (subscriber == subscribers.end()) {
        return make_result(io_status::not_open, "SHM shm_client is not subscribed to topic " + topic_key);
    }

    auto session_result = ensure_control_session();
    if (!session_result) {
        return session_result;
    }
    auto unsubscribe_result = control.unsubscribe(options.client_id, topic_key);
    if (unsubscribe_result.is_error()) {
        return make_result(map_client_error(unsubscribe_result.error->code), unsubscribe_result.error->message);
    }
    if (unsubscribe_result.is_rejected()) {
        auto message = unsubscribe_result.rejected->message.empty()
                           ? "SHM control unsubscribe rejected for topic " + topic_key
                           : unsubscribe_result.rejected->message;
        return make_result(map_control_error(unsubscribe_result.rejected->error), std::move(message));
    }

    unregister_subscriber_from_all(topic_key);
    subscribers.erase(subscriber);
    subscriber_order.erase(std::remove(subscriber_order.begin(), subscriber_order.end(), topic_key),
                           subscriber_order.end());
    prune_segment_cache();
    subscriber_removals.fetch_add(1, std::memory_order_relaxed);
    return {};
}

void shm_client::impl::handle_control_session_event(const event::fd_event_handler::event_list& events) {
    if (!connected) {
        return;
    }
    if (events & event::poll_events::read || events & event::poll_events::hungup ||
        events & event::poll_events::error) {
        mark_server_disconnected();
    }
}

void shm_client::impl::mark_server_disconnected() {
    if (!connected) {
        return;
    }

    unregister_all_events();
    subscribers.clear();
    subscriber_order.clear();
    publishers.clear();
    segment_mappings.clear();
    control.close();
    connected = false;
    liveness_disconnects.fetch_add(1, std::memory_order_relaxed);
    report(make_result(io_status::not_open, "SHM server liveness closed; control owner disconnected"));
}

void shm_client::impl::handle_subscriber_event(const std::string& topic_name) {
    auto subscriber = subscribers.find(topic_name);
    if (subscriber == subscribers.end() || subscriber->second.subscriber == nullptr) {
        return;
    }

    try {
        const auto profile_start = options.profile ? monotonic_ns() : 0U;
        const auto dispatch_count = subscriber->second.subscriber->handle_event_count();
        if (options.profile) {
            options.profile(profile_stage::subscriber_callback_path, monotonic_ns() - profile_start);
        }
        if (options.profile_metric) {
            options.profile_metric(profile_metric::subscriber_dispatch_batch_depth, dispatch_count);
        }
    } catch (const std::exception& e) {
        report(make_result(io_status::resource_error, e.what()));
    }
}

void shm_client::impl::handle_subscriber_fd(const std::string& fd_key) {
    std::vector<std::string> active_topics;
    active_topics.reserve(subscribers.size());
    for (const auto& topic_name : subscriber_order) {
        auto found = subscribers.find(topic_name);
        if (found == subscribers.end() || found->second.subscriber == nullptr ||
            found->second.subscriber->get_event_fd() == nullptr) {
            continue;
        }
        active_topics.push_back(topic_name);
    }
    for (const auto& topic_name : active_topics) {
        const auto found = subscribers.find(topic_name);
        if (found != subscribers.end() && found->second.subscriber != nullptr &&
            found->second.subscriber->get_event_fd() != nullptr && found->second.fd_key == fd_key) {
            (void)found->second.subscriber->get_event_fd()->read();
            break;
        }
    }

    for (const auto& topic_name : active_topics) {
        auto found = subscribers.find(topic_name);
        if (found == subscribers.end() || found->second.subscriber == nullptr ||
            found->second.subscriber->get_event_fd() == nullptr) {
            continue;
        }

        auto& subscriber = found->second;
        auto subscriber_topic = subscriber.subscriber;
        auto subscriber_memory = subscriber.memory;
        if (subscriber_topic == nullptr) {
            continue;
        }
        try {
            const auto profile_start = options.profile ? monotonic_ns() : 0U;
            const auto dispatch_count = subscriber_topic->handle_pending_count();
            if (options.profile) {
                options.profile(profile_stage::subscriber_callback_path, monotonic_ns() - profile_start);
            }
            if (options.profile_metric) {
                options.profile_metric(profile_metric::subscriber_dispatch_batch_depth, dispatch_count);
            }
        } catch (const std::exception& e) {
            report(make_result(io_status::resource_error, e.what()));
        }
    }
}

bool shm_client::impl::register_events(event::fd_event_handler& handler) {
    if (!connected) {
        report(make_result(io_status::not_open, "Cannot register SHM shm_client events before connect()"));
        return false;
    }
    if (find_registration(handler) != nullptr) {
        return true;
    }

    handler_registration registration;
    registration.handler = &handler;
    if (!register_control_session(registration)) {
        report(make_result(io_status::resource_error, "Failed to register SHM control session fd"));
        return false;
    }
    for (auto& [topic_name, subscriber] : subscribers) {
        (void)topic_name;
        if (!register_subscriber(registration, subscriber)) {
            if (registration.control_session_fd != event::unique_fd::NO_DESCRIPTOR_SENTINEL) {
                handler.unregister_event_handler(registration.control_session_fd);
            }
            for (const auto& subscriber_fd : registration.subscriber_fds) {
                if (subscriber_fd.is_registered) {
                    handler.unregister_event_handler(subscriber_fd.fd);
                }
            }
            report(make_result(io_status::resource_error, "Failed to register SHM subscriber event fd"));
            return false;
        }
    }

    handler_registrations.push_back(std::move(registration));
    return true;
}

bool shm_client::impl::unregister_events(event::fd_event_handler& handler) {
    auto* registration = find_registration(handler);
    if (registration == nullptr) {
        return true;
    }

    bool ok = true;
    if (registration->control_session_fd != event::unique_fd::NO_DESCRIPTOR_SENTINEL) {
        ok = handler.unregister_event_handler(registration->control_session_fd) && ok;
    }
    for (const auto& subscriber_fd : registration->subscriber_fds) {
        if (subscriber_fd.is_registered) {
            ok = handler.unregister_event_handler(subscriber_fd.fd) && ok;
        }
    }

    handler_registrations.erase(std::remove_if(handler_registrations.begin(), handler_registrations.end(),
                                               [&handler](const auto& item) { return item.handler == &handler; }),
                                handler_registrations.end());
    return ok;
}

shm_client::shm_client(client_options options) : m_impl(std::make_unique<impl>(std::move(options))) {
}

shm_client::~shm_client() {
    if (m_impl != nullptr) {
        m_impl->disconnect();
    }
}
shm_client::shm_client(shm_client&&) noexcept = default;
shm_client& shm_client::operator=(shm_client&&) noexcept = default;

const client_options& shm_client::options() const {
    return m_impl->options;
}

const std::string& shm_client::client_id() const {
    return m_impl->options.client_id;
}

io_result shm_client::connect() {
    auto result = m_impl->connect();
    report_if_set(m_impl->error_handler, result);
    return result;
}

io_result shm_client::disconnect() {
    auto result = m_impl->disconnect();
    report_if_set(m_impl->error_handler, result);
    return result;
}

bool shm_client::is_connected() const {
    return m_impl->connected;
}

void shm_client::set_error_handler(error_callback handler) {
    m_impl->error_handler = std::move(handler);
}

io_result shm_client::publish(std::string_view topic, std::string_view payload, publish_options options) {
    auto result = m_impl->publish(topic, payload, options);
    report_if_set(m_impl->error_handler, result);
    return result;
}

io_result shm_client::subscribe(std::string_view topic, message_callback callback, subscribe_options options) {
    auto result = m_impl->subscribe(topic, std::move(callback), {}, options);
    report_if_set(m_impl->error_handler, result);
    return result;
}

io_result shm_client::subscribe(std::string_view topic, message_callback callback,
                                sequence_error_callback sequence_error, subscribe_options options) {
    auto result = m_impl->subscribe(topic, std::move(callback), std::move(sequence_error), options);
    report_if_set(m_impl->error_handler, result);
    return result;
}

io_result shm_client::subscribe(const std::vector<std::string>& topics, message_callback callback,
                                subscribe_options options) {
    if (!m_impl->connected) {
        auto result = make_result(io_status::not_open, "Cannot subscribe with a disconnected SHM shm_client");
        report_if_set(m_impl->error_handler, result);
        return result;
    }
    if (!callback) {
        auto result = make_result(io_status::invalid_options, "SHM subscribe callback must be set");
        report_if_set(m_impl->error_handler, result);
        return result;
    }

    std::vector<std::string> created;
    created.reserve(topics.size());
    for (const auto& topic_name : topics) {
        auto topic_callback = callback;
        auto result = m_impl->subscribe(topic_name, std::move(topic_callback), {}, options);
        if (!result) {
            for (auto rollback = created.rbegin(); rollback != created.rend(); ++rollback) {
                (void)m_impl->unsubscribe(*rollback);
            }
            report_if_set(m_impl->error_handler, result);
            return result;
        }
        created.push_back(topic_name);
    }

    return {};
}

io_result shm_client::unsubscribe(std::string_view topic) {
    auto result = m_impl->unsubscribe(topic);
    report_if_set(m_impl->error_handler, result);
    return result;
}

shm_client::registered_topics_result shm_client::registered_topics() {
    registered_topics_result result;
    if (!m_impl->connected) {
        result.status = io_status::not_open;
        result.message = "Cannot list registered topics with a disconnected SHM shm_client";
        report_if_set(m_impl->error_handler, io_result{result.status, result.message});
        return result;
    }

    auto query = control::request_topic_list(m_impl->options.control, m_impl->options.client_id);
    if (query.is_accepted()) {
        result.topics = std::move(query.accepted->topics);
        return result;
    }

    io_result mapped;
    if (query.is_rejected()) {
        mapped.status = map_control_error(query.rejected->error);
        mapped.message = query.rejected->message.empty() ? "SHM control list_topics rejected" : query.rejected->message;
    } else if (query.is_error()) {
        mapped.status = map_client_error(query.error->code);
        mapped.message = query.error->message;
    } else {
        mapped.status = io_status::protocol_error;
        mapped.message = "SHM control list_topics did not return an accepted response";
    }
    result.status = mapped.status;
    result.message = mapped.message;
    report_if_set(m_impl->error_handler, mapped);
    return result;
}

transport_counter_snapshot shm_client::counter_snapshot() const {
    return m_impl->counter_snapshot();
}

void shm_client::reset_counters() {
    m_impl->reset_counters();
}

bool shm_client::register_events(event::fd_event_handler& handler) {
    return m_impl->register_events(handler);
}

bool shm_client::unregister_events(event::fd_event_handler& handler) {
    return m_impl->unregister_events(handler);
}

int shm_client::get_poll_fd() {
    if (!is_connected()) {
        return -1;
    }
    return m_impl->internal_handler.get_poll_fd();
}

event::sync_status shm_client::sync() {
    if (!is_connected()) {
        return event::sync_status::error;
    }
    const auto ready = m_impl->internal_handler.poll(std::chrono::milliseconds(0));
    if (ready && m_impl->options.profile_metric) {
        m_impl->options.profile_metric(profile_metric::event_loop_ready_fd_count,
                                       m_impl->internal_handler.last_ready_count());
    }
    m_impl->internal_handler.run_actions();
    return ready ? event::sync_status::ok : event::sync_status::timeout;
}

transport_counter_snapshot shm_client::impl::counter_snapshot() const {
    transport_counter_snapshot snapshot;
    snapshot.failed_publish_attempts = failed_publish_attempts.load(std::memory_order_relaxed);
    snapshot.subscriber_joins = subscriber_joins.load(std::memory_order_relaxed);
    snapshot.subscriber_removals = subscriber_removals.load(std::memory_order_relaxed);
    snapshot.liveness_disconnects = liveness_disconnects.load(std::memory_order_relaxed);
    for (const auto& [topic_name, publisher] : publishers) {
        (void)topic_name;
        if (publisher.publisher != nullptr) {
            add_transport_counters(snapshot, publisher.publisher->counter_snapshot());
        }
    }
    for (const auto& [topic_name, subscriber] : subscribers) {
        (void)topic_name;
        if (subscriber.subscriber != nullptr) {
            add_transport_counters(snapshot, subscriber.subscriber->counter_snapshot());
        }
    }
    return snapshot;
}

std::shared_ptr<shared_memory> shm_client::impl::open_or_attach_segment(const std::string& shm_name,
                                                                        std::size_t min_size) {
    auto it = segment_mappings.find(shm_name);
    if (it != segment_mappings.end()) {
        if (auto existing = it->second.lock()) {
            if (existing->get_size() >= min_size) {
                return existing;
            }
            // The cached mapping is too small for the requested topic. This is unexpected when
            // every topic lives inside the same server-truncated segment; recreate the mapping
            // so the new (larger) one is used by future topics on this shm_name.
            segment_mappings.erase(it);
        } else {
            segment_mappings.erase(it);
        }
    }
    const auto map_size = probe_segment_size(shm_name, min_size);
    auto memory = std::make_shared<shared_memory>(shm_name, map_size, false);
    segment_mappings[shm_name] = memory;
    return memory;
}

void shm_client::impl::prune_segment_cache() {
    for (auto it = segment_mappings.begin(); it != segment_mappings.end();) {
        if (it->second.expired()) {
            it = segment_mappings.erase(it);
        } else {
            ++it;
        }
    }
}

void shm_client::impl::reset_counters() {
    failed_publish_attempts.store(0, std::memory_order_relaxed);
    subscriber_joins.store(0, std::memory_order_relaxed);
    subscriber_removals.store(0, std::memory_order_relaxed);
    liveness_disconnects.store(0, std::memory_order_relaxed);
    for (auto& [topic_name, publisher] : publishers) {
        (void)topic_name;
        if (publisher.publisher != nullptr) {
            publisher.publisher->reset_counters();
        }
    }
    for (auto& [topic_name, subscriber] : subscribers) {
        (void)topic_name;
        if (subscriber.subscriber != nullptr) {
            subscriber.subscriber->reset_counters();
        }
    }
}

shm_client::registered_topics_result::operator bool() const {
    return status == io_status::ok;
}

} // namespace everest::lib::io::shm
