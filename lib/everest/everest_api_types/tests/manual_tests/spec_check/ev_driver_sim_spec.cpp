// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "SpecDrift.hpp"

#include <everest_api_types/ev_driver_sim/API.hpp>
#include <everest_api_types/ev_driver_sim/codec.hpp>
#include <everest_api_types/generic/codec.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

namespace api = everest::lib::API::V1_0::types::ev_driver_sim;
namespace generic = everest::lib::API::V1_0::types::generic;
using spec_drift::json;

template <class T> json round_trip(json const& payload) {
    T value{};
    if (not api::adl_deserialize(payload.dump(), value)) {
        throw std::invalid_argument("deserialization failed");
    }
    return json::parse(api::serialize(value));
}

template <class T> json generic_round_trip(json const& payload) {
    T value{};
    if (not generic::adl_deserialize(payload.dump(), value)) {
        throw std::invalid_argument("deserialization failed");
    }
    return json::parse(generic::serialize(value));
}

template <class T> json codec(T const& value) {
    return json::parse(api::serialize(value));
}

template <class E> std::vector<json> values() {
    return spec_drift::enum_values<E>([](E value) { return api::serialize(value); });
}

spec_drift::Channels channels() {
    const api::ScenarioTimingOverrides timing{1000, 2000, 3000, 4000, 5000, 6000};
    const std::vector<json> flags{json::parse(generic::serialize(true)), json::parse(generic::serialize(false))};

    return {
        // No codec: the module ignores the payload of a command without arguments.
        {"m2e/plug", {nullptr, {json::object()}}},
        {"m2e/unplug", {nullptr, {json::object()}}},
        {"m2e/inject_fault",
         {round_trip<api::InjectFaultParams>,
          {codec(api::InjectFaultParams{api::WorldFault::RcdError, 30.0F, "message"})}}},
        {"m2e/clear_fault", {nullptr, {json::object()}}},
        {"m2e/run_scenario",
         {round_trip<api::RunScenarioParams>,
          {codec(api::RunScenarioParams{api::ScenarioName::DiodeFailSmoke, timing})}}},
        {"m2e/communication_check", {generic_round_trip<bool>, flags}},
        {"e2m/world_state",
         {round_trip<api::WorldState>, {codec(api::WorldState{api::PilotState::Passed, api::WorldFault::DiodeFail})}}},
        {"e2m/command_ack",
         {round_trip<api::CommandAck>,
          {codec(api::CommandAck{"inject_fault", api::CommandAckStatus::Rejected, "unplugged"})}}},
        {"e2m/scenario_event",
         {round_trip<api::ScenarioEvent>,
          {codec(api::ScenarioEvent{api::ScenarioName::AcIecBasic, api::ScenarioEventKind::StepFired, "plug"})}}},
        {"e2m/heartbeat", {generic_round_trip<std::size_t>, {json::parse(generic::serialize(std::size_t{42}))}}},
    };
}

spec_drift::Enums enums() {
    return {
        {"WorldFault", values<api::WorldFault>()},
        {"PilotState", values<api::PilotState>()},
        {"CommandAckStatus", values<api::CommandAckStatus>()},
        {"ScenarioName", values<api::ScenarioName>()},
        {"ScenarioEventKind", values<api::ScenarioEventKind>()},
    };
}

TEST(ApiSpecifications, ev_driver_sim_spec_matches_its_codec) {
    const auto spec = std::filesystem::path{EVEREST_API_SPEC_DIR} / "ev_driver_sim_API.yaml";
    for (auto const& error : spec_drift::check_api(spec, channels(), enums())) {
        ADD_FAILURE() << error;
    }
}

} // namespace
