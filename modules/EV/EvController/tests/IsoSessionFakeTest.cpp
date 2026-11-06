// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// IsoSession on the fake Controller (everest::iso15118_ev_fake_controller): what each start builds,
// what the session's feedback becomes, and how a pause carries to the next start. No session
// touches a network.

#include "../main/IsoSession.hpp"

#include <everest/iso15118_ev_session/fake_controller.hpp>
#include <everest/iso15118_ev_session/test_contract.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace module;

namespace {

using CM = everest::lib::API::V1_0::types::ev_controller::ChargeMode;
using everest::lib::API::V1_0::types::ev_controller::BptParams;
using everest::lib::API::V1_0::types::ev_controller::PaymentOption;
using iso15118::ProtocolId;
using Service = iso15118::message_20::datatypes::ServiceCategory;
using Iso2Mode = iso15118::shared_datatypes::EnergyTransferMode;

IsoSession::Settings settings_with(const std::string& protocols) {
    IsoSession::Settings settings;
    settings.device = "eth1";
    settings.supported_protocols = protocols;
    auto& vehicle = settings.vehicle;
    vehicle.evcc_id = "AA:BB:CC:DD:EE:01";
    vehicle.tls = "off";
    vehicle.tls_version = "auto";
    vehicle.control_mode = "Dynamic";
    vehicle.iso2_ac_ev_max_voltage_v = 230;
    vehicle.iso2_ac_ev_max_current_a = 16;
    vehicle.iso2_ac_ev_min_current_a = 6;
    vehicle.ac_max_charge_power_w = 11040;
    vehicle.ac_min_charge_power_w = 4140;
    vehicle.ac_max_discharge_power_w = 11040;
    vehicle.ac_min_discharge_power_w = 4140;
    vehicle.dc_max_discharge_power_w = 150000;
    vehicle.dc_max_discharge_current_a = 300;
    vehicle.der_functions.dso_q_setpoint_provision = true;
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

IsoStart start_of(CM mode) {
    IsoStart start;
    start.mode = mode;
    start.departure_time_s = 3600;
    start.e_amount_wh = 25500;
    return start;
}

BptParams bpt(float power, float current) {
    return BptParams{current, power, 0.0f, 20.0f};
}

iso15118::ev::config::PausedSession paused_session(std::uint8_t id) {
    return {{id, 0, 0, 0, 0, 0, 0, 0}, ProtocolId::ISO15118_20, iso15118::v2gtp::Security::NO_TRANSPORT_SECURITY, {}};
}

// One IsoSession on a freshly reset fake, with the events its sessions enqueue.
struct Fixture {
    everest::lib::util::thread_safe_queue<Event> queue;
    IsoSession iso;

    explicit Fixture(const std::string& protocols = "ISO15118_20,ISO15118_2,DIN70121") :
        iso((fake_controller::reset(), settings_with(protocols)), [this](Event ev) { queue.push(std::move(ev)); }) {
        iso.set_dc_params(dc_params());
    }
    ~Fixture() {
        fake_controller::reset();
    }

    // The events of the running session up to and including its finish, which the loop admits.
    std::vector<Event> finish() {
        std::vector<Event> events;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            auto ev = queue.try_pop(std::chrono::milliseconds(50));
            if (not ev) {
                continue;
            }
            events.push_back(*ev);
            if (std::holds_alternative<IsoV2GFinishedEvt>(ev->payload)) {
                REQUIRE(iso.admit(ev->iso_session, true));
                return events;
            }
        }
        FAIL("no IsoV2GFinished");
        return events;
    }

    // Starts `start`, lets the fake run `body` as its loop, and returns the session's events.
    std::vector<Event> run(const IsoStart& start, fake_controller::LoopBody body = nullptr) {
        fake_controller::set_loop_body(std::move(body));
        REQUIRE(iso.start_charging(start));
        return finish();
    }

    iso15118::ev::config::EvConfig last_config() {
        const auto built = fake_controller::constructions();
        REQUIRE_FALSE(built.empty());
        return built.back().config;
    }
};

template <class T> std::vector<T> payloads(const std::vector<Event>& events) {
    std::vector<T> found;
    for (const auto& ev : events) {
        if (const auto* payload = std::get_if<T>(&ev.payload)) {
            found.push_back(*payload);
        }
    }
    return found;
}

} // namespace

