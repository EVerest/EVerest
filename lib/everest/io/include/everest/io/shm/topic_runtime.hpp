// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/shm/control_server.hpp>
#include <everest/io/shm/coordinator.hpp>
#include <everest/io/shm/ring_buffer.hpp>
#include <everest/io/shm/structures.hpp>

namespace everest::lib::io::shm {

/// \brief Manager-side runtime state for one active registry topic.
class topic_runtime {
public:
    /// \brief Build runtime state from an initialized registry entry and ring view.
    topic_runtime(std::string topic_name, const TopicRegistryEntry& registry_entry, ring_buffer rb);
    /// \brief Build runtime state using caller-supplied shared notification descriptors.
    topic_runtime(std::string topic_name, const TopicRegistryEntry& registry_entry, ring_buffer rb,
                  coordinator::notification_fds notification_fds);

    topic_runtime(const topic_runtime&) = delete;
    topic_runtime& operator=(const topic_runtime&) = delete;
    topic_runtime(topic_runtime&&) = delete;
    topic_runtime& operator=(topic_runtime&&) = delete;

    /// \returns The exact topic name.
    const std::string& name() const;
    /// \returns The fixed registry entry backing this runtime object.
    const TopicRegistryEntry& registry_entry() const;
    /// \returns A non-owning view of the topic ring.
    ring_buffer ring() const;
    /// \returns The coordinator that manages publication and ACK flow for this topic.
    coordinator& topic_coordinator();
    const coordinator& topic_coordinator() const;

    /// \brief Create the endpoint description registered with the control server.
    control::server::topic_endpoint make_control_endpoint(const std::string& shm_segment_name);

private:
    std::string m_name;
    const TopicRegistryEntry* m_registry_entry;
    ring_buffer m_ring;
    coordinator m_coordinator;
};

/// \brief Manager-side container mapping active SHM registry topics to stable coordinator instances.
class topic_runtime_registry {
public:
    /// \brief Construct an empty runtime registry for one SHM segment.
    explicit topic_runtime_registry(std::string shm_segment_name);

    topic_runtime_registry(const topic_runtime_registry&) = delete;
    topic_runtime_registry& operator=(const topic_runtime_registry&) = delete;
    topic_runtime_registry(topic_runtime_registry&&) = delete;
    topic_runtime_registry& operator=(topic_runtime_registry&&) = delete;

    /// \brief Validate a segment and register every active topic registry entry.
    std::size_t register_active_topics(void* segment_base, std::uint64_t mapped_segment_size);
    /// \brief Register all active topic endpoints with a control server.
    void register_with_control_server(control::server& control_server);

    /// \brief Find a runtime by exact topic name.
    topic_runtime* find(std::string_view topic_name);
    const topic_runtime* find(std::string_view topic_name) const;

    /// \returns Number of active topic runtimes.
    std::size_t size() const;
    /// \returns True when no topics have been registered.
    bool empty() const;
    /// \returns POSIX shared memory object name for this registry.
    const std::string& shm_segment_name() const;

private:
    static coordinator::notification_fds make_shared_notification_fds();
    coordinator::notification_fds make_topic_notification_fds() const;

    std::string m_shm_segment_name;
    std::list<topic_runtime> m_topics;
    std::unordered_map<std::string, topic_runtime*> m_topic_index;
    coordinator::notification_fds m_notification_fds;
};

} // namespace everest::lib::io::shm
