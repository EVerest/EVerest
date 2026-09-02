// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

namespace {
// A well-formed DC request: DIN SPEC 70121 is DC only, so the DC_EVChargeParameter is required.
message_din::ChargeParameterDiscoveryRequest
make_request(const dt::SessionId& session, dt::EnergyTransferMode mode = dt::EnergyTransferMode::DC_extended) {
    message_din::ChargeParameterDiscoveryRequest req;
    req.header.session_id = session;
    req.ev_requested_energy_transfer_type = mode;
    auto& params = req.dc_ev_charge_parameter.emplace();
    params.ev_maximum_current_limit = 200.0;
    params.ev_maximum_voltage_limit = 400.0;
    return req;
}

din::SessionConfig make_config() {
    din::SessionConfig config;
    // The charge-loop limits (what energy management currently grants) are deliberately lower than the
    // hardware capabilities: ChargeParameterDiscoveryRes must advertise the capabilities.
    config.evse_capability_maximum_current_limit = 400.0;
    config.evse_capability_maximum_power_limit = 360000.0;
    config.evse_capability_maximum_voltage_limit = 920.0;
    config.evse_maximum_current_limit = 200.0;
    config.evse_maximum_power_limit = 150000.0;
    config.evse_maximum_voltage_limit = 900.0;
    config.evse_minimum_current_limit = 0.0;
    config.evse_minimum_voltage_limit = 0.0;
    return config;
}
} // namespace

SCENARIO("DIN SECC ChargeParameterDiscovery state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A DC_extended request, processing finished") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("The DC EVSE parameters are advertised and EVSEProcessing is Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Finished);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.dc_evse_charge_parameter->evse_maximum_current_limit == 400.0);
            REQUIRE(res.dc_evse_charge_parameter->evse_maximum_voltage_limit == 920.0);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
        }
        THEN("A single-tuple SAScheduleList is advertised (mandatory when Finished)") {
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->size() == 1);
            REQUIRE(res.sa_schedule_list->front().sa_schedule_tuple_id == 1);
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.size() == 1);
            // 360 kW exceeds the DIN PMax short range and is capped at SHRT_MAX.
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.front().p_max == 32767);
        }
    }

    GIVEN("A DC_extended request, processing ongoing (no schedule yet)") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        const auto res = din::state::handle_request(req, make_config(), false, session);
        THEN("No SAScheduleList while Ongoing") {
            REQUIRE_FALSE(res.sa_schedule_list.has_value());
        }
    }

    GIVEN("A request after an EVSE-initiated stop") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        // The stop request reaches the EV in every state (EvseV2G parity); notification None [V2G-DC-500].
        const auto res = din::state::handle_request(req, make_config(), true, session, /*charger_stop=*/true);
        THEN("the response signals EVSE_Shutdown") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_status_code ==
                    dt::DcEvseStatusCode::EVSE_Shutdown);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_notification == dt::EvseNotification::None);
        }
    }

    GIVEN("A DC_extended request, processing ongoing") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        const auto res = din::state::handle_request(req, make_config(), false, session);
        THEN("EVSEProcessing is Ongoing") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Ongoing);
        }
    }

    GIVEN("A DC_core request against a DC_extended-only EVSE") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_core);

        // make_config() advertises DC_extended; [V2G-DC-397] the EV may only request what was offered.
        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("ResponseCode is FAILED_WrongEnergyTransferType") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongEnergyTransferType);
        }
    }

    GIVEN("A DC_core request against a DC_core EVSE") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_core);

        auto config = make_config();
        config.energy_transfer_mode = dt::SupportedEnergyTransferMode::DC_core;

        const auto res = din::state::handle_request(req, config, true, session);
        THEN("ResponseCode is OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("A request carrying an AC_EVChargeParameter") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);
        req.ac_ev_charge_parameter_present = true;

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("ResponseCode is FAILED_WrongChargeParameter [V2G-DC-398]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("A request while the charger reports no energy available") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        auto config = make_config();
        config.no_energy_pause = d20::NoEnergyPauseMode::BeforeCableCheck;

        const auto res = din::state::handle_request(req, config, true, session);
        THEN("The EV is told to stop without delay (IEC 61851-23:2023 CC.3.5.3)") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_notification ==
                    dt::EvseNotification::StopCharging);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.notification_max_delay == 0);
        }
        THEN("A short schedule horizon is advertised instead of a full day") {
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.front().duration == 60 * 30);
        }
    }

    GIVEN("A request while energy is available") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("No stop is signalled and the full-day schedule is advertised [V2G-DC-556]") {
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_notification == dt::EvseNotification::None);
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.front().duration == 86400);
        }
    }

    GIVEN("Physical EVSE values reported by the module") {
        auto req = make_request(session, dt::EnergyTransferMode::DC_extended);

        auto config = make_config();
        config.evse_peak_current_ripple = 3.5;
        config.evse_current_regulation_tolerance = 2.0;
        config.evse_energy_to_be_delivered = 10000.0;

        const auto res = din::state::handle_request(req, config, true, session);
        THEN("They are advertised in the DC_EVSEChargeParameter") {
            REQUIRE(res.dc_evse_charge_parameter->evse_peak_current_ripple == 3.5);
            REQUIRE(res.dc_evse_charge_parameter->evse_current_regulation_tolerance.value() == 2.0);
            REQUIRE(res.dc_evse_charge_parameter->evse_energy_to_be_delivered.value() == 10000.0);
        }
    }

    GIVEN("An EV whose maximum current does not exceed the EVSE minimum") {
        auto req = make_request(session);
        auto config = make_config();
        config.evse_minimum_current_limit = 250.0; // the fixture EV offers at most 200 A

        const auto res = din::state::handle_request(req, config, true, session);
        THEN("FAILED_WrongChargeParameter with the EVSE announcing EVSE_Shutdown (EvseV2G parity)") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_status_code ==
                    dt::DcEvseStatusCode::EVSE_Shutdown);
        }
    }

    GIVEN("An EV whose maximum voltage does not exceed the EVSE minimum") {
        auto req = make_request(session);
        auto config = make_config();
        config.evse_minimum_voltage_limit = 400.0; // the fixture EV offers at most 400 V

        const auto res = din::state::handle_request(req, config, true, session);
        THEN("FAILED_WrongChargeParameter") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("A DC request without a DC_EVChargeParameter") {
        auto req = make_request(session);
        req.dc_ev_charge_parameter.reset();

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("ResponseCode is FAILED_WrongChargeParameter and the mandatory EVSE parameters still ride along") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
        }
    }

    GIVEN("An AC request") {
        auto req = make_request(session, dt::EnergyTransferMode::AC_single_phase_core);

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("ResponseCode is FAILED_WrongEnergyTransferType") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongEnergyTransferType);
        }
    }

    // [V2G-DC-638]: the module-reported EVSE error belongs in the ChargeParameterDiscoveryRes status too,
    // not only in the charge-loop responses. [V2G-DC-637] keeps it informational -- the offer stands.
    GIVEN("A module-reported malfunction") {
        auto req = make_request(session);

        const auto res = din::state::handle_request(req, make_config(), true, session, /*charger_stop=*/false,
                                                    dt::DcEvseStatusCode::EVSE_Malfunction);
        THEN("The EVSE status reports it while the response stays OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_status_code ==
                    dt::DcEvseStatusCode::EVSE_Malfunction);
        }
    }
}
