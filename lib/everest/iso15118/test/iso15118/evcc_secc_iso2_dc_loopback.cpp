// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// In-process EVCC <-> SECC loopback integration test: the real SECC TbdController (no SDP server,
// plain TCP on lo:50000) and the EvController (no SDP, direct endpoint) drive a full ISO 15118-2 DC
// session against each other over the loopback interface, exercising the complete wire path (EXI
// codec, V2GTP framing, TCP). Mirrors evcc_secc_dc_loopback.cpp for the ISO 15118-2 stack.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <arpa/inet.h>

#include <iso15118/ev_controller.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/session/logger.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/tbd_controller.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;
namespace m2 = message_2::datatypes;

namespace {

constexpr auto SECC_PORT = 50000;
constexpr auto EV_TARGET_VOLTAGE = 400.0f;

io::Ipv6EndPoint loopback_endpoint(uint16_t port) {
    io::Ipv6EndPoint endpoint{};
    endpoint.port = port;
    in6_addr addr{};
    REQUIRE(inet_pton(AF_INET6, "::1", &addr) == 1);
    std::memcpy(endpoint.address, &addr, sizeof(endpoint.address));
    return endpoint;
}

d20::DcTransferLimits make_secc_dc_limits() {
    d20::DcTransferLimits limits;
    limits.charge_limits.power = {dt::from_float(360000.0f), dt::from_float(0.0f)};
    limits.charge_limits.current = {dt::from_float(400.0f), dt::from_float(0.0f)};
    limits.voltage = {dt::from_float(920.0f), dt::from_float(0.0f)};
    return limits;
}

session::EvseSetupConfig make_secc_setup() {
    session::EvseSetupConfig setup;
    setup.evse_id = "DE*PNX*E12345*1";
    setup.supported_energy_services = {dt::ServiceCategory::DC};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.enable_certificate_install_service = false;
    setup.dc_limits = make_secc_dc_limits();
    setup.powersupply_limits = make_secc_dc_limits();
    setup.control_mobility_modes = {{dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc}};
    setup.supported_protocols = {ProtocolId::ISO15118_2};
    return setup;
}

session::EvSetupConfig make_ev_setup() {
    session::EvSetupConfig setup;
    setup.evcc_id = "WMIV1234567890ABCDEX";
    setup.supported_energy_services = {dt::ServiceCategory::DC};
    setup.supported_auth_options = {dt::Authorization::EIM};
    setup.supported_protocols = {ProtocolId::ISO15118_2};
    setup.iso2_energy_transfer_mode = m2::EnergyTransferMode::DC_extended;

    auto& dc = setup.dc_charge_parameters;
    dc.max_charge_power = dt::from_float(150000.0f);
    dc.min_charge_power = dt::from_float(100.0f);
    dc.max_charge_current = dt::from_float(300.0f);
    dc.min_charge_current = dt::from_float(10.0f);
    dc.max_voltage = dt::from_float(900.0f);
    dc.min_voltage = dt::from_float(10.0f);
    dc.target_voltage = dt::from_float(EV_TARGET_VOLTAGE);
    dc.target_current = dt::from_float(20.0f);
    dc.energy_capacity = dt::from_float(60000.0f);
    return setup;
}

template <typename Predicate> bool wait_for(Predicate&& predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

struct Recorder {
    std::mutex mutex;
    std::vector<std::string> ev_feedback_order;
    std::vector<session::feedback::Signal> secc_signals;
    std::atomic<int> ev_session_finished_count{0};
    std::atomic<bool> ev_dlink_error{false};
    std::atomic<bool> ev_power_ready{false};
    std::atomic<bool> ev_dc_power_on{false};
    std::atomic<bool> ev_charge_loop_started{false};
    std::atomic<bool> ev_selected_protocol_iso2_dc{false};
    std::atomic<bool> secc_dc_max_limits_seen{false};
    std::atomic<bool> secc_loop_target_current_seen{false};

    void record_ev(const std::string& entry) {
        std::lock_guard<std::mutex> lock(mutex);
        ev_feedback_order.push_back(entry);
    }
    void record_secc(session::feedback::Signal signal) {
        std::lock_guard<std::mutex> lock(mutex);
        secc_signals.push_back(signal);
    }
    bool secc_has_signal(session::feedback::Signal signal) {
        std::lock_guard<std::mutex> lock(mutex);
        return std::find(secc_signals.begin(), secc_signals.end(), signal) != secc_signals.end();
    }
    std::vector<std::string> ev_order() {
        std::lock_guard<std::mutex> lock(mutex);
        return ev_feedback_order;
    }
};

} // namespace

SCENARIO("EVCC and SECC complete a full ISO 15118-2 DC session over an in-process TCP loopback") {
    if (std::getenv("LOOPBACK_TEST_VERBOSE") != nullptr) {
        session::logging::set_session_log_callback([](std::size_t id, const session::logging::Event& event) {
            if (const auto* simple = std::get_if<session::logging::SimpleEvent>(&event)) {
                printf("[session %zu] %s\n", id, simple->info.c_str());
            }
        });
    } else {
        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
    }
    io::set_logging_callback([](LogLevel, std::string) {});

    Recorder rec;

    // --- SECC side ---
    std::atomic<TbdController*> secc_ptr{nullptr};

    session::feedback::Callbacks secc_callbacks{};
    secc_callbacks.signal = [&](session::feedback::Signal signal) {
        rec.record_secc(signal);
        auto* secc = secc_ptr.load();
        if (not secc) {
            return;
        }
        switch (signal) {
        case session::feedback::Signal::REQUIRE_AUTH_EIM:
            secc->send_control_event(d20::AuthorizationResponse{true});
            break;
        case session::feedback::Signal::START_CABLE_CHECK:
            secc->send_control_event(d20::CableCheckFinished{true});
            break;
        case session::feedback::Signal::PRE_CHARGE_STARTED:
            secc->send_control_event(d20::PresentVoltageCurrent{EV_TARGET_VOLTAGE, 0.0f});
            break;
        case session::feedback::Signal::CHARGE_LOOP_STARTED:
            secc->send_control_event(d20::PresentVoltageCurrent{EV_TARGET_VOLTAGE, 20.0f});
            break;
        case session::feedback::Signal::CHARGE_LOOP_FINISHED:
            // After PowerDelivery(off) the EV returns to CP state B; the SECC parks WeldingDetection/
            // SessionStop requests until it is reported ([V2G2-919]/[V2G2-921]).
            secc->send_control_event(d20::CpStateChanged{d20::CpState::B});
            break;
        default:
            break;
        }
    };
    secc_callbacks.dc_pre_charge_target_voltage = [&](float target) {
        auto* secc = secc_ptr.load();
        if (secc) {
            secc->send_control_event(d20::PresentVoltageCurrent{target, 0.0f});
        }
    };
    // The SECC forwards the EV's advertised maxima at ChargeParameterDiscovery and the target V/I during
    // the charge loop (the fixes for the SIL 500V fallback and the 0 A charge-loop setpoint).
    secc_callbacks.dc_max_limits = [&](const session::feedback::DcMaximumLimits&) {
        rec.secc_dc_max_limits_seen.store(true);
        // The EV applies CP state C after ChargeParameterDiscovery; the SECC parks CableCheckReq until
        // it measures C/D ([V2G2-917]/[V2G2-918]).
        if (auto* secc = secc_ptr.load()) {
            secc->send_control_event(d20::CpStateChanged{d20::CpState::C});
        }
    };
    secc_callbacks.dc_charge_loop_req = [&](const session::feedback::DcChargeLoopReq& req) {
        if (const auto* mode = std::get_if<session::feedback::DcReqControlMode>(&req)) {
            if (const auto* scheduled = std::get_if<dt::Scheduled_DC_CLReqControlMode>(mode)) {
                if (message_20::datatypes::from_RationalNumber(scheduled->target_current) > 0.0) {
                    rec.secc_loop_target_current_seen.store(true);
                }
            }
        }
    };

    TbdConfig secc_config;
    secc_config.interface_name = "lo";
    secc_config.enable_sdp_server = false;

    TbdController secc(secc_config, secc_callbacks, make_secc_setup());
    secc_ptr.store(&secc);

    std::thread secc_thread([&]() { secc.loop(); });

    // --- EV side ---
    session::ev::feedback::Callbacks ev_callbacks{};
    ev_callbacks.ev_power_ready = [&](bool ready) {
        if (ready and not rec.ev_power_ready.exchange(true)) {
            rec.record_ev("ev_power_ready");
        }
    };
    ev_callbacks.dc_power_on = [&]() {
        if (not rec.ev_dc_power_on.exchange(true)) {
            rec.record_ev("dc_power_on");
        }
    };
    ev_callbacks.v2g_session_finished = [&]() {
        rec.ev_session_finished_count++;
        rec.record_ev("v2g_session_finished");
    };
    ev_callbacks.signal = [&](session::ev::feedback::Signal signal) {
        if (signal == session::ev::feedback::Signal::CHARGE_LOOP_STARTED) {
            if (not rec.ev_charge_loop_started.exchange(true)) {
                rec.record_ev("charge_loop_started");
            }
        } else if (signal == session::ev::feedback::Signal::DLINK_ERROR) {
            rec.ev_dlink_error.store(true);
        }
    };
    ev_callbacks.selected_protocol = [&](const std::string& protocol) {
        if (protocol == "ISO15118-2:DC") {
            rec.ev_selected_protocol_iso2_dc.store(true);
        }
    };

    EvConfig ev_config;
    ev_config.interface_name = "lo";
    ev_config.enable_sdp = false;
    ev_config.direct_secc_endpoint = loopback_endpoint(SECC_PORT);
    ev_config.use_tls = false;

    EvController ev(ev_config, ev_callbacks, make_ev_setup());

    std::thread ev_thread([&]() { ev.loop(); });

    bool started = false;
    for (int attempt = 0; attempt < 10 and not started; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        rec.ev_dlink_error.store(false);
        if (not ev.start_charging(dt::ServiceCategory::DC, m2::EnergyTransferMode::DC_extended)) {
            continue;
        }
        started = wait_for([&]() { return rec.ev_power_ready.load() or rec.ev_dlink_error.load(); },
                           std::chrono::seconds(10)) and
                  not rec.ev_dlink_error.load();
    }
    REQUIRE(started);

    // ISO 15118-2 DC was negotiated and reported to the driver.
    REQUIRE(rec.ev_selected_protocol_iso2_dc.load());

    REQUIRE(wait_for([&]() { return rec.ev_dc_power_on.load(); }, std::chrono::seconds(10)));
    REQUIRE(wait_for([&]() { return rec.ev_charge_loop_started.load(); }, std::chrono::seconds(10)));

    // Feed the EV with SoC/measurement updates so the CurrentDemand loop runs for a while.
    uint8_t soc = 42;
    wait_for(
        [&]() {
            ev.update_soc(soc);
            ev.update_present_voltage_current(EV_TARGET_VOLTAGE, 20.0f);
            return false;
        },
        std::chrono::seconds(2));

    // EV-initiated stop: PowerDelivery(Stop) -> WeldingDetection -> SessionStop(Terminate).
    ev.stop_charging();

    REQUIRE(wait_for([&]() { return rec.ev_session_finished_count.load() >= 1; }, std::chrono::seconds(20)));

    // --- EV feedback ordering contract ---
    const auto order = rec.ev_order();
    REQUIRE(order.size() == 4);
    REQUIRE(order[0] == "ev_power_ready");
    REQUIRE(order[1] == "dc_power_on");
    REQUIRE(order[2] == "charge_loop_started");
    REQUIRE(order[3] == "v2g_session_finished");
    REQUIRE(rec.ev_session_finished_count.load() == 1);

    // --- SECC side reached the end of the charging phase cleanly ---
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::REQUIRE_AUTH_EIM));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::START_CABLE_CHECK));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::PRE_CHARGE_STARTED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::SETUP_FINISHED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::CHARGE_LOOP_STARTED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::CHARGE_LOOP_FINISHED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::DC_OPEN_CONTACTOR));

    // The SECC forwarded the EV maxima (ChargeParameterDiscovery) and a non-zero target current (loop).
    REQUIRE(rec.secc_dc_max_limits_seen.load());
    REQUIRE(rec.secc_loop_target_current_seen.load());

    // --- Teardown ---
    ev.shutdown();
    secc.shutdown();
    ev_thread.join();
    secc_thread.join();
}
