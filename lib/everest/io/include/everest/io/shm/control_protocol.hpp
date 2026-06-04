// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace everest::lib::io::shm::control {

/// \brief Wire protocol version for SHM control messages.
///
/// The control protocol is a hard compatibility boundary. Clients and servers reject
/// mismatched versions before exchanging shared-memory mappings or file descriptors.
/// Version 6 uses a persistent UDS `SOCK_SEQPACKET` connection per client, per-session
/// subscriber wake descriptors, shared per-topic ACK descriptors, and SHM-resident
/// publication/release/subscriber progress counters.
constexpr std::uint32_t protocol_version = 6;

/// \brief Type of request carried in a control message.
enum class request_kind {
    handshake,
    list_topics,
    unsubscribe,
};

/// \brief Access role requested by a client for a topic.
enum class role {
    publisher,
    subscriber,
    publisher_subscriber,
};

/// \brief Initial subscriber state assigned by the coordinator.
enum class join_state {
    active,
    pending,
};

/// \brief Error codes returned by the server for rejected control requests.
enum class error_code {
    incompatible_version,
    unknown_topic,
    invalid_role,
    resource_error,
};

/// \brief Indexes of file descriptors transferred with an accepted handshake response.
///
/// The indexes address the `SCM_RIGHTS` payload that accompanies the JSON response. A
/// field is empty when the requested role does not need the corresponding descriptor.
struct fd_bundle {
    /// Publisher-to-manager publication notification descriptor.
    std::optional<std::uint32_t> publication;
    /// Manager-to-publisher release notification descriptor.
    std::optional<std::uint32_t> release;
    /// Client-session wake descriptor for subscriber dispatch notifications.
    std::optional<std::uint32_t> broadcast;
    /// Subscriber-to-manager ACK notification descriptor.
    std::optional<std::uint32_t> ack;
};

/// \brief Shared-memory mapping details for one topic ring.
struct topic_mapping {
    /// POSIX shared memory object name.
    std::string shm_name;
    /// Byte offset of the topic ring from the start of the mapped segment.
    std::uint64_t ring_offset{0};
    /// Number of slots in the ring.
    std::uint32_t total_slots{0};
    /// Payload capacity of each slot in bytes.
    std::uint32_t slot_size{0};
};

/// \brief Initial subscriber read position returned by a successful handshake.
struct join_cursor {
    std::uint32_t write_idx{0};
    std::uint64_t sequence{0};
};

/// \brief Request to register a client as publisher and/or subscriber for a topic.
struct handshake_request {
    std::uint32_t version{protocol_version};
    std::string client_id;
    std::string topic;
    /// Requested topic access. Handshake requests do not carry file descriptors.
    role topic_role{role::subscriber};
};

/// \brief Server response to a topic registration request.
struct handshake_response {
    std::uint32_t version{protocol_version};
    bool accepted{false};
    std::string topic;
    role topic_role{role::subscriber};
    std::optional<topic_mapping> mapping;
    std::optional<fd_bundle> fds;
    std::optional<join_state> state;
    std::optional<join_cursor> cursor;
    std::optional<std::string> retained_payload;
    /// Coordinator-assigned subscriber id, present for subscriber-capable roles.
    std::optional<std::uint32_t> subscriber_id;
    std::optional<error_code> error;
    std::optional<std::string> message;
};

/// \brief Request the server's active topic names.
struct list_topics_request {
    std::uint32_t version{protocol_version};
    std::string client_id;
};

/// \brief Response containing the server's active topic names.
struct list_topics_response {
    std::uint32_t version{protocol_version};
    bool accepted{false};
    std::vector<std::string> topics;
    std::optional<error_code> error;
    std::optional<std::string> message;
};

/// \brief Request removal of one client subscription.
struct unsubscribe_request {
    std::uint32_t version{protocol_version};
    std::string client_id;
    std::string topic;
};

/// \brief Response to an unsubscribe request.
struct unsubscribe_response {
    std::uint32_t version{protocol_version};
    bool accepted{false};
    std::string topic;
    std::optional<error_code> error;
    std::optional<std::string> message;
};

std::string to_string(request_kind value);
std::string to_string(role value);
std::string to_string(join_state value);
std::string to_string(error_code value);

request_kind request_kind_from_string(const std::string& value);
role role_from_string(const std::string& value);
join_state join_state_from_string(const std::string& value);
error_code error_code_from_string(const std::string& value);

/// \brief Inspect a control message JSON object and return its declared request_kind.
///
/// Messages without a "kind" field default to request_kind::handshake to preserve compatibility
/// with handshake-only clients that pre-date the topic discovery path. Unknown values throw.
request_kind request_kind_of(const nlohmann::json& json);

void to_json(nlohmann::json& json, const fd_bundle& value);
void from_json(const nlohmann::json& json, fd_bundle& value);

void to_json(nlohmann::json& json, const topic_mapping& value);
void from_json(const nlohmann::json& json, topic_mapping& value);

void to_json(nlohmann::json& json, const join_cursor& value);
void from_json(const nlohmann::json& json, join_cursor& value);

void to_json(nlohmann::json& json, const handshake_request& value);
void from_json(const nlohmann::json& json, handshake_request& value);

void to_json(nlohmann::json& json, const handshake_response& value);
void from_json(const nlohmann::json& json, handshake_response& value);

void to_json(nlohmann::json& json, const list_topics_request& value);
void from_json(const nlohmann::json& json, list_topics_request& value);

void to_json(nlohmann::json& json, const list_topics_response& value);
void from_json(const nlohmann::json& json, list_topics_response& value);

void to_json(nlohmann::json& json, const unsubscribe_request& value);
void from_json(const nlohmann::json& json, unsubscribe_request& value);

void to_json(nlohmann::json& json, const unsubscribe_response& value);
void from_json(const nlohmann::json& json, unsubscribe_response& value);

} // namespace everest::lib::io::shm::control
