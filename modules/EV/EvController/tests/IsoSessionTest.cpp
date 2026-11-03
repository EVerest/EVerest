// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// The seam between the FSM loop and the in-process ISO 15118 session: what the loop does
// with an event a session produced, and what the session does with the loop's calls.

#include "../main/IsoSession.hpp"
#include "../main/EvCtlRuntime.hpp"
#include "../main/states/Disabled.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <variant>

using namespace module;
using namespace module::test;

namespace {

Event tagged(EventPayload payload, std::uint32_t session) {
    Event ev{std::move(payload)};
    ev.iso_session = session;
    return ev;
}

} // namespace

TEST_CASE("pki_path reaches the session setup", "[evctl][iso_session]") {
    TestFixture fx;
    fx.cfg.pki_path = "/var/lib/ev-pki";

    const auto settings = iso_settings(fx.cfg, "/etc/everest");

    CHECK(settings.vehicle.pki_path == "/var/lib/ev-pki");
    CHECK(settings.vehicle.etc == "/etc/everest");
}

TEST_CASE("the loop admits a session's event only while that session is live", "[evctl][iso_session]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    auto fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Disabled>(*ctx));
    everest::lib::util::thread_safe_queue<Event> queue;
    const auto topic = fx.topics.everest_to_extern("iso_session_event");
    ctx->vars.iso_session_active = true;

    SECTION("an untagged event does not ask the session") {
        queue.push(Event{IsoV2GFinishedEvt{}});

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "admit"));
        CHECK_FALSE(ctx->vars.iso_session_active);
    }

    SECTION("the live session's finish is admitted as its end") {
        queue.push(tagged(IsoV2GFinishedEvt{}, 3));

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        CHECK(fx.mocks.iso.records == std::vector<std::string>{"admit(session=3,ended=1)"});
        CHECK_FALSE(ctx->vars.iso_session_active);
        CHECK(topic_recorded(fx.sink, topic));
    }

    SECTION("a finish of a session that is no longer live reaches nothing") {
        fx.mocks.iso_admits = false;
        queue.push(tagged(IsoV2GFinishedEvt{}, 3));

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        CHECK(fx.mocks.iso.records == std::vector<std::string>{"admit(session=3,ended=1)"});
        CHECK(ctx->vars.iso_session_active);
        CHECK_FALSE(topic_recorded(fx.sink, topic));
    }

    SECTION("any other event of a session is admitted without ending it") {
        queue.push(tagged(IsoPowerReadyEvt{}, 2));

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        CHECK(fx.mocks.iso.records == std::vector<std::string>{"admit(session=2,ended=0)"});
        CHECK(topic_recorded(fx.sink, topic));
    }

    SECTION("a session's welding failure is reported as a charger fault") {
        const auto fault_topic = fx.topics.everest_to_extern("fault");
        ::types::iso15118::WeldingDetectionFailure measured;
        measured.cycles = 3;
        measured.last_voltage_V = 120.5;
        queue.push(tagged(IsoWeldingDetectionFailedEvt{measured}, 2));

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        auto fault = nlohmann::json::parse(last_payload_for(fx.sink, fault_topic));
        CHECK(fault.at("type") == "ChargerFault");
        CHECK(fault.at("message") == "welding detection failed after 3 cycles, last voltage 120.5 V");

        queue.push(
            tagged(IsoWeldingDetectionFailedEvt{::types::iso15118::WeldingDetectionFailure{0, std::nullopt}}, 2));
        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        fault = nlohmann::json::parse(last_payload_for(fx.sink, fault_topic));
        CHECK(fault.at("message") == "welding detection failed after 0 cycles, last voltage unknown");
    }

    SECTION("a session's DER summaries are republished") {
        const auto sae = GENERATE(false, true);
        using ::types::iso15118::DerFlavor;
        ::types::iso15118::DerNegotiatedFunctions negotiated;
        negotiated.flavor = sae ? DerFlavor::AC_DER_SAE : DerFlavor::AC_DER_IEC;
        if (sae) {
            negotiated.sae_functions = std::vector<::types::iso15118::DerSaeFunction>{
                ::types::iso15118::DerSaeFunction::ChargeFunction, ::types::iso15118::DerSaeFunction::VoltVarFunction};
        } else {
            negotiated.iec_functions = std::vector<::types::iso15118::DerIecFunction>{
                ::types::iso15118::DerIecFunction::VoltVarMode, ::types::iso15118::DerIecFunction::WattCosPhiMode};
        }
        ::types::iso15118::DerControlReceived control;
        control.flavor = negotiated.flavor;
        control.source = ::types::iso15118::DerControlSource::ChargeLoop;
        control.dso_q_setpoint = -1200.5f;
        queue.push(tagged(IsoDerNegotiatedEvt{negotiated}, 2));
        queue.push(tagged(IsoDerControlEvt{control}, 2));

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        const nlohmann::json functions =
            sae ? nlohmann::json{"ChargeFunction", "VoltVarFunction"} : nlohmann::json{"VoltVarMode", "WattCosPhiMode"};
        const nlohmann::json expected_negotiated{{"flavor", sae ? "SAE" : "IEC"}, {"functions", functions}};
        const nlohmann::json expected_control{
            {"flavor", sae ? "SAE" : "IEC"}, {"source", "ChargeLoop"}, {"dso_q_setpoint", -1200.5}};
        CHECK(nlohmann::json::parse(last_payload_for(
                  fx.sink, fx.topics.everest_to_extern("der_negotiated_functions"))) == expected_negotiated);
        CHECK(nlohmann::json::parse(last_payload_for(fx.sink, fx.topics.everest_to_extern("der_control"))) ==
              expected_control);
    }

    SECTION("a dropped session's DC present value does not reach the vars") {
        fx.mocks.iso_admits = false;
        ctx->vars.dc_present_voltage_v = 400.0f;
        queue.push(tagged(DcEvsePresentVoltagePayload{12.0}, 2));

        EvCtlRuntime::drain_step(fsm, *ctx, queue, false);

        CHECK(ctx->vars.dc_present_voltage_v == 400.0f);
    }
}