TEST_CASE("each charge mode builds its session", "[evctl][iso_session_fake]") {
    struct Case {
        CM mode;
        std::optional<BptParams> bpt;
        bool mcs;
        bool three_phases;
        Service service;
        Iso2Mode iso2_mode;
        std::vector<ProtocolId> protocols;
    };
    const std::vector<ProtocolId> d20_dc{ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121};
    const std::vector<ProtocolId> d20_ac{ProtocolId::ISO15118_20, ProtocolId::ISO15118_2};
    const auto c = GENERATE_COPY(
        Case{CM::DcIsoD20, std::nullopt, false, true, Service::DC, Iso2Mode::DC_extended, d20_dc},
        Case{CM::DcIsoD20, bpt(5000, 10), false, true, Service::DC_BPT, Iso2Mode::DC_extended, d20_dc},
        Case{CM::DcIsoD20, std::nullopt, true, true, Service::MCS, Iso2Mode::DC_extended, d20_dc},
        Case{CM::DcIsoD20, bpt(5000, 10), true, true, Service::MCS_BPT, Iso2Mode::DC_extended, d20_dc},
        Case{CM::AcIsoD20, std::nullopt, false, true, Service::AC, Iso2Mode::AC_three_phase_core, d20_ac},
        Case{CM::AcIsoD20, std::nullopt, false, false, Service::AC, Iso2Mode::AC_single_phase_core, d20_ac},
        Case{CM::AcIsoD20, bpt(5000, 10), false, true, Service::AC_BPT, Iso2Mode::AC_three_phase_core, d20_ac},
        Case{CM::AcIsoD20DerIec, std::nullopt, false, true, Service::AC_DER_IEC, Iso2Mode::AC_three_phase_core, d20_ac},
        Case{CM::AcIsoD20DerSae, std::nullopt, false, true, Service::AC_DER_SAE, Iso2Mode::AC_three_phase_core, d20_ac},
        Case{CM::AcIso2,
             std::nullopt,
             false,
             true,
             Service::AC,
             Iso2Mode::AC_three_phase_core,
             {ProtocolId::ISO15118_2}},
        Case{CM::AcIso2,
             bpt(5000, 10),
             false,
             false,
             Service::AC,
             Iso2Mode::AC_single_phase_core,
             {ProtocolId::ISO15118_2}},
        Case{CM::DcIso2,
             std::nullopt,
             false,
             true,
             Service::DC,
             Iso2Mode::DC_extended,
             {ProtocolId::ISO15118_2, ProtocolId::DIN70121}});
    Fixture f;
    auto start = start_of(c.mode);
    start.bpt = c.bpt;
    start.mcs = c.mcs;
    start.three_phases = c.three_phases;

    f.run(start);

    const auto config = f.last_config();
    CHECK(config.energy_service == c.service);
    CHECK(config.params.energy_transfer_mode == c.iso2_mode);
    CHECK(config.supported_protocols == c.protocols);
    // EAmount in Wh, as configured: no kWh round trip.
    CHECK(config.params.energy_request.energy_wh == 25500.0f);
    CHECK(config.params.ac.e_amount == 25500.0f);
}

