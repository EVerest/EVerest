// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include <everest/io/shm/control_protocol.hpp>

using namespace everest::lib::io::shm::control;

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

void test_handshake_request_roundtrip() {
    const handshake_request request{protocol_version, "module-a", "everest/module-a/var/state",
                                    role::publisher_subscriber};

    const nlohmann::json json = request;
    require(json.at("version") == protocol_version, "request version should serialize");
    require(json.at("client_id") == "module-a", "request client id should serialize");
    require(json.at("topic") == "everest/module-a/var/state", "request topic should serialize");
    require(json.at("role") == "publisher_subscriber", "request role should serialize");

    const auto parsed = json.get<handshake_request>();
    require(parsed.version == protocol_version, "request version should roundtrip");
    require(parsed.client_id == request.client_id, "request client id should roundtrip");
    require(parsed.topic == request.topic, "request topic should roundtrip");
    require(parsed.topic_role == role::publisher_subscriber, "request role should roundtrip");
}

void test_active_handshake_response_roundtrip() {
    handshake_response response;
    response.accepted = true;
    response.topic = "everest/module-a/var/state";
    response.topic_role = role::subscriber;
    response.mapping = topic_mapping{"/everest-shm-manager", 4096, 8, 8192};
    // T-4.009: server_liveness FD index is gone — the persistent control connection is
    // the liveness channel. Subscriber-capable handshakes ship broadcast (index 0) and
    // ack (index 1) FDs only.
    response.fds = fd_bundle{std::nullopt, std::nullopt, 0, 1};
    response.state = join_state::active;
    response.cursor = join_cursor{3, 42};

    const nlohmann::json json = response;
    require(json.at("accepted") == true, "accepted response should serialize accepted flag");
    require(json.at("mapping").at("shm_name") == "/everest-shm-manager", "accepted response should serialize shm name");
    require(json.at("mapping").at("ring_offset") == 4096, "accepted response should serialize ring offset");
    require(json.at("mapping").at("total_slots") == 8, "accepted response should serialize total slots");
    require(json.at("mapping").at("slot_size") == 8192, "accepted response should serialize slot size");
    require(!json.at("fds").contains("publication"), "subscriber response should omit publication fd");
    require(!json.at("fds").contains("server_liveness"),
            "T-4.009 subscriber response should omit the obsolete server liveness fd index");
    require(json.at("fds").at("broadcast") == 0, "subscriber response should serialize broadcast fd index");
    require(json.at("fds").at("ack") == 1, "subscriber response should serialize ack fd index");
    require(json.at("join_state") == "active", "accepted response should serialize join state");
    require(json.at("cursor").at("write_idx") == 3, "accepted response should serialize cursor write index");
    require(json.at("cursor").at("sequence") == 42, "accepted response should serialize cursor sequence");

    const auto parsed = json.get<handshake_response>();
    require(parsed.accepted, "accepted response should roundtrip accepted flag");
    require(parsed.mapping.has_value(), "accepted response should roundtrip mapping");
    require(parsed.mapping->shm_name == "/everest-shm-manager", "accepted response should roundtrip shm name");
    require(parsed.fds.has_value(), "accepted response should roundtrip fds");
    require(!parsed.fds->publication.has_value(), "accepted response should preserve missing publication fd");
    require(parsed.fds->broadcast == 0, "accepted response should roundtrip broadcast fd index");
    require(parsed.fds->ack == 1, "accepted response should roundtrip ack fd index");
    require(parsed.state == join_state::active, "accepted response should roundtrip active state");
    require(parsed.cursor.has_value(), "accepted response should roundtrip cursor");
    require(parsed.cursor->write_idx == 3, "accepted response should roundtrip cursor write index");
    require(parsed.cursor->sequence == 42, "accepted response should roundtrip cursor sequence");
}

