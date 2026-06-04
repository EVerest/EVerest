// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/topic_runtime.hpp>

#include <stdexcept>
#include <utility>

namespace everest::lib::io::shm {

namespace {

std::string topic_name_from_entry(const TopicRegistryEntry& entry) {
    return std::string(reinterpret_cast<const char*>(entry.topic_name), entry.topic_name_length);
}

void require_valid_segment_header(const void* segment_base, std::uint64_t mapped_segment_size) {
    if (validate_segment_header(segment_base, mapped_segment_size) != SegmentHeaderValidationStatus::valid) {
        throw std::invalid_argument("SHM topic runtime registry needs a valid segment header");
    }
}

} // namespace

topic_runtime::topic_runtime(std::string topic_name, const TopicRegistryEntry& registry_entry, ring_buffer rb) :
    m_name(std::move(topic_name)), m_registry_entry(&registry_entry), m_ring(rb), m_coordinator(rb) {
}

topic_runtime::topic_runtime(std::string topic_name, const TopicRegistryEntry& registry_entry, ring_buffer rb,
                             coordinator::notification_fds notification_fds) :
    m_name(std::move(topic_name)),
    m_registry_entry(&registry_entry),
    m_ring(rb),
    m_coordinator(rb, std::move(notification_fds)) {
}

const std::string& topic_runtime::name() const {
    return m_name;
}

const TopicRegistryEntry& topic_runtime::registry_entry() const {
    return *m_registry_entry;
}

ring_buffer topic_runtime::ring() const {
    return m_ring;
}

coordinator& topic_runtime::topic_coordinator() {
    return m_coordinator;
}

const coordinator& topic_runtime::topic_coordinator() const {
    return m_coordinator;
}

control::server::topic_endpoint topic_runtime::make_control_endpoint(const std::string& shm_segment_name) {
    return control::server::topic_endpoint{shm_segment_name, m_registry_entry->ring_offset,
                                           m_registry_entry->total_slots, m_registry_entry->slot_size, &m_coordinator};
}

topic_runtime_registry::topic_runtime_registry(std::string shm_segment_name) :
    m_shm_segment_name(std::move(shm_segment_name)), m_notification_fds(make_shared_notification_fds()) {
}

std::size_t topic_runtime_registry::register_active_topics(void* segment_base, std::uint64_t mapped_segment_size) {
    require_valid_segment_header(segment_base, mapped_segment_size);

    auto* header = static_cast<SegmentHeader*>(segment_base);
    std::size_t registered = 0;
    for (std::uint32_t index = 0U; index < header->registry_entry_count; ++index) {
        const auto* entry = topic_registry_entry_at(segment_base, *header, index);
        if (entry == nullptr || (entry->flags & shm_topic_registry_entry_active) == 0U) {
            continue;
        }
        if (validate_topic_registry_entry(segment_base, mapped_segment_size, header, entry) !=
            TopicRegistryValidationStatus::valid) {
            throw std::invalid_argument("SHM topic runtime registry found an invalid active topic entry");
        }

        auto topic_name = topic_name_from_entry(*entry);
        if (m_topic_index.find(topic_name) != m_topic_index.end()) {
            throw std::invalid_argument("SHM topic runtime registry topic is already registered");
        }

        auto* bytes = static_cast<std::uint8_t*>(segment_base);
        m_topics.emplace_back(topic_name, *entry, ring_buffer(bytes + entry->ring_offset),
                              make_topic_notification_fds());
        auto& runtime = m_topics.back();
        m_topic_index.emplace(runtime.name(), &runtime);
        ++registered;
    }

    return registered;
}

void topic_runtime_registry::register_with_control_server(control::server& control_server) {
    for (auto& runtime : m_topics) {
        control_server.register_topic(runtime.name(), runtime.make_control_endpoint(m_shm_segment_name));
    }
}

topic_runtime* topic_runtime_registry::find(std::string_view topic_name) {
    const auto it = m_topic_index.find(std::string(topic_name));
    if (it == m_topic_index.end()) {
        return nullptr;
    }
    return it->second;
}

const topic_runtime* topic_runtime_registry::find(std::string_view topic_name) const {
    const auto it = m_topic_index.find(std::string(topic_name));
    if (it == m_topic_index.end()) {
        return nullptr;
    }
    return it->second;
}

std::size_t topic_runtime_registry::size() const {
    return m_topics.size();
}

bool topic_runtime_registry::empty() const {
    return m_topics.empty();
}

const std::string& topic_runtime_registry::shm_segment_name() const {
    return m_shm_segment_name;
}

coordinator::notification_fds topic_runtime_registry::make_shared_notification_fds() {
    return coordinator::notification_fds{std::make_shared<event::event_fd>(), std::make_shared<event::event_fd>(),
                                         std::make_shared<event::event_fd>(), std::make_shared<event::semaphore_fd>()};
}

coordinator::notification_fds topic_runtime_registry::make_topic_notification_fds() const {
    return coordinator::notification_fds{m_notification_fds.publication, m_notification_fds.release,
                                         m_notification_fds.ack, m_notification_fds.broadcast};
}

} // namespace everest::lib::io::shm
