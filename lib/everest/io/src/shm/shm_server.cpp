// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/shm_server.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/shm/coordinator.hpp>
#include <everest/io/shm/shared_memory.hpp>
#include <everest/io/shm/structures.hpp>
#include <everest/io/shm/topic_runtime.hpp>

namespace everest::lib::io::shm {

namespace {

void report_if_set(const shm_server::error_callback& callback, const io_result& result) {
    if (callback && result.status != io_status::ok) {
        callback(result.status, result.message);
    }
}

io_result make_result(io_status status, std::string message) {
    return {status, std::move(message)};
}

bool add_overflows(std::uint64_t lhs, std::uint64_t rhs) {
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
}

bool multiply_overflows(std::uint64_t lhs, std::uint64_t rhs) {
    return lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs;
}

std::uint64_t monotonic_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t registry_offset() {
    return sizeof(SegmentHeader);
}

bool size_fits_size_t(std::uint64_t value) {
    return value <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
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

topic_definition topic_with_defaults(const server_options& options, const topic_definition& topic) {
    auto effective = topic;
    if (effective.total_slots == 0U) {
        effective.total_slots = options.default_ring_slots;
    }
    if (effective.slot_size == 0U) {
        effective.slot_size = options.default_slot_size;
    }
    return effective;
}

std::vector<topic_definition> topics_with_defaults(const server_options& options) {
    std::vector<topic_definition> effective;
    effective.reserve(options.topics.size());
    for (const auto& topic : options.topics) {
        effective.push_back(topic_with_defaults(options, topic));
    }
    return effective;
}

std::uint32_t effective_registry_capacity(const server_options& options) {
    if (options.topic_registry_capacity != 0U) {
        return options.topic_registry_capacity;
    }
    return static_cast<std::uint32_t>(options.topics.size());
}

io_result validate_options(const server_options& options) {
    if (options.shm_name.empty()) {
        return make_result(io_status::invalid_options, "SHM server option shm_name must not be empty");
    }
    if (options.control_socket_name.empty()) {
        return make_result(io_status::invalid_options, "SHM server option control_socket_name must not be empty");
    }
    if (options.topics.empty()) {
        return make_result(io_status::invalid_options, "SHM server requires at least one topic definition");
    }
    if (options.topics.size() > std::numeric_limits<std::uint32_t>::max()) {
        return make_result(io_status::invalid_options, "SHM server topic definition count exceeds registry capacity");
    }
    if (options.topic_registry_capacity != 0U && options.topic_registry_capacity < options.topics.size()) {
        return make_result(io_status::invalid_options,
                           "SHM server topic registry capacity is smaller than topic count");
    }
    if (options.segment_size != 0U && !size_fits_size_t(options.segment_size)) {
        return make_result(io_status::invalid_options, "SHM server segment size exceeds addressable size");
    }

    std::unordered_set<std::string> names;
    names.reserve(options.topics.size());
    for (const auto& topic : options.topics) {
        if (topic.name.empty()) {
            return make_result(io_status::invalid_options, "SHM topic names must not be empty");
        }
        if (topic.name.size() > shm_topic_name_capacity) {
            return make_result(io_status::invalid_options,
                               "SHM topic name exceeds registry name capacity: " + topic.name);
        }
        if (!names.insert(topic.name).second) {
            return make_result(io_status::invalid_options, "Duplicate SHM topic definition: " + topic.name);
        }
        const auto effective = topic_with_defaults(options, topic);
        if (effective.total_slots == 0U) {
            return make_result(io_status::invalid_options, "SHM topic total_slots must be non-zero: " + topic.name);
        }
        if (effective.slot_size == 0U) {
            return make_result(io_status::invalid_options, "SHM topic slot_size must be non-zero: " + topic.name);
        }
        if (shm_ring_buffer_required_size_overflows(effective.total_slots, effective.slot_size)) {
            return make_result(io_status::invalid_options, "SHM topic ring size overflows: " + topic.name);
        }
    }

    return {};
}

io_result calculate_minimum_segment_size(const std::vector<topic_definition>& topics,
                                         std::uint32_t topic_registry_capacity, std::uint64_t& segment_size) {
    const auto registry_capacity = static_cast<std::uint64_t>(topic_registry_capacity);
    if (multiply_overflows(registry_capacity, shm_segment_registry_entry_size)) {
        return make_result(io_status::invalid_options, "SHM topic registry table size overflows");
    }

    const auto registry_bytes = registry_capacity * shm_segment_registry_entry_size;
    if (add_overflows(registry_offset(), registry_bytes)) {
        return make_result(io_status::invalid_options, "SHM segment registry layout overflows");
    }

    std::uint64_t next_offset = align_shm_topic_offset(registry_offset() + registry_bytes);
    if (next_offset == std::numeric_limits<std::uint64_t>::max()) {
        return make_result(io_status::invalid_options, "SHM first topic ring offset overflows");
    }

    for (const auto& topic : topics) {
        const auto ring_size = shm_ring_buffer_required_size_u64(topic.total_slots, topic.slot_size);
        if (add_overflows(next_offset, ring_size)) {
            return make_result(io_status::invalid_options, "SHM segment topic ring layout overflows");
        }
        next_offset = align_shm_topic_offset(next_offset + ring_size);
        if (next_offset == std::numeric_limits<std::uint64_t>::max()) {
            return make_result(io_status::invalid_options, "SHM segment topic ring alignment overflows");
        }
    }

    if (!size_fits_size_t(next_offset)) {
        return make_result(io_status::invalid_options, "SHM segment size exceeds addressable size");
    }

    segment_size = next_offset;
    return {};
}

io_result calculate_segment_size(const server_options& options, const std::vector<topic_definition>& effective_topics,
                                 std::uint64_t& segment_size) {
    auto minimum_segment_size = std::uint64_t{0};
    auto sizing =
        calculate_minimum_segment_size(effective_topics, effective_registry_capacity(options), minimum_segment_size);
    if (!sizing) {
        return sizing;
    }
    if (options.segment_size != 0U && options.segment_size < minimum_segment_size) {
        return make_result(io_status::invalid_options, "SHM server segment size is smaller than required topic layout");
    }

    segment_size = options.segment_size == 0U ? minimum_segment_size : options.segment_size;
    return {};
}

subscriber_snapshot adapt_snapshot(const control::server::subscriber_snapshot& snapshot) {
    return subscriber_snapshot{snapshot.topic, snapshot.client_id, static_cast<std::uint32_t>(snapshot.subscriber),
                               snapshot.state};
}

} // namespace

struct shm_server::impl {
    explicit impl(server_options server_options_) : options(std::move(server_options_)) {
    }

