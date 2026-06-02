// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Tests for kind_of(): the single mapping from a payload variant alternative
// to its EventKind discriminant. Exhaustive — one assertion per alternative.
// Since the variant is the event's only identity, a round-trip through
// kind_of() is the contract every producer/consumer relies on.

#include "../main/Events.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace module;

namespace api = API_types::ev_simulator;

TEST_CASE("kind_of maps every variant alternative to its EventKind", "[evsim][events]") {
    CHECK(kind_of(Event{EnableCmd{}}) == EventKind::Enable);
    CHECK(kind_of(Event{DisableCmd{}}) == EventKind::Disable);
    CHECK(kind_of(Event{PlugCmd{}}) == EventKind::Plug);
    CHECK(kind_of(Event{UnplugCmd{}}) == EventKind::Unplug);
    CHECK(kind_of(Event{api::SetSocParams{}}) == EventKind::SetSoc);
    CHECK(kind_of(Event{api::SessionConfigParams{}}) == EventKind::ConfigureSession);
    CHECK(kind_of(Event{StopSessionCmd{}}) == EventKind::StopSession);
    CHECK(kind_of(Event{PauseSessionCmd{}}) == EventKind::PauseSession);
    CHECK(kind_of(Event{ResumeSessionCmd{}}) == EventKind::ResumeSession);
    CHECK(kind_of(Event{api::SetChargingCurrentParams{}}) == EventKind::SetChargingCurrent);
    CHECK(kind_of(Event{api::InjectFaultParams{}}) == EventKind::InjectFault);
    CHECK(kind_of(Event{ClearFaultCmd{}}) == EventKind::ClearFault);
    CHECK(kind_of(Event{api::BcbToggleParams{}}) == EventKind::BcbToggle);
    CHECK(kind_of(Event{api::RunScenarioParams{}}) == EventKind::RunScenario);
    CHECK(kind_of(Event{RaiseErrorCmd{}}) == EventKind::RaiseError);
    CHECK(kind_of(Event{ClearErrorCmd{}}) == EventKind::ClearError);
    CHECK(kind_of(Event{QueryStateCmd{}}) == EventKind::QueryState);
    CHECK(kind_of(Event{BspEventPayload{}}) == EventKind::BspEvent);
    CHECK(kind_of(Event{BspMeasurementPayload{}}) == EventKind::BspMeasurement);
    CHECK(kind_of(Event{EvInfoPayload{}}) == EventKind::EvInfo);
    CHECK(kind_of(Event{SlacStatePayload{}}) == EventKind::SlacState);
    CHECK(kind_of(Event{IsoPowerReadyEvt{}}) == EventKind::IsoPowerReady);
    CHECK(kind_of(Event{IsoAcMaxCurrentEvt{}}) == EventKind::IsoAcMaxCurrent);
    CHECK(kind_of(Event{IsoAcTargetPowerEvt{}}) == EventKind::IsoAcTargetPower);
    CHECK(kind_of(Event{IsoStopFromChargerEvt{}}) == EventKind::IsoStopFromCharger);
    CHECK(kind_of(Event{IsoV2GFinishedEvt{}}) == EventKind::IsoV2GFinished);
    CHECK(kind_of(Event{IsoDcPowerOnEvt{}}) == EventKind::IsoDcPowerOn);
    CHECK(kind_of(Event{IsoPauseFromChargerEvt{}}) == EventKind::IsoPauseFromCharger);
    CHECK(kind_of(Event{DcEvsePresentCurrentPayload{}}) == EventKind::DcEvsePresentCurrent);
    CHECK(kind_of(Event{DcEvsePresentVoltagePayload{}}) == EventKind::DcEvsePresentVoltage);
    CHECK(kind_of(Event{StateDeadlineEvt{}}) == EventKind::StateDeadline);
    CHECK(kind_of(Event{ShutdownEvt{}}) == EventKind::Shutdown);
}

TEST_CASE("Event constructed from an EventKind round-trips through kind_of", "[evsim][events]") {
    // The EventKind-taking constructor is a convenience for the common test
    // pattern; it must always produce an internally consistent payload.
    auto roundtrip = [](EventKind k) { return kind_of(Event{k}); };

    CHECK(roundtrip(EventKind::Enable) == EventKind::Enable);
    CHECK(roundtrip(EventKind::ConfigureSession) == EventKind::ConfigureSession);
    CHECK(roundtrip(EventKind::SetChargingCurrent) == EventKind::SetChargingCurrent);
    CHECK(roundtrip(EventKind::BspMeasurement) == EventKind::BspMeasurement);
    CHECK(roundtrip(EventKind::StateDeadline) == EventKind::StateDeadline);
    CHECK(roundtrip(EventKind::Shutdown) == EventKind::Shutdown);
}