void test_pending_publisher_response_roundtrip() {
    handshake_response response;
    response.accepted = true;
    response.topic = "everest/module-a/cmd/start";
    response.topic_role = role::publisher;
    response.mapping = topic_mapping{"/everest-shm-manager", 8192, 4, 4096};
    response.fds = fd_bundle{0, 1, std::nullopt, std::nullopt};
    response.state = join_state::pending;
    response.cursor = join_cursor{0, 0};

    const nlohmann::json json = response;
    require(json.at("role") == "publisher", "publisher response should serialize role");
    require(json.at("join_state") == "pending", "publisher response should serialize pending state");
    require(!json.at("fds").contains("server_liveness"),
            "T-4.009 publisher response should omit the obsolete server liveness fd index");
    require(json.at("fds").at("publication") == 0, "publisher response should serialize publication fd index");
    require(json.at("fds").at("release") == 1, "publisher response should serialize release fd index");
    require(!json.at("fds").contains("broadcast"), "publisher response should omit broadcast fd");
    require(!json.at("fds").contains("ack"), "publisher response should omit ack fd");

    const auto parsed = json.get<handshake_response>();
    require(parsed.topic_role == role::publisher, "publisher response should roundtrip role");
    require(parsed.state == join_state::pending, "publisher response should roundtrip pending state");
    require(parsed.fds->publication == 0, "publisher response should roundtrip publication fd");
    require(parsed.fds->release == 1, "publisher response should roundtrip release fd");
    require(!parsed.fds->broadcast.has_value(), "publisher response should preserve missing broadcast fd");
    require(!parsed.fds->ack.has_value(), "publisher response should preserve missing ack fd");
}

void test_protocol_version_is_t_4_010() {
    require(protocol_version == 6U, "T-4.011 bumps the SHM control protocol version to 6");
}

void test_error_response_roundtrip() {
    handshake_response response;
    response.accepted = false;
    response.topic = "everest/unknown";
    response.topic_role = role::subscriber;
    response.error = error_code::unknown_topic;
    response.message = "topic is not registered";

    const nlohmann::json json = response;
    require(json.at("accepted") == false, "error response should serialize accepted flag");
    require(json.at("error") == "unknown_topic", "error response should serialize error code");
    require(json.at("message") == "topic is not registered", "error response should serialize message");
    require(!json.contains("mapping"), "error response should omit mapping");
    require(!json.contains("fds"), "error response should omit fds");

    const auto parsed = json.get<handshake_response>();
    require(!parsed.accepted, "error response should roundtrip accepted flag");
    require(parsed.error == error_code::unknown_topic, "error response should roundtrip error code");
    require(parsed.message == "topic is not registered", "error response should roundtrip message");
    require(!parsed.mapping.has_value(), "error response should preserve missing mapping");
    require(!parsed.fds.has_value(), "error response should preserve missing fds");
}

void test_invalid_enum_strings_throw() {
    require_throws([]() { role_from_string("reader"); }, "invalid role should throw");
    require_throws([]() { join_state_from_string("ready"); }, "invalid join state should throw");
    require_throws([]() { error_code_from_string("bad"); }, "invalid error code should throw");
    require_throws([]() { request_kind_from_string("frob"); }, "invalid request kind should throw");
}

void test_handshake_serialization_carries_kind_field() {
    const handshake_request request{protocol_version, "module-a", "topic", role::subscriber};
    const nlohmann::json json = request;
    require(json.contains("kind"), "handshake request should serialize a kind discriminator");
    require(json.at("kind") == "handshake", "handshake request should serialize kind=handshake");
}

void test_handshake_parses_without_kind_for_backward_compat() {
    nlohmann::json json{
        {"version", protocol_version}, {"client_id", "module-a"}, {"topic", "topic"}, {"role", "subscriber"}};
    const auto parsed = json.get<handshake_request>();
    require(parsed.client_id == "module-a", "handshake without kind should roundtrip client id");
    require(parsed.topic == "topic", "handshake without kind should roundtrip topic");
    require(parsed.topic_role == role::subscriber, "handshake without kind should roundtrip role");
    require(request_kind_of(json) == request_kind::handshake,
            "handshake without kind should default to handshake discriminator");
}

