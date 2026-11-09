// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Per-command malformed-payload decode contract for the m2e router.
//
// setup_command_router binds against a live framework EvController&, so it is not
// unit-constructible; its catch -> Rejected-ack -> drop glue is covered by the SIL
// smokes. What is pinned here is the decision it branches on: a malformed, wrong-type
// or empty payload must make adl_deserialize return false. A codec that silently
// accepted garbage would admit a half-populated struct with no Rejected ack.

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_controller/API.hpp>
#include <everest_api_types/ev_controller/codec.hpp>
#include <everest_api_types/generic/API.hpp>
#include <everest_api_types/generic/codec.hpp>

#include <string>

namespace api = everest::lib::API::V1_0::types::ev_controller;
namespace api_generic = everest::lib::API::V1_0::types::generic;

namespace {

// Mirrors the router's decode step: returns the bool the handler
// branches on (false -> throw -> Rejected ack -> drop).
template <class T> bool decodes(const std::string& payload) {
    T obj{};
    return api::adl_deserialize(payload, obj);
}

template <class T> bool decodes_generic(const std::string& payload) {
    T obj{};
    return api_generic::adl_deserialize(payload, obj);
}

} // namespace

TEST_CASE("Command router decode contract: malformed payloads are rejected", "[evctl][command_router]") {
    // Each SECTION is one m2e command topic the router subscribes. The
    // three malformed shapes match the router's failure modes: a
    // wrong-type JSON document, a truncated/garbage document, and an
    // empty payload.
    const std::string truncated = "{not valid json";
    const std::string empty = "";
    const std::string wrong_type = "true"; // a bare bool where an object is expected

    SECTION("enable / disable / communication_check decode a bare bool") {
        // enable + communication_check route a bool through the generic
        // codec. A non-bool document must be rejected.
        CHECK(decodes_generic<bool>("true"));
        CHECK(decodes_generic<bool>("false"));
        CHECK_FALSE(decodes_generic<bool>(truncated));
        CHECK_FALSE(decodes_generic<bool>(empty));
        CHECK_FALSE(decodes_generic<bool>("{\"enable\":true}"));
    }

    SECTION("set_soc rejects malformed SetSocParams") {
        CHECK(decodes<api::SetSocParams>("{\"soc_pct\":42.0}"));
        CHECK_FALSE(decodes<api::SetSocParams>(truncated));
        CHECK_FALSE(decodes<api::SetSocParams>(empty));
        CHECK_FALSE(decodes<api::SetSocParams>("{\"soc_pct\":\"high\"}"));
    }

    SECTION("configure_session rejects malformed SessionConfigParams") {
        CHECK(decodes<api::SessionConfigParams>("{\"mode\":\"AcIec\"}"));
        CHECK_FALSE(decodes<api::SessionConfigParams>(truncated));
        CHECK_FALSE(decodes<api::SessionConfigParams>(empty));
        CHECK_FALSE(decodes<api::SessionConfigParams>(wrong_type));
        // A structurally valid object with an unknown mode enum value
        // must not silently decode to a default alternative.
        CHECK_FALSE(decodes<api::SessionConfigParams>("{\"mode\":\"NotAMode\"}"));
    }

    SECTION("set_charging_current rejects malformed SetChargingCurrentParams") {
        CHECK(decodes<api::SetChargingCurrentParams>("{\"current_a\":16.0,\"three_phases\":true}"));
        CHECK_FALSE(decodes<api::SetChargingCurrentParams>(truncated));
        CHECK_FALSE(decodes<api::SetChargingCurrentParams>(empty));
        CHECK_FALSE(decodes<api::SetChargingCurrentParams>(wrong_type));
    }

    SECTION("bcb_toggle rejects malformed BcbToggleParams") {
        CHECK(decodes<api::BcbToggleParams>("{}"));
        CHECK(decodes<api::BcbToggleParams>("{\"count\":3}"));
        CHECK_FALSE(decodes<api::BcbToggleParams>(truncated));
        CHECK_FALSE(decodes<api::BcbToggleParams>(empty));
        CHECK_FALSE(decodes<api::BcbToggleParams>("{\"count\":\"three\"}"));
    }

    SECTION("raise_error / clear_error reject a malformed generic Error") {
        CHECK(decodes_generic<api_generic::Error>("{\"type\":\"VendorError\"}"));
        CHECK_FALSE(decodes_generic<api_generic::Error>(truncated));
        CHECK_FALSE(decodes_generic<api_generic::Error>(empty));
        CHECK_FALSE(decodes_generic<api_generic::Error>(wrong_type));
    }
}