    struct handler_registration {
        struct wake_fd_registration {
            int fd{event::unique_fd::NO_DESCRIPTOR_SENTINEL};
            std::string fd_key;
        };

        event::fd_event_handler* handler{nullptr};
        /// Listening control socket FD (one per server).
        int listener_fd{event::unique_fd::NO_DESCRIPTOR_SENTINEL};
        /// Accepted-connection FDs, one per persistent client session. Cleanup of every
        /// subscriber/publisher registration that came from a given client is driven by
        /// EOF/POLLHUP on a single FD per client process.
        std::vector<int> client_session_fds;
        std::vector<wake_fd_registration> publication_fds;
        std::vector<wake_fd_registration> ack_fds;
    };

    io_result open();
    io_result close();
    void reset_resources(bool unlink_shm, bool unlink_control_socket);
    io_result initialize_segment(const std::vector<topic_definition>& effective_topics);
    void cache_runtime_wake_fd_keys();
    bool register_events(event::fd_event_handler& handler);
    bool unregister_events(event::fd_event_handler& handler);
    void reconcile_ack_events();
    void reconcile_ack_events(handler_registration& registration);
    void reconcile_client_sessions();
    void reconcile_client_sessions(handler_registration& registration);
    void handle_client_session_event(int connection_fd);
    void drain_pending_publications();
    void cleanup_liveness();
    void handle_listener_event();
    void handle_publication_fd(const std::string& fd_key);
    void handle_publication_event(topic_runtime& runtime);
    void handle_ack_fd(const std::string& fd_key);
    void handle_pending_topic_acks(coordinator& topic_coordinator);
    void report(const io_result& result) const;
    handler_registration* find_registration(event::fd_event_handler& handler);
    const handler_registration* find_registration(event::fd_event_handler& handler) const;
    std::vector<topic_runtime*> runtimes();
    transport_counter_snapshot counter_snapshot() const;
    void reset_counters();