namespace {

// The session's events, as the loop would receive them, in order.
struct Collected {
    everest::lib::util::thread_safe_queue<Event> events;

    // The next event of kind T within the timeout.
    template <class T> std::optional<Event> next(std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            auto ev = events.try_pop(std::chrono::milliseconds(50));
            if (ev && std::holds_alternative<T>(ev->payload)) {
                return ev;
            }
        }
        return std::nullopt;
    }
};

IsoSession::Settings loopback_settings() {
    IsoSession::Settings settings;
    // No SECC answers on loopback: a session stays in discovery until it is ended.
    settings.device = "lo";
    settings.supported_protocols = "ISO15118_20";
    settings.vehicle.evcc_id = "AA:BB:CC:DD:EE:01";
    settings.vehicle.response_timeout_ms = 100;
    settings.vehicle.tls = "off";
    settings.vehicle.tls_version = "auto";
    settings.vehicle.control_mode = "Dynamic";
    return settings;
}

::types::iso15118::DcEvParameters dc_params() {
    ::types::iso15118::DcEvParameters params{};
    params.max_current_limit = 300.0f;
    params.max_power_limit = 150000.0f;
    params.max_voltage_limit = 900.0f;
    params.min_voltage_limit = 150.0f;
    params.energy_capacity = 60000.0f;
    params.target_current = 20.0f;
    params.target_voltage = 400.0f;
    return params;
}

IsoStart start_of(API_types::ev_controller::ChargeMode mode) {
    IsoStart start;
    start.mode = mode;
    start.departure_time_s = 86400;
    return start;
}

bool start_dc(IsoSession& iso) {
    return iso.start_charging(start_of(API_types::ev_controller::ChargeMode::DcIsoD20));
}

} // namespace

TEST_CASE("the in-process session answers the loop's calls", "[evctl][iso_session]") {
    Collected collected;
    IsoSession iso{loopback_settings(), [&collected](Event ev) { collected.events.push(std::move(ev)); }};
    iso.set_dc_params(dc_params());

    SECTION("an aborted session ends with one finish, tagged with it") {
        REQUIRE(start_dc(iso));
        iso.abort_charging();

        const auto finish = collected.next<IsoV2GFinishedEvt>();
        REQUIRE(finish);
        CHECK(finish->iso_session != 0);
        CHECK(iso.admit(finish->iso_session, true));
        // Released: neither its finish nor any other event of it is admitted again.
        CHECK_FALSE(iso.admit(finish->iso_session, true));
        CHECK_FALSE(iso.admit(finish->iso_session, false));
    }

    SECTION("a start while a session runs is rejected") {
        REQUIRE(start_dc(iso));

        CHECK_FALSE(start_dc(iso));

        iso.abort_charging();
        CHECK(collected.next<IsoV2GFinishedEvt>());
    }

    SECTION("a start after the previous loop returned replaces it, and the old finish is dropped") {
        REQUIRE(start_dc(iso));
        iso.abort_charging();
        const auto first = collected.next<IsoV2GFinishedEvt>();
        REQUIRE(first);

        REQUIRE(start_dc(iso));

        CHECK_FALSE(iso.admit(first->iso_session, true));
        iso.abort_charging();
        const auto second = collected.next<IsoV2GFinishedEvt>();
        REQUIRE(second);
        CHECK(second->iso_session != first->iso_session);
        CHECK(iso.admit(second->iso_session, true));
    }

    // Before a session exists: with no SECC on loopback the Controller is still in discovery.
    SECTION("a pilot E ends the running session") {
        REQUIRE(start_dc(iso));

        iso.cp_state_changed(::types::iso15118::CpState::E);

        CHECK(collected.next<IsoV2GFinishedEvt>());
    }

    SECTION("a mode with no ISO 15118 session, or none the protocols offer, is rejected") {
        CHECK_FALSE(iso.start_charging(start_of(API_types::ev_controller::ChargeMode::AcIec)));
        CHECK_FALSE(iso.start_charging(start_of(API_types::ev_controller::ChargeMode::DcIso2)));
    }

    SECTION("a start without valid DC parameters is rejected") {
        IsoSession unset{loopback_settings(), [](Event) {}};

        CHECK_FALSE(start_dc(unset));
    }
}
