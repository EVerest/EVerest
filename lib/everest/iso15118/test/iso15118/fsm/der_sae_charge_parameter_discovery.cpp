// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

#include <cstdint>
#include <variant>
#include <vector>

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace dt_sae = message_20::datatypes::sae;
namespace sae = iso15118::sae;

using CpdRequest = message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest;
using CpdResponse = message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse;

namespace {

constexpr float NOMINAL_VOLTAGE_V = 230.0f;

// Copied from test/exi/cb/iso20/helper.hpp so the FSM tests do not reach across test directories.
template <typename Message> std::vector<std::uint8_t> serialize_helper(const Message& message) {
    std::uint8_t serialization_buffer[2048];
    io::StreamOutputView out({serialization_buffer, sizeof(serialization_buffer)});

    const auto size = message_20::serialize(message, out);

    return std::vector<std::uint8_t>(serialization_buffer, serialization_buffer + size);
}

constexpr std::uint32_t bit_of(sae::DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

constexpr std::uint32_t CHARGE_AND_DISCHARGE_ONLY =
    bit_of(sae::DerBitMapFunctions::ChargeFunction) | bit_of(sae::DerBitMapFunctions::DischargeFunction);

d20::AcTransferLimits make_ac_limits() {
    d20::AcTransferLimits limits{};
    limits.charge_power = {{22, 3}, {10, 0}};
    limits.nominal_frequency = {50, 0};
    limits.power_ramp_limitation = dt::RationalNumber{2, 0};
    return limits;
}

d20::SaeDerTransferLimits make_sae_limits() {
    d20::SaeDerTransferLimits limits{};
    limits.nominal_charge_power = dt::RationalNumber{11, 3};
    limits.nominal_discharge_power = dt::RationalNumber{-11, 3};
    limits.max_discharge_power = {-22, 3};
    return limits;
}

d20::EvseSetupConfig make_evse_setup() {
    d20::EvseSetupConfig setup{};
    setup.evse_id = "everest se";
    setup.supported_energy_services = {dt::ServiceCategory::AC_DER_SAE};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.supported_vas_services = {};
    setup.enable_certificate_install_service = false;
    setup.dc_limits = {};
    setup.ac_limits = make_ac_limits();
    setup.der_iec_limits = std::nullopt;
    setup.der_sae_limits = make_sae_limits();
    setup.control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    // Both modes are deliberately the non default enumerator, so an unsent control set cannot be mistaken for
    // the struct default.
    setup.der_sae_setup_config =
        d20::DerSaeSetupConfig{d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V),
                               sae::RequiredDEROperatingMode::GridForming, sae::GridConnectionMode::GridIslanded};
    setup.powersupply_limits = {};
    return setup;
}

d20::SelectedServiceParameters make_service_parameters() {
    return d20::SelectedServiceParameters(dt::ServiceCategory::AC_DER_SAE, dt::AcConnector::ThreePhase,
                                          dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                          dt::Pricing::NoPricing, 230);
}

// Turning every enable on makes the gating assertions below non-vacuous: the inert default config has them
// all off already.
sae::DERControl all_enabled_der_control() {
    auto der_control = d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
    der_control.voltage_trip.over_voltage_must_trip_curve.enable = true;
    der_control.voltage_trip.under_voltage_must_trip_curve.enable = true;
    der_control.frequency_trip.over_frequency_must_trip_curve.enable = true;
    der_control.frequency_trip.under_frequency_must_trip_curve.enable = true;
    der_control.enter_service.permit_service = true;
    der_control.reactive_power_support.constant_power_factor.enable = true;
    der_control.reactive_power_support.volt_var.enable = true;
    der_control.reactive_power_support.watt_var.enable = true;
    der_control.reactive_power_support.constant_var.enable = true;
    der_control.active_power_support.frequency_droop.enable = true;
    der_control.active_power_support.volt_watt.enable = true;
    der_control.active_power_support.constant_watt.enable = true;
    der_control.active_power_support.limit_max_discharge_power.enable = true;
    return der_control;
}

CpdRequest make_request(const std::array<std::uint8_t, 8>& session_id, dt::Processing processing,
                        std::uint32_t supported_modes, std::uint32_t enabled_modes) {
    CpdRequest req{};
    req.header.session_id = session_id;
    req.header.timestamp = 1691411798;

    auto& mode = req.transfer_mode;
    mode.max_charge_power = {22, 3};
    mode.min_charge_power = {10, 0};
    mode.maximum_discharge_power = {-22, 3};
    mode.processing = processing;
    mode.supported_modes = supported_modes;
    mode.enabled_modes = enabled_modes;
    return req;
}

// The default request mirrors make_ac_limits and make_sae_limits, so an assertion on those values cannot
// tell an EV limit apart from the EVSE's own configuration. No generation below matches those, and the
// generations differ from each other, so consecutive requests within one session stay distinguishable.
void with_distinct_ev_limits(CpdRequest& req, std::int16_t generation = 1) {
    auto& mode = req.transfer_mode;
    mode.max_charge_power = {static_cast<std::int16_t>(7 * generation), 3};
    mode.max_charge_power_L2 = dt::RationalNumber{static_cast<std::int16_t>(4 * generation), 3};
    mode.max_charge_power_L3 = dt::RationalNumber{static_cast<std::int16_t>(2 * generation), 3};
    mode.min_charge_power = {static_cast<std::int16_t>(350 * generation), 0};
    mode.maximum_discharge_power = {static_cast<std::int16_t>(-6 * generation), 3};
    mode.maximum_discharge_power_L2 = dt::RationalNumber{static_cast<std::int16_t>(-35 * generation), 2};
    mode.maximum_discharge_power_L3 = dt::RationalNumber{static_cast<std::int16_t>(-15 * generation), 2};
    mode.minimum_discharge_power = dt::RationalNumber{static_cast<std::int16_t>(-250 * generation), 0};
}

void require_distinct_ev_limits(const dt_sae::DER_SAE_AC_CPDReqEnergyTransferMode& mode, std::int16_t generation = 1) {
    const auto scale = static_cast<float>(generation);
    REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 7000.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.max_charge_power_L2.value()) == 4000.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.max_charge_power_L3.value()) == 2000.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 350.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power) == -6000.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power_L2.value()) == -3500.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power_L3.value()) == -1500.0f * scale);
    REQUIRE(dt::from_RationalNumber(mode.minimum_discharge_power.value()) == -250.0f * scale);
}

