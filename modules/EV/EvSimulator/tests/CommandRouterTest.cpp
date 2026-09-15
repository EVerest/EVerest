// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Per-command malformed-payload decode contract for the m2e router.
//
// setup_command_router binds against a live framework EvSimulator&, so it is not
// unit-constructible; its catch -> Rejected-ack -> drop glue is covered by the SIL
// smokes. What is pinned here is the decision it branches on: a malformed, wrong-type
// or empty payload must make adl_deserialize return false. A codec that silently
// accepted garbage would admit a half-populated struct with no Rejected ack.

#include <catch2/catch_test_macros.hpp>
#include <everest_api_types/ev_simulator/API.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>
#include <everest_api_types/generic/API.hpp>
#include <everest_api_types/generic/codec.hpp>

#include <string>

namespace api = everest::lib::API::V1_0::types::ev_simulator;
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

TEST_CASE("Command router decode contract: malformed payloads are rejected", "[evsim][command_router]") {
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

    SECTION("inject_fault rejects malformed InjectFaultParams") {
        CHECK(decodes<api::InjectFaultParams>("{\"type\":\"DiodeFail\"}"));
        CHECK_FALSE(decodes<api::InjectFaultParams>(truncated));
        CHECK_FALSE(decodes<api::InjectFaultParams>(empty));
        CHECK_FALSE(decodes<api::InjectFaultParams>("{\"type\":\"NotAFault\"}"));
    }

    SECTION("bcb_toggle rejects malformed BcbToggleParams") {
        CHECK(decodes<api::BcbToggleParams>("{}"));
        CHECK(decodes<api::BcbToggleParams>("{\"count\":3}"));
        CHECK_FALSE(decodes<api::BcbToggleParams>(truncated));
        CHECK_FALSE(decodes<api::BcbToggleParams>(empty));
        CHECK_FALSE(decodes<api::BcbToggleParams>("{\"count\":\"three\"}"));
    }

    SECTION("run_scenario rejects malformed RunScenarioParams") {
        CHECK(decodes<api::RunScenarioParams>("{\"name\":\"AcIecBasic\"}"));
        CHECK_FALSE(decodes<api::RunScenarioParams>(truncated));
        CHECK_FALSE(decodes<api::RunScenarioParams>(empty));
        CHECK_FALSE(decodes<api::RunScenarioParams>("{\"name\":\"NoSuchScenario\"}"));
        // Known, library-wide leniency: a non-object `timing` is not
        // rejected. ScenarioTimingOverrides::from_json uses the same
        // contains+emplace idiom as BcbToggleParams (no is_object()
        // guard), so a scalar `timing` decodes to an all-nullopt
        // override rather than failing. This is intentional and matches
        // the established codec idiom; a one-off is_object() throw here
        // would diverge from it and is out of WS-A scope.
        CHECK(decodes<api::RunScenarioParams>(R"({"name":"AcIecBasic","timing":42})"));
    }

    SECTION("raise_error / clear_error reject a malformed generic Error") {
        CHECK(decodes_generic<api_generic::Error>("{\"type\":\"VendorError\"}"));
        CHECK_FALSE(decodes_generic<api_generic::Error>(truncated));
        CHECK_FALSE(decodes_generic<api_generic::Error>(empty));
        CHECK_FALSE(decodes_generic<api_generic::Error>(wrong_type));
    }
}