TEST_CASE("a session gets the vehicle's parameters for its mode", "[evctl][iso_session_fake]") {
    Fixture f;

    SECTION("DC BPT asks for the session's discharge limits") {
        auto start = start_of(CM::DcIsoD20);
        start.bpt = bpt(7400, 16);
        f.run(start);
        const auto dc = fake_controller::constructions().back().dc_params;
        CHECK(dc.max_discharge_power == 7400.0f);
        CHECK(dc.max_discharge_current == 16.0f);
    }

    SECTION("a single-phase AC session asks for one line and its share") {
        auto start = start_of(CM::AcIsoD20);
        start.three_phases = false;
        f.run(start);
        const auto ac = fake_controller::constructions().back().ac_params;
        CHECK(ac.phase_count == 1);
        CHECK(ac.max_charge_power == 11040.0f / 3.0f);
    }

    SECTION("an ISO 15118-2 AC session carries the configured line limits") {
        f.run(start_of(CM::AcIso2));
        const auto ac = f.last_config().params.ac;
        CHECK(ac.ev_max_voltage == 230.0f);
        CHECK(ac.ev_max_current == 16.0f);
        CHECK(ac.ev_min_current == 6.0f);
    }

    SECTION("an IEC DER session declares the configured functions") {
        f.run(start_of(CM::AcIsoD20DerIec));
        CHECK(f.last_config().d20.der_control_functions.dso_q_setpoint_provision);
        CHECK_FALSE(f.last_config().d20.der_control_functions.volt_var_mode);
    }

    SECTION("a rejected start leaves no parameters behind") {
        auto bad = start_of(CM::DcIsoD20);
        bad.bpt = bpt(-1, 16);
        CHECK_FALSE(f.iso.start_charging(bad));
        f.run(start_of(CM::DcIsoD20));
        CHECK(fake_controller::constructions().size() == 1);
    }
}

TEST_CASE("a mode the configured protocols cannot run is refused", "[evctl][iso_session_fake]") {
    SECTION("-20 modes need ISO15118_20") {
        Fixture f{"ISO15118_2,DIN70121"};
        CHECK(f.iso.cannot_run(CM::DcIsoD20).has_value());
        CHECK(f.iso.cannot_run(CM::AcIsoD20DerSae).has_value());
        CHECK_FALSE(f.iso.cannot_run(CM::DcIso2).has_value());
        CHECK_FALSE(f.iso.start_charging(start_of(CM::DcIsoD20)));
    }

    SECTION("AC ISO 15118-2 needs ISO15118_2; DIN SPEC 70121 is DC only") {
        Fixture f{"ISO15118_20,DIN70121"};
        CHECK(f.iso.cannot_run(CM::AcIso2).has_value());
        CHECK_FALSE(f.iso.cannot_run(CM::DcIso2).has_value());
        CHECK_FALSE(f.iso.start_charging(start_of(CM::AcIso2)));
    }

    SECTION("AcIec runs no session and is not refused here") {
        Fixture f{"ISO15118_20"};
        CHECK_FALSE(f.iso.cannot_run(CM::AcIec).has_value());
        CHECK_FALSE(f.iso.start_charging(start_of(CM::AcIec)));
    }

    CHECK(fake_controller::constructions().empty());
}

TEST_CASE("a start without the HLC interface or a Controller is rejected", "[evctl][iso_session_fake]") {
    Fixture f;

    SECTION("no interface") {
        fake_controller::set_interface_present(false);
        CHECK_FALSE(f.iso.start_charging(start_of(CM::DcIsoD20)));
    }

    SECTION("the Controller throws") {
        fake_controller::set_throw_on_construct(true);
        CHECK_FALSE(f.iso.start_charging(start_of(CM::DcIsoD20)));
        fake_controller::set_throw_on_construct(false);
        CHECK(f.run(start_of(CM::DcIsoD20)).size() == 1);
    }
}

TEST_CASE("the session's feedback reaches the loop as its tagged events", "[evctl][iso_session_fake]") {
    Fixture f;
    const auto events = f.run(start_of(CM::AcIsoD20), [] {
        fake_controller::offer_ac_evse_max_current(16.0f);
        fake_controller::stop_power_delivery();
        fake_controller::fail_welding_detection(120.5f, 3);
    });

    const auto session = events.back().iso_session;
    CHECK(session != 0);
    for (const auto& ev : events) {
        CHECK(ev.iso_session == session);
    }
    const auto max_current = payloads<IsoAcMaxCurrentEvt>(events);
    REQUIRE(max_current.size() == 1);
    CHECK(max_current.front().max_current_a == 16.0f);
    CHECK(payloads<IsoPowerDeliveryStoppedEvt>(events).size() == 1);
    const auto welding = payloads<IsoWeldingDetectionFailedEvt>(events);
    REQUIRE(welding.size() == 1);
    CHECK(welding.front().failure.cycles == 3);
    CHECK(welding.front().failure.last_voltage_V == 120.5f);
}