void require_every_enable_cleared(const dt_sae::DERControlCPDRes& out) {
    REQUIRE(out.voltage_trip.over_voltage_must_trip_curve.enable == false);
    REQUIRE(out.voltage_trip.under_voltage_must_trip_curve.enable == false);
    REQUIRE(out.frequency_trip.over_frequency_must_trip_curve.enable == false);
    REQUIRE(out.frequency_trip.under_frequency_must_trip_curve.enable == false);
    REQUIRE(out.reactive_power_support_cpd_res.constant_power_factor.enable == false);
    REQUIRE(out.reactive_power_support_cpd_res.volt_var.enable == false);
    REQUIRE(out.reactive_power_support_cpd_res.watt_var.enable == false);
    REQUIRE(out.reactive_power_support_cpd_res.constant_var.enable == false);
    REQUIRE(out.active_power_support_cpd_res.frequency_droop.enable == false);
    REQUIRE(out.active_power_support_cpd_res.volt_watt.enable == false);
    REQUIRE(out.active_power_support_cpd_res.constant_watt.enable == false);
    REQUIRE(out.active_power_support_cpd_res.limit_max_discharge_power.enable == false);
}

} // namespace

SCENARIO("ISO15118-20 der sae ac charge parameter discovery state transitions") {

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(make_evse_setup()), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();

    const auto service_parameters = make_service_parameters();

    GIVEN("Bad case - unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        const auto req = make_request(d20::Session().get_id(), dt::Processing::Ongoing, CHARGE_AND_DISCHARGE_ONLY, 0);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED_UnknownSession and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_UnknownSession);
        }

        THEN("The EV supported modes are not recorded from a rejected request") {
            REQUIRE(ctx.session.get_ev_supported_sae_functions().has_value() == false);
        }
    }

    GIVEN("Bad case - no SAE limits are configured") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);
        ctx.session_config.der_sae_limits.reset();

        const auto req = make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED_WrongChargeParameter without a transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("Good case - the EV is still processing") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        const auto req = make_request(ctx.session.get_id(), dt::Processing::Ongoing, CHARGE_AND_DISCHARGE_ONLY, 0);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("EVSEProcessing: Ongoing and the state is kept") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == false);

            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().transfer_mode.processing == dt::Processing::Ongoing);
        }
    }

    GIVEN("Good case - the EV finished processing") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        const auto req = make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("EVSEProcessing: Finished and the schedule exchange is entered") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);
            REQUIRE(ctx.session_stopped == false);

            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().transfer_mode.processing == dt::Processing::Finished);
        }

        THEN("The DER control set is marked as delivered") {
            const auto revision = ctx.session_config.der_sae_setup_config.value().revision;
            REQUIRE(ctx.session.der_control_changed_since_cpd(revision) == false);
        }

        THEN("The masked EV supported modes are recorded on the session") {
            REQUIRE(ctx.session.get_ev_supported_sae_functions().value_or(0) == CHARGE_AND_DISCHARGE_ONLY);
        }

        THEN("The configured limits and the update time are answered") {
            const auto& mode = ctx.get_response<CpdResponse>().value().transfer_mode;
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power) == -22000.0f);
            REQUIRE(mode.update_time == ctx.session_config.der_sae_setup_config.value().der_control_update_time);
            REQUIRE(mode.required_der_operating_mode == dt_sae::RequiredDEROperatingMode::GridForming);
            REQUIRE(mode.grid_connection_mode == dt_sae::GridConnectionMode::GridIslanded);
        }
    }

    // [V2G20-3354]: while the charge parameter discovery is still Ongoing, the only allowed next request is
    // another ChargeParameterDiscoveryReq. The service selection restart is only reachable once the state has
    // been left, and is handled by the schedule exchange state.
    GIVEN("The EV sends a service discovery while the charge parameter discovery is ongoing") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        const auto cpd_req = make_request(ctx.session.get_id(), dt::Processing::Ongoing, CHARGE_AND_DISCHARGE_ONLY, 0);
        state_helper.handle_request(cpd_req);
        REQUIRE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned() == false);

        message_20::ServiceDiscoveryRequest service_discovery_req{};
        service_discovery_req.header.session_id = ctx.session.get_id();
        service_discovery_req.header.timestamp = 1691411799;

        state_helper.handle_request(service_discovery_req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The service discovery is rejected with FAILED_SequenceError and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<message_20::ServiceDiscoveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }

    GIVEN("The EV stops the session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        message_20::SessionStopRequest req{};
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The stop is answered and the session ends") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Bad case - an unexpected message type") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        message_20::SessionSetupRequest req{};
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The session setup is rejected with FAILED_SequenceError and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }

    // The per function gating itself is pinned exhaustively by test/iso15118/d20/sae_der_control_convert.cpp.
    // What only the state machine can show is that the gate is driven by the SupportedModes of the request it
    // just recorded on the session.
    GIVEN("The DER control set has every function enabled") {
        ctx.session_config.der_sae_setup_config.value().der_control = all_enabled_der_control();

        THEN("An EV declaring only charge and discharge gets every enable cleared") {
            fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};
            ctx.session = d20::Session(service_parameters);

            state_helper.handle_request(
                make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0));
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            require_every_enable_cleared(res.value().transfer_mode.der_control_cpd_res);
        }

        THEN("An EV additionally declaring volt var keeps only that enable") {
            fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};
            ctx.session = d20::Session(service_parameters);

            const auto supported_modes = CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::VoltVarFunction);
            state_helper.handle_request(
                make_request(ctx.session.get_id(), dt::Processing::Finished, supported_modes, 0));
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);

            const auto& out = res.value().transfer_mode.der_control_cpd_res;
            REQUIRE(out.reactive_power_support_cpd_res.volt_var.enable == true);
            REQUIRE(out.reactive_power_support_cpd_res.constant_power_factor.enable == false);
            REQUIRE(out.reactive_power_support_cpd_res.watt_var.enable == false);
            REQUIRE(out.reactive_power_support_cpd_res.constant_var.enable == false);
            // permit_service is an authorization, not an Enable: not gated. See ADR-0023.
            REQUIRE(out.enter_service_cpd_res.permit_service == true);
            REQUIRE(out.voltage_trip.over_voltage_must_trip_curve.enable == false);
            REQUIRE(out.voltage_trip.under_voltage_must_trip_curve.enable == false);
            REQUIRE(out.frequency_trip.over_frequency_must_trip_curve.enable == false);
            REQUIRE(out.frequency_trip.under_frequency_must_trip_curve.enable == false);
            REQUIRE(out.active_power_support_cpd_res.frequency_droop.enable == false);
            REQUIRE(out.active_power_support_cpd_res.volt_watt.enable == false);
            REQUIRE(out.active_power_support_cpd_res.constant_watt.enable == false);
            REQUIRE(out.active_power_support_cpd_res.limit_max_discharge_power.enable == false);
        }
    }

    GIVEN("The present power arrives as a control event") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        d20::AcPresentPower present_power{};
        present_power.present_active_power = dt::RationalNumber{15, 3};
        present_power.present_active_power_L2 = dt::RationalNumber{16, 3};
        present_power.present_active_power_L3 = dt::RationalNumber{17, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(
            make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("All three phases are reported back to the EV") {
            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);

            const auto& mode = res.value().transfer_mode;
            REQUIRE(dt::from_RationalNumber(mode.present_active_power.value()) == 15000.0f);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power_L2.value()) == 16000.0f);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power_L3.value()) == 17000.0f);
        }
    }

    // A rejection returns before the configured DER control is applied, so it carries the inert default one.
    // Decoding it verifies that a rejection is well formed on the wire and not merely encodable.
    GIVEN("A rejection the EXI codec has to carry") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        state_helper.handle_request(
            make_request(d20::Session().get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        const auto res = ctx.get_response<CpdResponse>();
        REQUIRE(res.has_value());
        REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_UnknownSession);

        const auto bytes = serialize_helper(res.value());
        const io::StreamInputView stream_view{bytes.data(), bytes.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("The rejection round trips with the inert default DER control") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeParameterDiscoveryRes);
            const auto& round_tripped = variant.get<CpdResponse>();

            REQUIRE(round_tripped.response_code == dt::ResponseCode::FAILED_UnknownSession);
            require_every_enable_cleared(round_tripped.transfer_mode.der_control_cpd_res);

            const auto& der_control = round_tripped.transfer_mode.der_control_cpd_res;
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.voltage_trip.under_voltage_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.frequency_trip.over_frequency_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.frequency_trip.under_frequency_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.reactive_power_support_cpd_res.volt_var.curve_data_points.size() == 2);
            REQUIRE(der_control.reactive_power_support_cpd_res.watt_var.curve_data_points.size() == 2);
            REQUIRE(der_control.active_power_support_cpd_res.volt_watt.curve_data_points.size() == 2);
        }

        THEN("The scalars a rejection never assigns carry their declared defaults") {
            const auto& round_tripped = variant.get<CpdResponse>();
            const auto& mode = round_tripped.transfer_mode;

            REQUIRE(mode.processing == dt::Processing::Finished);
            REQUIRE(mode.required_der_operating_mode == dt_sae::RequiredDEROperatingMode::GridFollowing);
            REQUIRE(mode.grid_connection_mode == dt_sae::GridConnectionMode::GridConnected);
            REQUIRE(mode.update_time == 0);
        }
    }

    GIVEN("The EV set reserved bits in its supported modes") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        constexpr std::uint32_t reserved_bits = (1U << 2) | (1U << 9) | (1U << 25) | (1U << 31);
        const auto req =
            make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY | reserved_bits, 0);
        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The request succeeds and the reserved bits are masked out of the recorded declaration") {
            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);

            const auto recorded = ctx.session.get_ev_supported_sae_functions();
            REQUIRE(recorded.has_value());
            REQUIRE((recorded.value() & reserved_bits) == 0U);
            REQUIRE(recorded.value() == CHARGE_AND_DISCHARGE_ONLY);
        }
    }

    GIVEN("The EV reports enabled modes that match nothing the SECC asked for") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        const auto req = make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY,
                                      bit_of(sae::DerBitMapFunctions::WattVarFunction));
        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The inequality is not treated as an error") {
            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(ctx.session_stopped == false);
        }
    }
}