    server_options options;
    error_callback error_handler;
    std::unique_ptr<shared_memory> memory;
    std::unique_ptr<topic_runtime_registry> runtime_registry;
    std::unique_ptr<control::server> control_server;
    event::fd_event_handler internal_handler;
    std::vector<handler_registration> handler_registrations;
    std::unordered_map<std::string, std::string> publication_fd_keys;
    std::unordered_map<std::string, std::string> ack_fd_keys;
    std::atomic<std::uint64_t> liveness_disconnects{0};
    bool open_state{false};
};

void shm_server::impl::report(const io_result& result) const {
    report_if_set(error_handler, result);
}

shm_server::impl::handler_registration* shm_server::impl::find_registration(event::fd_event_handler& handler) {
    const auto found = std::find_if(handler_registrations.begin(), handler_registrations.end(),
                                    [&handler](const auto& item) { return item.handler == &handler; });
    return found == handler_registrations.end() ? nullptr : &*found;
}

const shm_server::impl::handler_registration*
shm_server::impl::find_registration(event::fd_event_handler& handler) const {
    const auto found = std::find_if(handler_registrations.begin(), handler_registrations.end(),
                                    [&handler](const auto& item) { return item.handler == &handler; });
    return found == handler_registrations.end() ? nullptr : &*found;
}

std::vector<topic_runtime*> shm_server::impl::runtimes() {
    std::vector<topic_runtime*> result;
    if (runtime_registry == nullptr) {
        return result;
    }
    result.reserve(options.topics.size());
    for (const auto& topic : options.topics) {
        if (auto* runtime = runtime_registry->find(topic.name); runtime != nullptr) {
            result.push_back(runtime);
        }
    }
    return result;
}

io_result shm_server::impl::initialize_segment(const std::vector<topic_definition>& effective_topics) {
    auto* base = memory->get_ptr();
    const auto mapped_size = static_cast<std::uint64_t>(memory->get_size());
    std::memset(base, 0, memory->get_size());

    const auto topic_count = static_cast<std::uint32_t>(effective_topics.size());
    const auto header_status = initialize_segment_header(base, mapped_size, registry_offset(),
                                                         effective_registry_capacity(options), topic_count);
    if (header_status != SegmentHeaderValidationStatus::valid) {
        return make_result(io_status::resource_error, "Failed to initialize SHM segment header");
    }

    auto* header = static_cast<SegmentHeader*>(base);
    std::uint64_t ring_offset = first_topic_ring_offset_after_registry(*header);
    for (std::uint32_t index = 0U; index < topic_count; ++index) {
        const auto& topic = effective_topics[index];
        auto* entry = topic_registry_entry_at(base, *header, index);
        const auto entry_status =
            initialize_topic_registry_entry(entry, topic.name, ring_offset, topic.total_slots, topic.slot_size);
        if (entry_status != TopicRegistryValidationStatus::valid) {
            return make_result(io_status::invalid_options,
                               "Failed to initialize SHM topic registry entry: " + topic.name);
        }
        const auto validation_status = validate_topic_registry_entry(base, mapped_size, header, entry);
        if (validation_status != TopicRegistryValidationStatus::valid) {
            return make_result(io_status::resource_error,
                               "Initialized SHM topic registry entry is invalid: " + topic.name);
        }

        auto* bytes = static_cast<std::uint8_t*>(base);
        coordinator::initialize_ring_buffer(ring_buffer(bytes + entry->ring_offset), topic.total_slots, topic.slot_size,
                                            0U);
        ring_offset = align_shm_topic_offset(ring_offset + entry->ring_size);
    }

    return {};
}

void shm_server::impl::cache_runtime_wake_fd_keys() {
    publication_fd_keys.clear();
    ack_fd_keys.clear();
    for (auto* runtime : runtimes()) {
        publication_fd_keys.emplace(runtime->name(),
                                    wake_fd_key(runtime->topic_coordinator().publication_event_fd()->get_raw_fd()));
        ack_fd_keys.emplace(runtime->name(),
                            wake_fd_key(runtime->topic_coordinator().topic_ack_event_fd()->get_raw_fd()));
    }
}

io_result shm_server::impl::open() {
    if (open_state) {
        return make_result(io_status::already_open, "SHM shm_server is already open");
    }

    auto validation = validate_options(options);
    if (!validation) {
        return validation;
    }

    std::uint64_t segment_size = 0;
    auto effective_topics = topics_with_defaults(options);
    auto sizing = calculate_segment_size(options, effective_topics, segment_size);
    if (!sizing) {
        return sizing;
    }
    options.segment_size = segment_size;

    try {
        memory = std::make_unique<shared_memory>(options.shm_name, static_cast<std::size_t>(segment_size), true);
        auto initialized = initialize_segment(effective_topics);
        if (!initialized) {
            reset_resources(options.unlink_shm_on_close, options.unlink_control_socket_on_close);
            return initialized;
        }

        runtime_registry = std::make_unique<topic_runtime_registry>(options.shm_name);
        const auto registered = runtime_registry->register_active_topics(memory->get_ptr(), memory->get_size());
        if (registered != options.topics.size()) {
            reset_resources(options.unlink_shm_on_close, options.unlink_control_socket_on_close);
            return make_result(io_status::resource_error, "SHM runtime registry did not register all topics");
        }
        cache_runtime_wake_fd_keys();

        control_server = std::make_unique<control::server>();
        if (!control_server->open(options.control_socket_name, options.control_socket_abstract_namespace)) {
            auto message = control_server->error_message();
            if (message.empty()) {
                message = "Failed to open SHM control server";
            }
            reset_resources(options.unlink_shm_on_close, options.unlink_control_socket_on_close);
            return make_result(io_status::resource_error, std::move(message));
        }
        runtime_registry->register_with_control_server(*control_server);

        options.topics = std::move(effective_topics);
        open_state = true;
        register_events(internal_handler);
    } catch (const std::exception& e) {
        reset_resources(options.unlink_shm_on_close, options.unlink_control_socket_on_close);
        return make_result(io_status::resource_error, e.what());
    }

    return {};
}

void shm_server::impl::reset_resources(bool unlink_shm, bool unlink_control_socket) {
    for (auto& registration : handler_registrations) {
        if (registration.handler == nullptr) {
            continue;
        }
        for (const auto& fd : registration.ack_fds) {
            registration.handler->unregister_event_handler(fd.fd);
        }
        for (const auto fd : registration.client_session_fds) {
            registration.handler->unregister_event_handler(fd);
        }
        for (const auto& fd : registration.publication_fds) {
            registration.handler->unregister_event_handler(fd.fd);
        }
        if (registration.listener_fd != event::unique_fd::NO_DESCRIPTOR_SENTINEL) {
            registration.handler->unregister_event_handler(registration.listener_fd);
        }
    }
    handler_registrations.clear();

    if (control_server != nullptr) {
        control_server->close(unlink_control_socket);
    }
    control_server.reset();
    runtime_registry.reset();
    publication_fd_keys.clear();
    ack_fd_keys.clear();
    if (memory != nullptr) {
        const auto shm_name = memory->get_name();
        memory.reset();
        if (unlink_shm) {
            ::shm_unlink(shm_name.c_str());
        }
    }
    open_state = false;
}

io_result shm_server::impl::close() {
    if (!open_state && memory == nullptr && control_server == nullptr && runtime_registry == nullptr) {
        return {};
    }
    reset_resources(options.unlink_shm_on_close, options.unlink_control_socket_on_close);
    return {};
}

bool shm_server::impl::register_events(event::fd_event_handler& handler) {
    if (!open_state || control_server == nullptr || runtime_registry == nullptr) {
        report(make_result(io_status::not_open, "Cannot register SHM shm_server events before open()"));
        return false;
    }
    if (find_registration(handler) != nullptr) {
        return true;
    }

    handler_registration registration;
    registration.handler = &handler;

    const auto control_fd = control_server->socket_fd();
    if (!handler.register_event_handler(
            control_fd, [this](const auto&) { handle_listener_event(); }, event::poll_events::read)) {
        report(make_result(io_status::resource_error, "Failed to register SHM control server event fd"));
        return false;
    }
    registration.listener_fd = control_fd;

    for (auto* runtime : runtimes()) {
        const auto publication_fd = runtime->topic_coordinator().publication_event_fd()->get_raw_fd();
        const auto publication_fd_key = publication_fd_keys.at(runtime->name());
        if (std::find_if(registration.publication_fds.begin(), registration.publication_fds.end(),
                         [&](const auto& item) { return item.fd_key == publication_fd_key; }) ==
            registration.publication_fds.end()) {
            if (!handler.register_event_handler(
                    publication_fd,
                    [this, publication_fd_key](const auto&) { handle_publication_fd(publication_fd_key); },
                    event::poll_events::read)) {
                handler.unregister_event_handler(control_fd);
                for (const auto& fd : registration.publication_fds) {
                    handler.unregister_event_handler(fd.fd);
                }
                report(make_result(io_status::resource_error, "Failed to register SHM publication event fd"));
                return false;
            }
            registration.publication_fds.push_back({publication_fd, publication_fd_key});
        }
    }

    handler_registrations.push_back(std::move(registration));
    reconcile_ack_events(handler_registrations.back());
    reconcile_client_sessions(handler_registrations.back());
    return true;
}

bool shm_server::impl::unregister_events(event::fd_event_handler& handler) {
    auto* registration = find_registration(handler);
    if (registration == nullptr) {
        return true;
    }

    bool ok = true;
    for (const auto& fd : registration->ack_fds) {
        if (!handler.unregister_event_handler(fd.fd)) {
            ok = false;
        }
    }
    for (const auto fd : registration->client_session_fds) {
        if (!handler.unregister_event_handler(fd)) {
            ok = false;
        }
    }
    for (const auto& fd : registration->publication_fds) {
        if (!handler.unregister_event_handler(fd.fd)) {
            ok = false;
        }
    }
    if (registration->listener_fd != event::unique_fd::NO_DESCRIPTOR_SENTINEL) {
        if (!handler.unregister_event_handler(registration->listener_fd)) {
            ok = false;
        }
    }

    handler_registrations.erase(std::remove_if(handler_registrations.begin(), handler_registrations.end(),
                                               [&handler](const auto& item) { return item.handler == &handler; }),
                                handler_registrations.end());
    return ok;
}

void shm_server::impl::reconcile_ack_events() {
    for (auto& registration : handler_registrations) {
        reconcile_ack_events(registration);
    }
}

void shm_server::impl::reconcile_ack_events(handler_registration& registration) {
    if (control_server == nullptr || registration.handler == nullptr) {
        return;
    }

    std::vector<std::string> current_ack_fd_keys;
    for (auto* runtime : runtimes()) {
        const auto fd = runtime->topic_coordinator().topic_ack_event_fd()->get_raw_fd();
        const auto fd_key = ack_fd_keys.at(runtime->name());
        current_ack_fd_keys.push_back(fd_key);
        if (std::find_if(registration.ack_fds.begin(), registration.ack_fds.end(),
                         [&](const auto& item) { return item.fd_key == fd_key; }) == registration.ack_fds.end()) {
            if (registration.handler->register_event_handler(
                    fd, [this, fd_key](const auto&) { handle_ack_fd(fd_key); }, event::poll_events::read)) {
                registration.ack_fds.push_back({fd, fd_key});
            } else {
                report(make_result(io_status::resource_error, "Failed to register SHM ACK event fd"));
            }
        }
    }

    auto ack_fd = registration.ack_fds.begin();
    while (ack_fd != registration.ack_fds.end()) {
        if (std::find(current_ack_fd_keys.begin(), current_ack_fd_keys.end(), ack_fd->fd_key) ==
            current_ack_fd_keys.end()) {
            registration.handler->unregister_event_handler(ack_fd->fd);
            ack_fd = registration.ack_fds.erase(ack_fd);
        } else {
            ++ack_fd;
        }
    }
}

void shm_server::impl::reconcile_client_sessions() {
    for (auto& registration : handler_registrations) {
        reconcile_client_sessions(registration);
    }
}

void shm_server::impl::reconcile_client_sessions(handler_registration& registration) {
    if (control_server == nullptr || registration.handler == nullptr) {
        return;
    }

    const auto current_fds = control_server->active_client_fds();
    // Register new client connection FDs.
    for (auto fd : current_fds) {
        if (std::find(registration.client_session_fds.begin(), registration.client_session_fds.end(), fd) !=
            registration.client_session_fds.end()) {
            continue;
        }
        if (registration.handler->register_event_handler(
                fd,
                [this, fd](const auto& events) {
                    (void)events;
                    handle_client_session_event(fd);
                },
                event::fd_event_handler::event_list{event::poll_events::read, event::poll_events::hungup,
                                                    event::poll_events::error})) {
            registration.client_session_fds.push_back(fd);
        } else {
            report(make_result(io_status::resource_error, "Failed to register SHM client session fd"));
        }
    }

    // Drop registrations for FDs the control server has closed.
    auto session_fd = registration.client_session_fds.begin();
    while (session_fd != registration.client_session_fds.end()) {
        if (std::find(current_fds.begin(), current_fds.end(), *session_fd) == current_fds.end()) {
            registration.handler->unregister_event_handler(*session_fd);
            session_fd = registration.client_session_fds.erase(session_fd);
        } else {
            ++session_fd;
        }
    }
}

void shm_server::impl::handle_client_session_event(int connection_fd) {
    if (control_server == nullptr) {
        return;
    }
    try {
        // Drain every pending message on this connection. Multiple handshakes for distinct
        // topics may be batched on one wake-up — keep reading until the connection reports
        // either no_data or session_closed.
        while (true) {
            const auto result = control_server->handle_client(connection_fd);
            if (result.status == control::server::client_message_status::session_closed) {
                liveness_disconnects.fetch_add(1, std::memory_order_relaxed);
                reconcile_ack_events();
                reconcile_client_sessions();
                return;
            }
            if (result.status == control::server::client_message_status::no_data) {
                // No more messages right now. If the connection has actually hung up (POLLHUP
                // without bytes pending), cleanup_disconnected_clients() will tear it down
                // on the next sweep. Don't close on no_data because the kernel may simply
                // have re-armed level-triggered POLLIN before the message arrived.
                cleanup_liveness();
                return;
            }
            reconcile_ack_events();
        }
    } catch (const std::exception& e) {
        report(make_result(io_status::protocol_error, e.what()));
    }
}

void shm_server::impl::drain_pending_publications() {
    for (auto* runtime : runtimes()) {
        handle_publication_event(*runtime);
    }
}

void shm_server::impl::cleanup_liveness() {
    if (control_server == nullptr) {
        return;
    }
    auto removed_fds = control_server->cleanup_disconnected_clients();
    liveness_disconnects.fetch_add(removed_fds.size(), std::memory_order_relaxed);
    if (!removed_fds.empty()) {
        reconcile_ack_events();
        reconcile_client_sessions();
    }
    (void)removed_fds;
}

void shm_server::impl::handle_listener_event() {
    try {
        drain_pending_publications();
        if (control_server != nullptr) {
            const auto accepted = control_server->accept_pending_connections();
            (void)accepted;
            reconcile_client_sessions();
        }
        cleanup_liveness();
    } catch (const std::exception& e) {
        report(make_result(io_status::protocol_error, e.what()));
    }
}

void shm_server::impl::handle_publication_fd(const std::string& fd_key) {
    bool drained = false;
    for (auto* runtime : runtimes()) {
        auto* event_fd = runtime->topic_coordinator().publication_event_fd();
        const auto cached_fd_key = publication_fd_keys.find(runtime->name());
        if (event_fd == nullptr || cached_fd_key == publication_fd_keys.end() || cached_fd_key->second != fd_key) {
            continue;
        }
        if (!drained) {
            (void)event_fd->read();
            drained = true;
        }
        handle_publication_event(*runtime);
    }
}

void shm_server::impl::handle_publication_event(topic_runtime& runtime) {
    try {
        const auto profile_start = options.profile ? monotonic_ns() : 0U;
        std::vector<coordinator::subscriber_id> dispatched_subscribers;
        const auto dispatched = runtime.topic_coordinator().handle_pending_publication(
            [this, topic = runtime.name()](std::string_view payload, bool retain, bool clear_retained) {
                if (control_server == nullptr || !retain) {
                    return;
                }
                if (clear_retained) {
                    control_server->clear_retained_payload(topic);
                    return;
                }
                control_server->set_retained_payload(topic, std::string(payload));
            },
            [&](coordinator::subscriber_id subscriber) { dispatched_subscribers.push_back(subscriber); });
        if (control_server != nullptr && !dispatched_subscribers.empty()) {
            std::sort(dispatched_subscribers.begin(), dispatched_subscribers.end());
            dispatched_subscribers.erase(std::unique(dispatched_subscribers.begin(), dispatched_subscribers.end()),
                                         dispatched_subscribers.end());
            (void)control_server->wake_subscriber_clients(runtime.name(), dispatched_subscribers);
        }
        if (options.profile) {
            options.profile(profile_stage::server_dispatch, monotonic_ns() - profile_start);
        }
        if (options.profile_metric) {
            options.profile_metric(profile_metric::publication_batch_depth, dispatched);
        }
    } catch (const std::exception& e) {
        report(make_result(io_status::resource_error, e.what()));
    }
}

void shm_server::impl::handle_ack_fd(const std::string& fd_key) {
    bool drained = false;
    for (auto* runtime : runtimes()) {
        auto* event_fd = runtime->topic_coordinator().topic_ack_event_fd();
        const auto cached_fd_key = ack_fd_keys.find(runtime->name());
        if (event_fd == nullptr || cached_fd_key == ack_fd_keys.end() || cached_fd_key->second != fd_key) {
            continue;
        }
        if (!drained) {
            (void)event_fd->read();
            drained = true;
        }
        handle_pending_topic_acks(runtime->topic_coordinator());
    }
}

void shm_server::impl::handle_pending_topic_acks(coordinator& topic_coordinator) {
    try {
        const auto profile_start = options.profile ? monotonic_ns() : 0U;
        const auto result = topic_coordinator.handle_pending_acks();
        if (options.profile) {
            options.profile(profile_stage::ack_release, monotonic_ns() - profile_start);
        }
        if (options.profile_metric) {
            options.profile_metric(profile_metric::ack_batch_depth, result.ack_count);
        }
    } catch (const std::exception& e) {
        report(make_result(io_status::resource_error, e.what()));
    }
}

shm_server::shm_server(server_options options) : m_impl(std::make_unique<impl>(std::move(options))) {
}

shm_server::~shm_server() {
    if (m_impl != nullptr) {
        m_impl->close();
    }
}
shm_server::shm_server(shm_server&&) noexcept = default;
shm_server& shm_server::operator=(shm_server&&) noexcept = default;

const server_options& shm_server::options() const {
    return m_impl->options;
}

io_result shm_server::open() {
    auto result = m_impl->open();
    report_if_set(m_impl->error_handler, result);
    return result;
}

io_result shm_server::close() {
    auto result = m_impl->close();
    report_if_set(m_impl->error_handler, result);
    return result;
}

bool shm_server::is_open() const {
    return m_impl->open_state;
}

void shm_server::set_error_handler(error_callback handler) {
    m_impl->error_handler = std::move(handler);
}

std::vector<subscriber_snapshot> shm_server::subscriber_snapshots() const {
    if (!is_open() || m_impl->control_server == nullptr) {
        return {};
    }

    std::vector<subscriber_snapshot> snapshots;
    for (const auto& [topic, topic_snapshots] : m_impl->control_server->subscriber_snapshots()) {
        (void)topic;
        snapshots.reserve(snapshots.size() + topic_snapshots.size());
        for (const auto& snapshot : topic_snapshots) {
            snapshots.push_back(adapt_snapshot(snapshot));
        }
    }
    return snapshots;
}

std::vector<subscriber_snapshot> shm_server::subscriber_snapshots(std::string_view topic) const {
    if (!is_open() || m_impl->control_server == nullptr) {
        return {};
    }

    std::vector<subscriber_snapshot> snapshots;
    const auto control_snapshots = m_impl->control_server->subscribers_for_topic(std::string(topic));
    snapshots.reserve(control_snapshots.size());
    for (const auto& snapshot : control_snapshots) {
        snapshots.push_back(adapt_snapshot(snapshot));
    }
    return snapshots;
}

transport_counter_snapshot shm_server::impl::counter_snapshot() const {
    transport_counter_snapshot snapshot;
    snapshot.liveness_disconnects = liveness_disconnects.load(std::memory_order_relaxed);
    if (runtime_registry == nullptr) {
        return snapshot;
    }
    for (const auto& topic : options.topics) {
        const auto* runtime = runtime_registry->find(topic.name);
        if (runtime != nullptr) {
            add_transport_counters(snapshot, runtime->topic_coordinator().counter_snapshot());
        }
    }
    return snapshot;
}

void shm_server::impl::reset_counters() {
    liveness_disconnects.store(0, std::memory_order_relaxed);
    if (runtime_registry == nullptr) {
        return;
    }
    for (const auto& topic : options.topics) {
        auto* runtime = runtime_registry->find(topic.name);
        if (runtime != nullptr) {
            runtime->topic_coordinator().reset_counters();
        }
    }
}

transport_counter_snapshot shm_server::counter_snapshot() const {
    return m_impl->counter_snapshot();
}

void shm_server::reset_counters() {
    m_impl->reset_counters();
}

bool shm_server::register_events(event::fd_event_handler& handler) {
    return m_impl->register_events(handler);
}

bool shm_server::unregister_events(event::fd_event_handler& handler) {
    return m_impl->unregister_events(handler);
}

int shm_server::get_poll_fd() {
    if (!is_open()) {
        return -1;
    }
    return m_impl->internal_handler.get_poll_fd();
}

event::sync_status shm_server::sync() {
    if (!is_open()) {
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

} // namespace everest::lib::io::shm