void test_handshake_parse_rejects_list_topics_kind() {
    nlohmann::json json{{"kind", "list_topics"},
                        {"version", protocol_version},
                        {"client_id", "module-a"},
                        {"topic", "topic"},
                        {"role", "subscriber"}};
    require_throws([&]() { (void)json.get<handshake_request>(); },
                   "handshake parser should reject list_topics-tagged JSON");
}

void test_list_topics_request_roundtrip() {
    const list_topics_request request{protocol_version, "module-a"};
    const nlohmann::json json = request;
    require(json.at("kind") == "list_topics", "list_topics request should serialize kind");
    require(json.at("version") == protocol_version, "list_topics request should serialize version");
    require(json.at("client_id") == "module-a", "list_topics request should serialize client id");
    require(request_kind_of(json) == request_kind::list_topics,
            "list_topics request kind should be derivable from JSON");

    const auto parsed = json.get<list_topics_request>();
    require(parsed.version == protocol_version, "list_topics request should roundtrip version");
    require(parsed.client_id == "module-a", "list_topics request should roundtrip client id");
}

void test_list_topics_request_parse_rejects_handshake_kind() {
    nlohmann::json json{{"kind", "handshake"}, {"version", protocol_version}, {"client_id", "module-a"}};
    require_throws([&]() { (void)json.get<list_topics_request>(); },
                   "list_topics parser should reject handshake-tagged JSON");
}

void test_accepted_list_topics_response_roundtrip() {
    list_topics_response response;
    response.accepted = true;
    response.topics = {"topic/a", "topic/b", "topic/c"};

    const nlohmann::json json = response;
    require(json.at("kind") == "list_topics", "list_topics response should serialize kind");
    require(json.at("accepted") == true, "list_topics response should serialize accepted flag");
    require(json.at("topics").size() == 3, "list_topics response should serialize topic count");
    require(!json.contains("error"), "accepted list_topics response should omit error");

    const auto parsed = json.get<list_topics_response>();
    require(parsed.accepted, "list_topics response should roundtrip accepted flag");
    require(parsed.topics == response.topics, "list_topics response should roundtrip topic list");
    require(!parsed.error.has_value(), "accepted list_topics response should preserve missing error");
}

void test_empty_list_topics_response_roundtrip() {
    list_topics_response response;
    response.accepted = true;

    const nlohmann::json json = response;
    require(json.at("topics").empty(), "empty list_topics response should serialize empty topics array");

    const auto parsed = json.get<list_topics_response>();
    require(parsed.accepted, "empty list_topics response should roundtrip accepted flag");
    require(parsed.topics.empty(), "empty list_topics response should roundtrip empty topic list");
}

void test_rejected_list_topics_response_roundtrip() {
    list_topics_response response;
    response.accepted = false;
    response.error = error_code::incompatible_version;
    response.message = "incompatible";

    const nlohmann::json json = response;
    require(json.at("accepted") == false, "rejected list_topics response should serialize accepted flag");
    require(json.at("error") == "incompatible_version", "rejected list_topics response should serialize error");
    require(json.at("message") == "incompatible", "rejected list_topics response should serialize message");

    const auto parsed = json.get<list_topics_response>();
    require(!parsed.accepted, "rejected list_topics response should roundtrip accepted flag");
    require(parsed.error == error_code::incompatible_version, "rejected list_topics response should roundtrip error");
    require(parsed.message == "incompatible", "rejected list_topics response should roundtrip message");
    require(parsed.topics.empty(), "rejected list_topics response should not carry topics");
}

int main() {
    test_protocol_version_is_t_4_010();
    test_handshake_request_roundtrip();
    test_active_handshake_response_roundtrip();
    test_pending_publisher_response_roundtrip();
    test_error_response_roundtrip();
    test_invalid_enum_strings_throw();
    test_handshake_serialization_carries_kind_field();
    test_handshake_parses_without_kind_for_backward_compat();
    test_handshake_parse_rejects_list_topics_kind();
    test_list_topics_request_roundtrip();
    test_list_topics_request_parse_rejects_handshake_kind();
    test_accepted_list_topics_response_roundtrip();
    test_empty_list_topics_response_roundtrip();
    test_rejected_list_topics_response_roundtrip();

    return 0;
}