SCENARIO("ISO15118-20 der sae ac charge parameter discovery feedback") {

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    std::vector<session::feedback::AcLimits> reported_limits;
    session::feedback::Callbacks callbacks{};
    callbacks.ac_limits = [&reported_limits](const session::feedback::AcLimits& limits) {
        reported_limits.push_back(limits);
    };

    auto state_helper = FsmStateHelper(d20::SessionConfig(make_evse_setup()), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();

    const auto service_parameters = make_service_parameters();

    GIVEN("An accepted SAE charge parameter discovery request") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        auto req = make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0);
        with_distinct_ev_limits(req);
        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The request is accepted and the schedule exchange is entered") {
            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);
        }

        THEN("The EV limits, not the EVSE configuration, are reported through the ac_limits feedback") {
            REQUIRE(reported_limits.size() == 1);

            const auto* mode = std::get_if<dt_sae::DER_SAE_AC_CPDReqEnergyTransferMode>(&reported_limits.front());
            REQUIRE(mode != nullptr);
            require_distinct_ev_limits(*mode);
        }

        THEN("The EV limits are recorded for the schedule exchange") {
            const auto* mode =
                std::get_if<dt_sae::DER_SAE_AC_CPDReqEnergyTransferMode>(&ctx.session_ev_info.ev_transfer_limits);
            REQUIRE(mode != nullptr);
            require_distinct_ev_limits(*mode);
        }
    }

    // The state stays resident while the EV keeps the discovery Ongoing, so every iteration has to be
    // reported and the recording has to track the latest one rather than the first.
    GIVEN("The EV keeps the charge parameter discovery ongoing before finishing") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        const auto feed = [&](dt::Processing processing, std::int16_t generation) {
            auto req = make_request(ctx.session.get_id(), processing, CHARGE_AND_DISCHARGE_ONLY, 0);
            with_distinct_ev_limits(req, generation);
            state_helper.handle_request(req);
            return fsm.feed(d20::Event::V2GTP_MESSAGE);
        };

        REQUIRE(feed(dt::Processing::Ongoing, 1).transitioned() == false);
        REQUIRE(feed(dt::Processing::Ongoing, 2).transitioned() == false);
        const auto result = feed(dt::Processing::Finished, 3);

        THEN("The schedule exchange is entered only after the finishing request") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);
            REQUIRE(ctx.session_stopped == false);
        }

        THEN("Every iteration is reported through the ac_limits feedback, in order") {
            REQUIRE(reported_limits.size() == 3);

            for (std::int16_t generation = 1; generation <= 3; generation++) {
                const auto* mode = std::get_if<dt_sae::DER_SAE_AC_CPDReqEnergyTransferMode>(
                    &reported_limits.at(static_cast<size_t>(generation - 1)));
                REQUIRE(mode != nullptr);
                require_distinct_ev_limits(*mode, generation);
            }
        }

        THEN("The recorded EV limits are the ones of the last iteration") {
            const auto* mode =
                std::get_if<dt_sae::DER_SAE_AC_CPDReqEnergyTransferMode>(&ctx.session_ev_info.ev_transfer_limits);
            REQUIRE(mode != nullptr);
            require_distinct_ev_limits(*mode, 3);
        }
    }

    GIVEN("A rejected SAE charge parameter discovery request") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);
        ctx.session_config.der_sae_limits.reset();

        auto req = make_request(ctx.session.get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0);
        with_distinct_ev_limits(req);
        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("No EV limits are reported") {
            REQUIRE(reported_limits.empty());
        }

        // The recording has to sit behind the same gate as the report, so a rejected request cannot leave its
        // declared limits on the session.
        THEN("No EV limits are recorded on the session") {
            REQUIRE(std::holds_alternative<dt::DC_CPDReqEnergyTransferMode>(ctx.session_ev_info.ev_transfer_limits));
        }
    }

    GIVEN("A SAE charge parameter discovery request carrying a foreign session id") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeParameterDiscovery>()};

        ctx.session = d20::Session(service_parameters);

        auto req = make_request(d20::Session().get_id(), dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY, 0);
        with_distinct_ev_limits(req);
        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The request is rejected and touches neither the feedback nor the session") {
            const auto res = ctx.get_response<CpdResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(reported_limits.empty());
            REQUIRE(std::holds_alternative<dt::DC_CPDReqEnergyTransferMode>(ctx.session_ev_info.ev_transfer_limits));
        }
    }
}
