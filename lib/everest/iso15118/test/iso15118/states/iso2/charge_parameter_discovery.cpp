// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {
d2::SessionConfig make_config() {
    d2::SessionConfig config;
    config.evse_id = "DE*PNX*E12345*1";
    config.supported_energy_transfer_modes.push_back(dt::EnergyTransferMode::DC_extended);
    config.supported_energy_transfer_modes.push_back(dt::EnergyTransferMode::AC_three_phase_core);
    // The charge-loop limits (what energy management currently grants) are deliberately lower than the
    // hardware capabilities: ChargeParameterDiscoveryRes must advertise the capabilities.
    config.dc_max_power = 150000.0f;
    config.dc_max_current = 300.0f;
    config.dc_max_voltage = 900.0f;
    config.dc_capability_max_power = 250000.0f;
    config.dc_capability_max_current = 500.0f;
    config.dc_capability_max_voltage = 1000.0f;
    config.ac_nominal_voltage = 230.0f;
    config.ac_max_current = 16.0f;
    config.ac_capability_max_current = 32.0f;
    return config;
}
} // namespace

SCENARIO("ISO 15118-2 SECC ChargeParameterDiscovery handling") {
    const dt::SessionId id{};
    const auto config = make_config();

    GIVEN("A DC request") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev = req.dc_ev_charge_parameter.emplace();
        ev.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);
        const auto res = d2::state::handle_request(req, id, config);
        THEN("OK, Finished, DC parameters and a valid SAScheduleList") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE_FALSE(res.ac_evse_charge_parameter.has_value());
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->size() == 1);
            REQUIRE(res.sa_schedule_list->front().sa_schedule_tuple_id == 1);
            REQUIRE_FALSE(res.sa_schedule_list->front().pmax_schedule.empty());
        }
        THEN("The offer is the hardware capability, not the current energy-management limit") {
            const auto& dc = res.dc_evse_charge_parameter.value();
            REQUIRE(dt::from_physical_value(dc.evse_maximum_current_limit) == 500.0);
            REQUIRE(dt::from_physical_value(dc.evse_maximum_power_limit) == 250000.0);
            REQUIRE(dt::from_physical_value(dc.evse_maximum_voltage_limit) == 1000.0);
            REQUIRE(dt::from_physical_value(res.sa_schedule_list->front().pmax_schedule.front().p_max) == 250000.0);
        }
    }

    GIVEN("An EV whose maximum current does not exceed the EVSE minimum") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& params = req.dc_ev_charge_parameter.emplace();
        params.ev_maximum_current_limit = dt::to_physical_value(20.0, dt::Unit::A);
        params.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);

        auto min_config = make_config();
        min_config.dc_min_current = 25.0f;

        const auto res = d2::state::handle_request(req, id, min_config);
        THEN("FAILED_WrongChargeParameter with the EVSE announcing EVSE_Shutdown (EvseV2G parity)") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
            REQUIRE(res.dc_evse_charge_parameter.has_value()); // [V2G2-736]
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }

    GIVEN("A DC request while the charger cannot even run the cable check") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev = req.dc_ev_charge_parameter.emplace();
        ev.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);

        auto pause_config = make_config();
        pause_config.no_energy_pause = d20::NoEnergyPauseMode::BeforeCableCheck;

        const auto res = d2::state::handle_request(req, id, pause_config);
        THEN("The EV is told to stop without delay (IEC 61851-23:2023 CC.3.5.3)") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.notification_max_delay == 0);
        }
        THEN("A short schedule horizon is advertised instead of a full day") {
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.front().duration == 60 * 30);
        }
    }

    GIVEN("A DC request while the charger can still pre-charge") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev = req.dc_ev_charge_parameter.emplace();
        ev.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);

        auto pause_config = make_config();
        pause_config.no_energy_pause = d20::NoEnergyPauseMode::AfterCableCheckPreCharge;

        const auto res = d2::state::handle_request(req, id, pause_config);
        THEN("The stop is signalled with a grace period rather than immediately") {
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.notification_max_delay == 300);
        }
    }

    GIVEN("A DC request while energy is available") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev = req.dc_ev_charge_parameter.emplace();
        ev.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);

        const auto res = d2::state::handle_request(req, id, config);
        THEN("No stop is signalled and the configured schedule horizon is advertised") {
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.notification == dt::EVSENotification::None);
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.front().duration == config.sa_schedule_duration);
        }
    }

    GIVEN("A DC request after an EVSE-initiated stop") {
        // The stop request (stop_charging) reaches the EV in every state (EvseV2G parity), a
        // renegotiated ChargeParameterDiscovery included.
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev = req.dc_ev_charge_parameter.emplace();
        ev.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);

        const auto res = d2::state::handle_request(req, id, config, /*charger_stop=*/true);
        THEN("The response signals StopCharging without delay and EVSE_Shutdown") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& status = res.dc_evse_charge_parameter->dc_evse_status;
            REQUIRE(status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(status.notification_max_delay == 0);
            REQUIRE(status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }

    GIVEN("An AC request after an EVSE-initiated stop") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_three_phase_core;
        auto& ev = req.ac_ev_charge_parameter.emplace();
        ev.e_amount = dt::to_physical_value(10000.0, dt::Unit::Wh);
        ev.ev_max_voltage = dt::to_physical_value(230.0, dt::Unit::V);
        ev.ev_max_current = dt::to_physical_value(32.0, dt::Unit::A);
        ev.ev_min_current = dt::to_physical_value(6.0, dt::Unit::A);

        const auto res = d2::state::handle_request(req, id, config, /*charger_stop=*/true);
        THEN("The AC status signals StopCharging without delay") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& status = res.ac_evse_charge_parameter->ac_evse_status;
            REQUIRE(status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(status.notification_max_delay == 0);
        }
    }

    GIVEN("Physical EVSE values reported by the module") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev = req.dc_ev_charge_parameter.emplace();
        ev.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);

        auto values_config = make_config();
        values_config.dc_peak_current_ripple = 3.5f;
        values_config.dc_current_regulation_tolerance = 2.0f;
        values_config.dc_energy_to_be_delivered = 10000.0f;

        const auto res = d2::state::handle_request(req, id, values_config);
        THEN("They are advertised in the DC_EVSEChargeParameter") {
            REQUIRE(dt::from_physical_value(res.dc_evse_charge_parameter->evse_peak_current_ripple) == 3.5);
            REQUIRE(res.dc_evse_charge_parameter->evse_current_regulation_tolerance.has_value());
            REQUIRE(dt::from_physical_value(res.dc_evse_charge_parameter->evse_current_regulation_tolerance.value()) ==
                    2.0);
            REQUIRE(res.dc_evse_charge_parameter->evse_energy_to_be_delivered.has_value());
            REQUIRE(dt::from_physical_value(res.dc_evse_charge_parameter->evse_energy_to_be_delivered.value()) ==
                    10000.0);
        }
    }

    GIVEN("An AC request") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_three_phase_core;
        req.ac_ev_charge_parameter.emplace();
        const auto res = d2::state::handle_request(req, id, config);
        THEN("OK, Finished, AC parameters and a valid SAScheduleList") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
            REQUIRE(res.ac_evse_charge_parameter.has_value());
            REQUIRE_FALSE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->front().sa_schedule_tuple_id == 1);
        }
        THEN("EVSEMaxCurrent is the hardware capability and PMax covers the three phases") {
            REQUIRE(dt::from_physical_value(res.ac_evse_charge_parameter->evse_max_current) == 32.0);
            // 32 A * 230 V * 3 phases (the live 16 A limit reaches the EV in ChargingStatusRes instead)
            REQUIRE(dt::from_physical_value(res.sa_schedule_list->front().pmax_schedule.front().p_max) == 22080.0);
        }
    }

    GIVEN("A single-phase AC request on a charger that also offers three phases") {
        auto single_phase_config = make_config();
        single_phase_config.supported_energy_transfer_modes.push_back(dt::EnergyTransferMode::AC_single_phase_core);
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_single_phase_core;
        req.ac_ev_charge_parameter.emplace();
        const auto res = d2::state::handle_request(req, id, single_phase_config);
        THEN("PMax covers the single requested phase only") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            // 32 A * 230 V * 1 phase; the capability current is per phase, so the mode the EV picked is
            // what scales the offer.
            REQUIRE(dt::from_physical_value(res.sa_schedule_list->front().pmax_schedule.front().p_max) == 7360.0);
        }
    }

    GIVEN("An unsupported energy transfer mode") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_single_phase_core;
        const auto res = d2::state::handle_request(req, id, config);
        THEN("FAILED_WrongEnergyTransferMode") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongEnergyTransferMode);
        }
    }

    // [V2G2-366]: the module-reported EVSE error belongs in the ChargeParameterDiscoveryRes status too,
    // not only in the charge-loop responses. [V2G2-880] keeps it informational -- the offer stands.
    GIVEN("A DC request while the module reports a utility interrupt") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& evp = req.dc_ev_charge_parameter.emplace();
        evp.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        evp.ev_maximum_voltage_limit = dt::to_physical_value(900.0, dt::Unit::V);
        const auto res = d2::state::handle_request(req, id, config, /*charger_stop=*/false,
                                                   dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
        THEN("The DC status reports it while the response stays OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.status_code ==
                    dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
        }
    }

    GIVEN("An AC request while the module reports an RCD error") {
        auto ac_config = make_config();
        ac_config.supported_energy_transfer_modes.push_back(dt::EnergyTransferMode::AC_single_phase_core);
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_single_phase_core;
        req.ac_ev_charge_parameter.emplace();
        const auto res = d2::state::handle_request(req, id, ac_config, /*charger_stop=*/false, std::nullopt,
                                                   /*rcd_error=*/true);
        THEN("The AC status carries the RCD flag") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.ac_evse_charge_parameter.has_value());
            REQUIRE(res.ac_evse_charge_parameter->ac_evse_status.rcd == true);
        }
    }
}
