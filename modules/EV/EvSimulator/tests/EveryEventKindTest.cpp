// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Every state must survive every event kind.
//
// EventPayload carries exactly one alternative per EventKind and the module asserts that at
// compile time, so a `std::get` in a case label group shared by several kinds is wrong for all
// but one of them. It throws bad_variant_access, the runtime catches it and reroots the machine
// at Faulted, and what an operator sees is a session that faults when they ask it a question.
//
// Nothing else catches that: a state's own suite feeds the kinds that state is about, which is
// exactly the set whose payload is right. This feeds all of them to all of them.

#include "../main/Events.hpp"
#include "../main/states/BcbToggling.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/ChargingPwmPaused.hpp"
#include "../main/states/Disabled.hpp"
#include "../main/states/Faulted.hpp"
#include "../main/states/Paused.hpp"
#include "../main/states/Plugged.hpp"
#include "../main/states/SlacMatching.hpp"
#include "../main/states/Stopping.hpp"
#include "../main/states/Unplugged.hpp"
#include "../main/states/V2GNegotiating.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

// Every value of EventKind. Listed rather than derived so that adding a kind without adding it
// here is visible: the count check below fails.
constexpr EventKind ALL_KINDS[] = {
    EventKind::Enable,
    EventKind::Disable,
    EventKind::Plug,
    EventKind::Unplug,
    EventKind::SetSoc,
    EventKind::ConfigureSession,
    EventKind::StopSession,
    EventKind::PauseSession,
    EventKind::ResumeSession,
    EventKind::SetChargingCurrent,
    EventKind::SetPresentValues,
    EventKind::InjectFault,
    EventKind::ClearFault,
    EventKind::BcbToggle,
    EventKind::RunScenario,
    EventKind::RaiseError,
    EventKind::ClearError,
    EventKind::QueryState,
    EventKind::BspEvent,
    EventKind::BspMeasurement,
    EventKind::EvInfo,
    EventKind::SlacState,
    EventKind::IsoPowerReady,
    EventKind::IsoAcMaxCurrent,
    EventKind::IsoAcTargetPower,
    EventKind::IsoStopFromCharger,
    EventKind::IsoV2GFinished,
    EventKind::IsoDcPowerOn,
    EventKind::IsoPauseFromCharger,
    EventKind::DcEvsePresentCurrent,
    EventKind::DcEvsePresentVoltage,
    EventKind::V2gMessage,
    EventKind::BeginSession,
    EventKind::StateDeadline,
    EventKind::Shutdown,
};

static_assert(std::size(ALL_KINDS) == std::variant_size_v<EventPayload>,
              "ALL_KINDS must list every EventKind; EventPayload has one alternative per kind");

} // namespace

TEST_CASE("Every state answers every event kind without throwing", "[evsim][events]") {
    TestFixture fx;

    const auto feed_all = [&](const char* state_name, auto make_state) {
        for (const auto kind : ALL_KINDS) {
            auto ctx = fx.make_ctx();
            auto state = make_state(*ctx);
            // Event{kind} builds the alternative that kind is paired with, which is the only one
            // the runtime can ever deliver for it. A state reading a different alternative throws.
            INFO(state_name << " fed EventKind " << static_cast<int>(kind));
            REQUIRE_NOTHROW(state->feed(Event{kind}));
        }
    };

    feed_all("Disabled", [](FsmContext& c) { return std::make_unique<Disabled>(c); });
    feed_all("Unplugged", [](FsmContext& c) { return std::make_unique<Unplugged>(c); });
    feed_all("Plugged", [](FsmContext& c) { return std::make_unique<Plugged>(c); });
    feed_all("SlacMatching", [](FsmContext& c) { return std::make_unique<SlacMatching>(c); });
    feed_all("V2GNegotiating", [](FsmContext& c) { return std::make_unique<V2GNegotiating>(c); });
    feed_all("Charging", [](FsmContext& c) { return std::make_unique<Charging>(c); });
    feed_all("ChargingPwmPaused", [](FsmContext& c) { return std::make_unique<ChargingPwmPaused>(c); });
    feed_all("Paused", [](FsmContext& c) { return std::make_unique<Paused>(c); });
    feed_all("BcbToggling", [](FsmContext& c) { return std::make_unique<BcbToggling>(c); });
    feed_all("Stopping", [](FsmContext& c) { return std::make_unique<Stopping>(c); });
    feed_all("Faulted", [](FsmContext& c) { return std::make_unique<Faulted>(c); });
}