TEST_CASE("DER blocks reach the loop", "[evctl][iso_session_fake]") {
    Fixture f;
    iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode control{};
    control.dso_q_setpoint = iso15118::message_20::datatypes::DsoQSetpoint{};
    control.dso_q_setpoint->dso_q_setpoint_value = {500, 0};
    const auto events = f.run(start_of(CM::AcIsoD20DerIec), [control] {
        fake_controller::report_iec_demanded_functions(0b1);
        fake_controller::receive_der_control(control);
    });

    const auto negotiated = payloads<IsoDerNegotiatedEvt>(events);
    REQUIRE(negotiated.size() == 1);
    CHECK(negotiated.front().value.flavor == ::types::iso15118::DerFlavor::AC_DER_IEC);
    const auto received = payloads<IsoDerControlEvt>(events);
    REQUIRE(received.size() == 1);
    CHECK(received.front().value.dso_q_setpoint == 500.0f);
}

TEST_CASE("stop, pause and resume", "[evctl][iso_session_fake]") {
    Fixture f;

    SECTION("stop and pause reach the running Controller") {
        bool stop_seen = false;
        fake_controller::set_loop_body([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (not stop_seen and std::chrono::steady_clock::now() < deadline) {
                stop_seen = fake_controller::stop_requests() > 0;
            }
        });
        REQUIRE(f.iso.start_charging(start_of(CM::DcIsoD20)));
        f.iso.pause_charging();
        f.iso.stop_charging();
        f.finish();
        CHECK(stop_seen);
        CHECK(fake_controller::pause_requests() == 1);
    }

    SECTION("a session that ended paused is resumed by the next start") {
        fake_controller::set_paused_session(paused_session(7));
        f.run(start_of(CM::DcIsoD20));
        fake_controller::set_paused_session(std::nullopt);

        f.run(start_of(CM::DcIsoD20));

        const auto built = fake_controller::constructions();
        REQUIRE(built.size() == 2);
        CHECK_FALSE(built[0].resume.has_value());
        REQUIRE(built[1].resume.has_value());
        CHECK(built[1].resume->session_id[0] == 7);

        // Taken once: the resumed session ended without a pause.
        f.run(start_of(CM::DcIsoD20));
        CHECK_FALSE(fake_controller::constructions().back().resume.has_value());
    }

    SECTION("a session that failed leaves nothing to resume") {
        fake_controller::set_paused_session(paused_session(7));
        f.run(start_of(CM::DcIsoD20), [] { throw std::runtime_error("session failed"); });
        fake_controller::set_paused_session(std::nullopt);

        f.run(start_of(CM::DcIsoD20));
        CHECK_FALSE(fake_controller::constructions().back().resume.has_value());
    }

    SECTION("a stop discards the pause, even one the running session ends with") {
        fake_controller::set_paused_session(paused_session(7));
        bool stopped = false;
        fake_controller::set_loop_body([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (not stopped and std::chrono::steady_clock::now() < deadline) {
                stopped = fake_controller::stop_requests() > 0;
            }
        });
        REQUIRE(f.iso.start_charging(start_of(CM::DcIsoD20)));
        f.iso.stop_charging();
        f.finish();

        f.run(start_of(CM::DcIsoD20));
        CHECK_FALSE(fake_controller::constructions().back().resume.has_value());
    }

    SECTION("a stop between sessions discards the kept pause") {
        fake_controller::set_paused_session(paused_session(7));
        f.run(start_of(CM::DcIsoD20));
        fake_controller::set_paused_session(std::nullopt);

        f.iso.stop_charging();
        f.run(start_of(CM::DcIsoD20));

        CHECK_FALSE(fake_controller::constructions().back().resume.has_value());
    }

    SECTION("a pilot E or F terminates the running session and discards its pause") {
        fake_controller::set_paused_session(paused_session(7));
        bool terminated = false;
        fake_controller::set_loop_body([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (not terminated and std::chrono::steady_clock::now() < deadline) {
                terminated = fake_controller::terminations() > 0;
            }
        });
        REQUIRE(f.iso.start_charging(start_of(CM::DcIsoD20)));
        f.iso.cp_state_changed(::types::iso15118::CpState::E);
        f.finish();
        CHECK(terminated);

        f.run(start_of(CM::DcIsoD20));
        CHECK_FALSE(fake_controller::constructions().back().resume.has_value());
    }
}

