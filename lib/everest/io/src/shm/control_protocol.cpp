// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/control_protocol.hpp>

#include <stdexcept>

namespace everest::lib::io::shm::control {
namespace {

void put_optional_fd(nlohmann::json& json, const char* key, const std::optional<std::uint32_t>& value) {
    if (value.has_value()) {
        json[key] = value.value();
    }
}

void get_optional_fd(const nlohmann::json& json, const char* key, std::optional<std::uint32_t>& value) {
    if (json.contains(key)) {
        value = json.at(key).get<std::uint32_t>();
    } else {
        value.reset();
    }
}

} // namespace

std::string to_string(request_kind value) {
    switch (value) {
    case request_kind::handshake:
        return "handshake";
    case request_kind::list_topics:
        return "list_topics";
    case request_kind::unsubscribe:
        return "unsubscribe";
    }
    throw std::invalid_argument("Unknown SHM control request kind");
}

request_kind request_kind_from_string(const std::string& value) {
    if (value == "handshake") {
        return request_kind::handshake;
    }
    if (value == "list_topics") {
        return request_kind::list_topics;
    }
    if (value == "unsubscribe") {
        return request_kind::unsubscribe;
    }
    throw std::invalid_argument("Unknown SHM control request kind: " + value);
}

request_kind request_kind_of(const nlohmann::json& json) {
    if (!json.contains("kind")) {
        return request_kind::handshake;
    }
    return request_kind_from_string(json.at("kind").get<std::string>());
}

std::string to_string(role value) {
    switch (value) {
    case role::publisher:
        return "publisher";
    case role::subscriber:
        return "subscriber";
    case role::publisher_subscriber:
        return "publisher_subscriber";
    }
    throw std::invalid_argument("Unknown SHM control role");
}

std::string to_string(join_state value) {
    switch (value) {
    case join_state::active:
        return "active";
    case join_state::pending:
        return "pending";
    }
    throw std::invalid_argument("Unknown SHM control join state");
}

std::string to_string(error_code value) {
    switch (value) {
    case error_code::incompatible_version:
        return "incompatible_version";
    case error_code::unknown_topic:
        return "unknown_topic";
    case error_code::invalid_role:
        return "invalid_role";
    case error_code::resource_error:
        return "resource_error";
    }
    throw std::invalid_argument("Unknown SHM control error code");
}

role role_from_string(const std::string& value) {
    if (value == "publisher") {
        return role::publisher;
    }
    if (value == "subscriber") {
        return role::subscriber;
    }
    if (value == "publisher_subscriber") {
        return role::publisher_subscriber;
    }
    throw std::invalid_argument("Unknown SHM control role: " + value);
}

join_state join_state_from_string(const std::string& value) {
    if (value == "active") {
        return join_state::active;
    }
    if (value == "pending") {
        return join_state::pending;
    }
    throw std::invalid_argument("Unknown SHM control join state: " + value);
}

error_code error_code_from_string(const std::string& value) {
    if (value == "incompatible_version") {
        return error_code::incompatible_version;
    }
    if (value == "unknown_topic") {
        return error_code::unknown_topic;
    }
    if (value == "invalid_role") {
        return error_code::invalid_role;
    }
    if (value == "resource_error") {
        return error_code::resource_error;
    }
    throw std::invalid_argument("Unknown SHM control error code: " + value);
}

void to_json(nlohmann::json& json, const fd_bundle& value) {
    json = nlohmann::json::object();
    put_optional_fd(json, "publication", value.publication);
    put_optional_fd(json, "release", value.release);
    put_optional_fd(json, "broadcast", value.broadcast);
    put_optional_fd(json, "ack", value.ack);
}

void from_json(const nlohmann::json& json, fd_bundle& value) {
    get_optional_fd(json, "publication", value.publication);
    get_optional_fd(json, "release", value.release);
    get_optional_fd(json, "broadcast", value.broadcast);
    get_optional_fd(json, "ack", value.ack);
}

void to_json(nlohmann::json& json, const topic_mapping& value) {
    json = nlohmann::json{{"shm_name", value.shm_name},
                          {"ring_offset", value.ring_offset},
                          {"total_slots", value.total_slots},
                          {"slot_size", value.slot_size}};
}

void from_json(const nlohmann::json& json, topic_mapping& value) {
    value.shm_name = json.at("shm_name").get<std::string>();
    value.ring_offset = json.at("ring_offset").get<std::uint64_t>();
    value.total_slots = json.at("total_slots").get<std::uint32_t>();
    value.slot_size = json.at("slot_size").get<std::uint32_t>();
}

void to_json(nlohmann::json& json, const join_cursor& value) {
    json = nlohmann::json{{"write_idx", value.write_idx}, {"sequence", value.sequence}};
}

void from_json(const nlohmann::json& json, join_cursor& value) {
    value.write_idx = json.at("write_idx").get<std::uint32_t>();
    value.sequence = json.at("sequence").get<std::uint64_t>();
}

void to_json(nlohmann::json& json, const handshake_request& value) {
    json = nlohmann::json{{"kind", to_string(request_kind::handshake)},
                          {"version", value.version},
                          {"client_id", value.client_id},
                          {"topic", value.topic},
                          {"role", to_string(value.topic_role)}};
}

void from_json(const nlohmann::json& json, handshake_request& value) {
    if (request_kind_of(json) != request_kind::handshake) {
        throw std::invalid_argument("SHM control message is not a handshake request");
    }
    value.version = json.at("version").get<std::uint32_t>();
    value.client_id = json.at("client_id").get<std::string>();
    value.topic = json.at("topic").get<std::string>();
    value.topic_role = role_from_string(json.at("role").get<std::string>());
}

void to_json(nlohmann::json& json, const handshake_response& value) {
    json = nlohmann::json{{"version", value.version},
                          {"accepted", value.accepted},
                          {"topic", value.topic},
                          {"role", to_string(value.topic_role)}};
    if (value.mapping.has_value()) {
        json["mapping"] = value.mapping.value();
    }
    if (value.fds.has_value()) {
        json["fds"] = value.fds.value();
    }
    if (value.state.has_value()) {
        json["join_state"] = to_string(value.state.value());
    }
    if (value.cursor.has_value()) {
        json["cursor"] = value.cursor.value();
    }
    if (value.retained_payload.has_value()) {
        json["retained_payload"] = value.retained_payload.value();
    }
    if (value.subscriber_id.has_value()) {
        json["subscriber_id"] = value.subscriber_id.value();
    }
    if (value.error.has_value()) {
        json["error"] = to_string(value.error.value());
    }
    if (value.message.has_value()) {
        json["message"] = value.message.value();
    }
}

void from_json(const nlohmann::json& json, handshake_response& value) {
    value.version = json.at("version").get<std::uint32_t>();
    value.accepted = json.at("accepted").get<bool>();
    value.topic = json.at("topic").get<std::string>();
    value.topic_role = role_from_string(json.at("role").get<std::string>());
    if (json.contains("mapping")) {
        value.mapping = json.at("mapping").get<topic_mapping>();
    } else {
        value.mapping.reset();
    }
    if (json.contains("fds")) {
        value.fds = json.at("fds").get<fd_bundle>();
    } else {
        value.fds.reset();
    }
    if (json.contains("join_state")) {
        value.state = join_state_from_string(json.at("join_state").get<std::string>());
    } else {
        value.state.reset();
    }
    if (json.contains("cursor")) {
        value.cursor = json.at("cursor").get<join_cursor>();
    } else {
        value.cursor.reset();
    }
    if (json.contains("retained_payload")) {
        value.retained_payload = json.at("retained_payload").get<std::string>();
    } else {
        value.retained_payload.reset();
    }
    if (json.contains("subscriber_id")) {
        value.subscriber_id = json.at("subscriber_id").get<std::uint32_t>();
    } else {
        value.subscriber_id.reset();
    }
    if (json.contains("error")) {
        value.error = error_code_from_string(json.at("error").get<std::string>());
    } else {
        value.error.reset();
    }
    if (json.contains("message")) {
        value.message = json.at("message").get<std::string>();
    } else {
        value.message.reset();
    }
}

void to_json(nlohmann::json& json, const list_topics_request& value) {
    json = nlohmann::json{
        {"kind", to_string(request_kind::list_topics)}, {"version", value.version}, {"client_id", value.client_id}};
}

void from_json(const nlohmann::json& json, list_topics_request& value) {
    if (request_kind_of(json) != request_kind::list_topics) {
        throw std::invalid_argument("SHM control message is not a list_topics request");
    }
    value.version = json.at("version").get<std::uint32_t>();
    value.client_id = json.at("client_id").get<std::string>();
}

void to_json(nlohmann::json& json, const list_topics_response& value) {
    json = nlohmann::json{{"kind", to_string(request_kind::list_topics)},
                          {"version", value.version},
                          {"accepted", value.accepted},
                          {"topics", value.topics}};
    if (value.error.has_value()) {
        json["error"] = to_string(value.error.value());
    }
    if (value.message.has_value()) {
        json["message"] = value.message.value();
    }
}

void from_json(const nlohmann::json& json, list_topics_response& value) {
    if (request_kind_of(json) != request_kind::list_topics) {
        throw std::invalid_argument("SHM control message is not a list_topics response");
    }
    value.version = json.at("version").get<std::uint32_t>();
    value.accepted = json.at("accepted").get<bool>();
    if (json.contains("topics")) {
        value.topics = json.at("topics").get<std::vector<std::string>>();
    } else {
        value.topics.clear();
    }
    if (json.contains("error")) {
        value.error = error_code_from_string(json.at("error").get<std::string>());
    } else {
        value.error.reset();
    }
    if (json.contains("message")) {
        value.message = json.at("message").get<std::string>();
    } else {
        value.message.reset();
    }
}

void to_json(nlohmann::json& json, const unsubscribe_request& value) {
    json = nlohmann::json{{"kind", to_string(request_kind::unsubscribe)},
                          {"version", value.version},
                          {"client_id", value.client_id},
                          {"topic", value.topic}};
}

void from_json(const nlohmann::json& json, unsubscribe_request& value) {
    if (request_kind_of(json) != request_kind::unsubscribe) {
        throw std::invalid_argument("SHM control message is not an unsubscribe request");
    }
    value.version = json.at("version").get<std::uint32_t>();
    value.client_id = json.at("client_id").get<std::string>();
    value.topic = json.at("topic").get<std::string>();
}

void to_json(nlohmann::json& json, const unsubscribe_response& value) {
    json = nlohmann::json{{"kind", to_string(request_kind::unsubscribe)},
                          {"version", value.version},
                          {"accepted", value.accepted},
                          {"topic", value.topic}};
    if (value.error.has_value()) {
        json["error"] = to_string(value.error.value());
    }
    if (value.message.has_value()) {
        json["message"] = value.message.value();
    }
}

void from_json(const nlohmann::json& json, unsubscribe_response& value) {
    if (request_kind_of(json) != request_kind::unsubscribe) {
        throw std::invalid_argument("SHM control message is not an unsubscribe response");
    }
    value.version = json.at("version").get<std::uint32_t>();
    value.accepted = json.at("accepted").get<bool>();
    value.topic = json.at("topic").get<std::string>();
    if (json.contains("error")) {
        value.error = error_code_from_string(json.at("error").get<std::string>());
    } else {
        value.error.reset();
    }
    if (json.contains("message")) {
        value.message = json.at("message").get<std::string>();
    } else {
        value.message.reset();
    }
}

} // namespace everest::lib::io::shm::control
