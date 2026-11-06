// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;
namespace state = ev::d20::state;

namespace {

ev::params::AcChargeParams ac_params() {
    ev::params::AcChargeParams p{};
    p.phase_count = 1;
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    p.max_discharge_power = 15000.0f;
    p.min_discharge_power = 800.0f;
    return p;
}

ev::params::DcChargeParams dc_params() {
    ev::params::DcChargeParams p{};
    p.max_charge_power = 50000.0f;
    p.max_charge_current = 125.0f;
    p.max_voltage = 500.0f;
    p.min_voltage = 200.0f;
    p.max_discharge_power = 30000.0f;
    p.min_discharge_power = 1000.0f;
    p.max_discharge_current = 80.0f;
    return p;
}

} // namespace

SCENARIO("ISO15118-20 EV SupportedAppProtocol request lists the advertised protocols that fit") {
    GIVEN("more protocols than the request holds") {
        std::vector<message_20::SupportedAppProtocol> advertised;
        for (std::uint8_t i = 0; i < 25; ++i) {
            advertised.push_back({"urn:iso:std:iso:15118:-20:AC", 1, i, i, static_cast<std::uint8_t>(i + 1)});
        }

        const auto req = state::SupportedAppProtocol::make_request(advertised);

        REQUIRE(req.app_protocol.size() == req.app_protocol.max_size());
        for (std::size_t i = 0; i < req.app_protocol.size(); ++i) {
            REQUIRE(req.app_protocol[i] == advertised[i]);
        }
    }

    GIVEN("fewer protocols than the request holds") {
        const std::vector<message_20::SupportedAppProtocol> advertised{
            {"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1},
            {"urn:iso:std:iso:15118:-20:DC", 1, 0, 2, 2},
        };

        const auto req = state::SupportedAppProtocol::make_request(advertised);

        REQUIRE(req.app_protocol.size() == 2);
        REQUIRE(req.app_protocol[0] == advertised[0]);
        REQUIRE(req.app_protocol[1] == advertised[1]);
    }
}

SCENARIO("ISO15118-20 EV SessionSetup request carries the EVCCID") {
    const auto req = state::SessionSetup::make_request("EVTESTID02");

    REQUIRE(req.evccid == "EVTESTID02");
}

SCENARIO("ISO15118-20 EV ServiceDiscovery request names only the selected service") {
    const auto req = state::ServiceDiscovery::make_request(dt::ServiceCategory::AC_BPT);

    REQUIRE(req.supported_service_ids.has_value());
    REQUIRE(req.supported_service_ids->size() == 1);
    REQUIRE(req.supported_service_ids->at(0) == message_20::to_underlying_value(dt::ServiceCategory::AC_BPT));
}

SCENARIO("ISO15118-20 EV ServiceDetail request names the selected service") {
    const auto req = state::ServiceDetail::make_request(dt::ServiceCategory::DC_BPT);

    REQUIRE(req.service == message_20::to_underlying_value(dt::ServiceCategory::DC_BPT));
}

SCENARIO("ISO15118-20 EV ServiceSelection request pairs the service with the parameter set") {
    const auto req = state::ServiceSelection::make_request(dt::ServiceCategory::AC, 7);

    REQUIRE(req.selected_energy_transfer_service.service_id == dt::ServiceCategory::AC);
    REQUIRE(req.selected_energy_transfer_service.parameter_set_id == 7);
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery request follows the service's BPT form") {
    GIVEN("the plain AC service") {
        const auto req = state::AC_ChargeParameterDiscovery::make_request(ac_params(), dt::AcConnector::SinglePhase,
                                                                          dt::ServiceCategory::AC);

        const auto* mode = std::get_if<dt::AC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(dt::from_RationalNumber(mode->max_charge_power) == 22000.0f);
        REQUIRE(dt::from_RationalNumber(mode->min_charge_power) == 1000.0f);
        REQUIRE_FALSE(mode->max_charge_power_L2.has_value());
    }

    GIVEN("the AC_BPT service") {
        const auto req = state::AC_ChargeParameterDiscovery::make_request(ac_params(), dt::AcConnector::SinglePhase,
                                                                          dt::ServiceCategory::AC_BPT);

        const auto* mode = std::get_if<dt::BPT_AC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(dt::from_RationalNumber(mode->max_charge_power) == 22000.0f);
        REQUIRE(dt::from_RationalNumber(mode->max_discharge_power) == -15000.0f);
        REQUIRE(dt::from_RationalNumber(mode->min_discharge_power) == -800.0f);
    }
}

SCENARIO("ISO15118-20 EV AC_DER_IEC ChargeParameterDiscovery request finishes in one round") {
    const auto req =
        state::AC_DER_IEC_ChargeParameterDiscovery::make_request(ac_params(), dt::AcConnector::SinglePhase);

    const auto& mode = req.transfer_mode;
    REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 22000.0f);
    REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 1000.0f);
    REQUIRE(dt::from_RationalNumber(mode.max_discharge_power) == -15000.0f);
    REQUIRE(dt::from_RationalNumber(mode.min_discharge_power) == -800.0f);
    REQUIRE(mode.processing == dt::Processing::Finished);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE ChargeParameterDiscovery request carries the round's inputs") {
    const ev::d20::SeccClock secc_clock;

    const auto req = state::AC_DER_SAE_ChargeParameterDiscovery::make_request(
        {}, ac_params(), dt::AcConnector::SinglePhase, dt::Processing::Ongoing, 0x5, 1234, secc_clock);

    const auto& mode = req.transfer_mode;
    REQUIRE(mode.processing == dt::Processing::Ongoing);
    REQUIRE(mode.enabled_modes == 0x5);
    REQUIRE(mode.update_time == 1234);
    REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 22000.0f);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery request follows the service's BPT form") {
    GIVEN("the plain DC service") {
        const auto req = state::DC_ChargeParameterDiscovery::make_request(dc_params(), dt::ServiceCategory::DC);

        const auto* mode = std::get_if<dt::DC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(dt::from_RationalNumber(mode->max_charge_power) == 50000.0f);
        REQUIRE(dt::from_RationalNumber(mode->min_charge_power) == 0.0f);
        REQUIRE(dt::from_RationalNumber(mode->max_charge_current) == 125.0f);
        REQUIRE(dt::from_RationalNumber(mode->max_voltage) == 500.0f);
        REQUIRE(dt::from_RationalNumber(mode->min_voltage) == 200.0f);
        REQUIRE_FALSE(mode->target_soc.has_value());
    }

    GIVEN("the DC_BPT service") {
        const auto req = state::DC_ChargeParameterDiscovery::make_request(dc_params(), dt::ServiceCategory::DC_BPT);

        const auto* mode = std::get_if<dt::BPT_DC_CPDReqEnergyTransferMode>(&req.transfer_mode);
        REQUIRE(mode != nullptr);
        REQUIRE(dt::from_RationalNumber(mode->max_charge_power) == 50000.0f);
        REQUIRE(dt::from_RationalNumber(mode->max_discharge_power) == -30000.0f);
        REQUIRE(dt::from_RationalNumber(mode->min_discharge_power) == -1000.0f);
        REQUIRE(dt::from_RationalNumber(mode->max_discharge_current) == -80.0f);
        REQUIRE(dt::from_RationalNumber(mode->min_discharge_current) == 0.0f);
    }
}

SCENARIO("ISO15118-20 EV SessionStop request maps the stop reason to the charging session") {
    REQUIRE(state::SessionStop::make_request(ev::session::StopReason::EvPause).charging_session ==
            dt::ChargingSession::Pause);
    REQUIRE(state::SessionStop::make_request(ev::session::StopReason::EvsePause).charging_session ==
            dt::ChargingSession::Pause);
    REQUIRE(state::SessionStop::make_request(ev::session::StopReason::EvStop).charging_session ==
            dt::ChargingSession::Terminate);
    REQUIRE(state::SessionStop::make_request(ev::session::StopReason::EvseTerminate).charging_session ==
            dt::ChargingSession::Terminate);
}