// Positive-path codec value fidelity for ScenarioTimingOverrides and the
// RunScenarioParams.timing it nests in. Distinct from the malformed-payload
// contract above: these sections assert that set/unset fields survive
// decode and a serialize -> deserialize round-trip with their exact values
// (and that an absent or empty override stays all-nullopt), independent of
// the accept/reject decision the router branches on.
TEST_CASE("ScenarioTimingOverrides codec preserves field values across round-trip", "[evsim][command_router]") {
    SECTION("all timing fields decode to their exact values") {
        const std::string with_all_timing = R"({
            "name": "AcIecBasic",
            "timing": {
                "pause_at_ms": 1000,
                "resume_at_ms": 3000,
                "stop_after_ms": 5000,
                "unplug_after_ms": 6000,
                "fault_at_ms": 2000,
                "clear_fault_at_ms": 4000
            }
        })";
        CHECK(decodes<api::RunScenarioParams>(with_all_timing));

        api::RunScenarioParams params_full{};
        REQUIRE(api::adl_deserialize(with_all_timing, params_full));
        REQUIRE(params_full.timing.has_value());
        CHECK(params_full.timing->pause_at_ms == 1000);
        CHECK(params_full.timing->resume_at_ms == 3000);
        CHECK(params_full.timing->stop_after_ms == 5000);
        CHECK(params_full.timing->unplug_after_ms == 6000);
        CHECK(params_full.timing->fault_at_ms == 2000);
        CHECK(params_full.timing->clear_fault_at_ms == 4000);

        // Round-trip serialize -> deserialize preserves set and unset fields.
        const std::string serialized = api::serialize(params_full);
        auto params_rt = api::deserialize<api::RunScenarioParams>(serialized);
        REQUIRE(params_rt.timing.has_value());
        CHECK(params_rt.timing->pause_at_ms == 1000);
        CHECK(params_rt.timing->resume_at_ms == 3000);
        CHECK(params_rt.timing->stop_after_ms == 5000);
        CHECK(params_rt.timing->unplug_after_ms == 6000);
        CHECK(params_rt.timing->fault_at_ms == 2000);
        CHECK(params_rt.timing->clear_fault_at_ms == 4000);
    }

    SECTION("absent timing decodes to nullopt") {
        api::RunScenarioParams params_no_timing{};
        REQUIRE(api::adl_deserialize("{\"name\":\"AcIecBasic\"}", params_no_timing));
        CHECK_FALSE(params_no_timing.timing.has_value());
    }

    SECTION("partial timing leaves unset fields nullopt") {
        const std::string with_partial_timing = R"({"name":"DcIsoBasic","timing":{"stop_after_ms":8000}})";
        api::RunScenarioParams params_partial{};
        REQUIRE(api::adl_deserialize(with_partial_timing, params_partial));
        REQUIRE(params_partial.timing.has_value());
        CHECK_FALSE(params_partial.timing->pause_at_ms.has_value());
        CHECK(params_partial.timing->stop_after_ms == 8000);
    }

    SECTION("ScenarioTimingOverrides round-trips in isolation") {
        api::ScenarioTimingOverrides timing_only{};
        timing_only.pause_at_ms = 500;
        timing_only.fault_at_ms = 1500;
        const std::string timing_serialized = api::serialize(timing_only);
        auto timing_rt = api::deserialize<api::ScenarioTimingOverrides>(timing_serialized);
        CHECK(timing_rt.pause_at_ms == 500);
        CHECK_FALSE(timing_rt.resume_at_ms.has_value());
        CHECK(timing_rt.fault_at_ms == 1500);
        CHECK_FALSE(timing_rt.clear_fault_at_ms.has_value());
    }

    SECTION("all-nullopt ScenarioTimingOverrides stays all-nullopt across round-trip") {
        api::ScenarioTimingOverrides empty_overrides{};
        const std::string serialized = api::serialize(empty_overrides);
        auto rt = api::deserialize<api::ScenarioTimingOverrides>(serialized);
        CHECK_FALSE(rt.pause_at_ms.has_value());
        CHECK_FALSE(rt.resume_at_ms.has_value());
        CHECK_FALSE(rt.stop_after_ms.has_value());
        CHECK_FALSE(rt.unplug_after_ms.has_value());
        CHECK_FALSE(rt.fault_at_ms.has_value());
        CHECK_FALSE(rt.clear_fault_at_ms.has_value());
    }
}