TEST_CASE("Plug&Charge follows the session's payment", "[evctl][iso_session_fake]") {
    auto settings = settings_with("ISO15118_2");
    settings.vehicle.allow_pnc_without_tls = true;
    fake_controller::reset();
    everest::lib::util::thread_safe_queue<Event> queue;
    IsoSession iso{settings, [&queue](Event ev) { queue.push(std::move(ev)); }};
    const auto finish = [&] {
        while (true) {
            auto ev = queue.try_pop(std::chrono::seconds(10));
            REQUIRE(ev);
            if (std::holds_alternative<IsoV2GFinishedEvt>(ev->payload)) {
                REQUIRE(iso.admit(ev->iso_session, true));
                return;
            }
        }
    };
    auto contract = start_of(CM::AcIso2);
    contract.payment = PaymentOption::Contract;

    SECTION("ExternalPayment offers EIM only, Contract prefers the contract, a forced one enforces it") {
        REQUIRE(iso.start_charging(start_of(CM::AcIso2)));
        finish();
        CHECK_FALSE(fake_controller::constructions().back().pnc.prefer_contract);

        REQUIRE(iso.start_charging(contract));
        finish();
        CHECK(fake_controller::constructions().back().pnc.prefer_contract);
        CHECK_FALSE(fake_controller::constructions().back().pnc.enforce_contract);

        contract.force_payment_option = true;
        REQUIRE(iso.start_charging(contract));
        finish();
        CHECK(fake_controller::constructions().back().pnc.enforce_contract);
    }

    SECTION("a resume presents the contract its paused session installed") {
        const auto installed = test_contract::generate("DE-EVT-C00000002-B");
        fake_controller::set_loop_body([installed] {
            fake_controller::install_contract(installed.chain_pem, installed.key_pem, "DEEVTC00000002B");
        });
        fake_controller::set_paused_session(iso15118::ev::config::PausedSession{
            {1}, ProtocolId::ISO15118_2, iso15118::v2gtp::Security::NO_TRANSPORT_SECURITY});
        REQUIRE(iso.start_charging(contract));
        finish();
        fake_controller::set_loop_body(nullptr);
        fake_controller::set_paused_session(std::nullopt);

        REQUIRE(iso.start_charging(contract));
        finish();
        const auto resumed = fake_controller::constructions().back().pnc.contract;
        REQUIRE(resumed.has_value());
        CHECK(resumed->emaid == "DEEVTC00000002B");

        // The chain of pauses ended: the next session no longer presents it.
        REQUIRE(iso.start_charging(contract));
        finish();
        CHECK_FALSE(fake_controller::constructions().back().pnc.contract.has_value());
    }
    fake_controller::reset();
}

TEST_CASE("the loop's calls reach the running Controller and seed the next one", "[evctl][iso_session_fake]") {
    Fixture f;
    bool done = false;
    fake_controller::set_loop_body([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (not done and std::chrono::steady_clock::now() < deadline) {
            done = fake_controller::terminations() > 0;
        }
    });
    REQUIRE(f.iso.start_charging(start_of(CM::AcIsoD20)));
    ::types::iso15118::EvPresentValues values;
    values.present_voltage = 231.0f;
    values.present_frequency = 50.1f;
    values.der_alarm_status = 2;
    f.iso.update_present_values(values);
    f.iso.update_soc(42.0f);
    f.iso.abort_charging();
    f.finish();

    const auto calls = fake_controller::channel_calls();
    CHECK(calls.present_voltage == std::vector<float>{231.0f});
    CHECK(calls.present_frequency == std::vector<float>{50.1f});
    CHECK(calls.der_alarm_status == std::vector<std::uint32_t>{2});
    CHECK(calls.present_soc == std::vector<double>{42.0});
}
