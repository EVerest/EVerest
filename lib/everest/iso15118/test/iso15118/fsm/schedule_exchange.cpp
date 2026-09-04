// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/schedule_exchange.hpp>
#include <iso15118/d20/state/service_detail.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_discovery.hpp>

#include <vector>

using namespace iso15118;

namespace dt = message_20::datatypes;

namespace {

d20::IecDerTransferLimits make_iec_limits() {
    d20::IecDerTransferLimits limits{};
    limits.nominal_charge_power = {11, 3};
    limits.nominal_discharge_power = {11, 3};
    limits.max_discharge_power = {11, 3};
    return limits;
}

d20::SaeDerTransferLimits make_sae_limits() {
    d20::SaeDerTransferLimits limits{};
    limits.nominal_charge_power = {11, 3};
    limits.nominal_discharge_power = {-11, 3};
    limits.max_discharge_power = {-11, 3};
    return limits;
}

// The DER limits are supplied unconditionally so the session config keeps the DER services in the offer list,
// which keeps the service discovery response non empty.
d20::EvseSetupConfig make_evse_setup(dt::ServiceCategory energy_service) {
    d20::AcTransferLimits ac_limits{};
    ac_limits.charge_power = {{11, 3}, {2, 3}};
    ac_limits.nominal_frequency = {50, 0};

    d20::EvseSetupConfig setup{};
    setup.evse_id = "everest se";
    setup.supported_energy_services = {energy_service};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.supported_vas_services = {};
    setup.enable_certificate_install_service = false;
    setup.dc_limits = {};
    setup.ac_limits = ac_limits;
    setup.der_iec_limits = make_iec_limits();
    setup.der_sae_limits = make_sae_limits();
    setup.control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    setup.powersupply_limits = {};
    return setup;
}

d20::SelectedServiceParameters make_service_parameters(dt::ServiceCategory energy_service) {
    return d20::SelectedServiceParameters(energy_service, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
                                          dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);
}

} // namespace

// [V2G20-3355] / [V2G20-3348]: in a DER session the EV may restart the service selection once the charge
// parameter discovery has finished, so the schedule exchange state has to accept a ServiceDiscoveryReq.
SCENARIO("ISO15118-20 schedule exchange service discovery escape") {

    const auto run = [](dt::ServiceCategory energy_service) {
        const auto evse_setup = make_evse_setup(energy_service);

        std::optional<d20::PauseContext> pause_ctx{std::nullopt};
        session::feedback::Callbacks callbacks{};

        auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
        auto& ctx = state_helper.get_context();

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        ctx.session = d20::Session(make_service_parameters(energy_service));

        message_20::ServiceDiscoveryRequest req{};
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        struct Outcome {
            bool transitioned;
            d20::StateID state_id;
            bool session_stopped;
            dt::ResponseCode response_code;
            std::vector<dt::ServiceCategory> offered_energy_services;
        };

        const auto res = ctx.get_response<message_20::ServiceDiscoveryResponse>();
        REQUIRE(res.has_value());

        std::vector<dt::ServiceCategory> offered_energy_services;
        for (const auto& service : res.value().energy_transfer_service_list) {
            offered_energy_services.push_back(service.service_id);
        }

        return Outcome{result.transitioned(), fsm.get_current_state_id(), ctx.session_stopped,
                       res.value().response_code, offered_energy_services};
    };

    GIVEN("An AC_DER_SAE session") {
        const auto outcome = run(dt::ServiceCategory::AC_DER_SAE);

        THEN("The service discovery is answered and the service detail state is entered") {
            REQUIRE(outcome.response_code == dt::ResponseCode::OK);
            REQUIRE(outcome.transitioned == true);
            REQUIRE(outcome.state_id == d20::StateID::ServiceDetail);
            REQUIRE(outcome.session_stopped == false);
            REQUIRE(outcome.offered_energy_services == std::vector{dt::ServiceCategory::AC_DER_SAE});
        }
    }

    GIVEN("An AC_DER_IEC session") {
        const auto outcome = run(dt::ServiceCategory::AC_DER_IEC);

        THEN("The service discovery is answered and the service detail state is entered") {
            REQUIRE(outcome.response_code == dt::ResponseCode::OK);
            REQUIRE(outcome.transitioned == true);
            REQUIRE(outcome.state_id == d20::StateID::ServiceDetail);
            REQUIRE(outcome.session_stopped == false);
            REQUIRE(outcome.offered_energy_services == std::vector{dt::ServiceCategory::AC_DER_IEC});
        }
    }

    GIVEN("A plain AC session") {
        const auto outcome = run(dt::ServiceCategory::AC);

        THEN("ResponseCode: FAILED_SequenceError and the session is stopped") {
            REQUIRE(outcome.response_code == dt::ResponseCode::FAILED_SequenceError);
            REQUIRE(outcome.transitioned == false);
            REQUIRE(outcome.state_id == d20::StateID::ScheduleExchange);
            REQUIRE(outcome.session_stopped == true);
        }
    }
}

// The service discovery arm above sits in the same if/else chain as the primary ScheduleExchangeReq arm, so the
// main path is pinned here as well.
SCENARIO("ISO15118-20 schedule exchange state transitions") {

    const auto evse_setup = make_evse_setup(dt::ServiceCategory::AC_DER_SAE);

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();

    GIVEN("An AC_DER_SAE session in scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        ctx.session = d20::Session(make_service_parameters(dt::ServiceCategory::AC_DER_SAE));

        message_20::ScheduleExchangeRequest req{};
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.max_supporting_points = 12;
        req.control_mode.emplace<dt::Scheduled_SEReqControlMode>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The schedule is answered and the power delivery state is entered") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::PowerDelivery);
            REQUIRE(ctx.session_stopped == false);

            const auto res = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().processing == dt::Processing::Finished);
            REQUIRE(std::holds_alternative<dt::Scheduled_SEResControlMode>(res.value().control_mode));
        }
    }
}
