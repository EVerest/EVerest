// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// In-process EVCC <-> SECC loopback integration test: the real SECC TbdController (no SDP server,
// plain TCP on lo:50000) and the EvController (no SDP, direct endpoint) drive a full ISO 15118-20
// AC_BPT Dynamic session against each other over the loopback interface, exercising the complete wire
// path (EXI codec, V2GTP framing, TCP). Mirrors evcc_secc_dc_loopback.cpp for the AC branch.
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
#include <iso15118/session/logger.hpp>
#include <iso15118/tbd_controller.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

constexpr auto SECC_PORT = 50000;
constexpr auto AC_TARGET_POWER = 7000.0f;

io::Ipv6EndPoint loopback_endpoint(uint16_t port) {
    io::Ipv6EndPoint endpoint{};
    endpoint.port = port;
    in6_addr addr{};
    REQUIRE(inet_pton(AF_INET6, "::1", &addr) == 1);
    std::memcpy(endpoint.address, &addr, sizeof(endpoint.address));
    return endpoint;
}

d20::AcTransferLimits make_secc_ac_limits() {
    d20::AcTransferLimits limits;
    limits.charge_power = {dt::from_float(22000.0f), dt::from_float(0.0f)};
    limits.nominal_frequency = dt::from_float(50.0f);
    limits.discharge_power = d20::Limit<dt::RationalNumber>{dt::from_float(11000.0f), dt::from_float(0.0f)};
    return limits;
}

session::EvseSetupConfig make_secc_setup() {
    session::EvseSetupConfig setup;
    setup.evse_id = "DE*PNX*E12345*1";
    // Offer AC_BPT (and AC) so the EV's prioritized AC_BPT is selected [flow spec §0].
    setup.supported_energy_services = {dt::ServiceCategory::AC_BPT, dt::ServiceCategory::AC};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.enable_certificate_install_service = false;
    setup.ac_limits = make_secc_ac_limits();
    setup.control_mobility_modes = {{dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc}};
    setup.ac_setup_config = d20::AcSetupConfig{230, {dt::AcConnector::ThreePhase}};
    setup.bpt_setup_config =
        d20::BptSetupConfig{dt::BptChannel::Unified, dt::GeneratorMode::GridFollowing, std::nullopt};
    return setup;
}

session::EvSetupConfig make_ev_setup() {
    session::EvSetupConfig setup;
    setup.evcc_id = "WMIV1234567890ABCDEX";
    // Prioritized: AC_BPT first, then AC.
    setup.supported_energy_services = {dt::ServiceCategory::AC_BPT, dt::ServiceCategory::AC};
    setup.preferred_control_mode = dt::ControlMode::Dynamic;
    setup.supported_auth_options = {dt::Authorization::EIM};

    auto& ac = setup.ac_charge_parameters;
    ac.max_charge_power = dt::from_float(11000.0f);
    ac.min_charge_power = dt::from_float(100.0f);
    ac.max_discharge_power = dt::from_float(11000.0f);
    ac.min_discharge_power = dt::from_float(100.0f);
    return setup;
}

// Polls the predicate every 10 ms until it holds or the deadline expires.
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

// Thread-safe recorder shared between the controller threads and the test thread.
struct Recorder {
    std::mutex mutex;
    std::vector<std::string> ev_feedback_order;
    std::vector<session::feedback::Signal> secc_signals;
    std::atomic<int> ev_target_power_count{0};
    std::atomic<int> ev_target_power_expected_count{0};
    std::atomic<int> ev_session_finished_count{0};
    std::atomic<bool> secc_saw_bpt_transfer_mode{false};
    std::atomic<bool> ev_dlink_error{false};
    std::atomic<bool> ev_power_ready{false};
    std::atomic<bool> ev_dc_power_on{false};
    std::atomic<bool> ev_charge_loop_started{false};

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

SCENARIO("EVCC and SECC complete a full AC_BPT Dynamic session over an in-process TCP loopback") {
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
        case session::feedback::Signal::AC_CLOSE_CONTACTOR:
            // Close the AC contactor so the SECC PowerDelivery(Start) can respond [flow spec §PowerDelivery].
            secc->send_control_event(d20::ClosedContactor{true});
            break;
        case session::feedback::Signal::CHARGE_LOOP_STARTED: {
            // Provide a target so the SECC returns it in the charge loop responses.
            d20::AcTargetPower target;
            target.target_active_power = dt::from_float(AC_TARGET_POWER);
            secc->send_control_event(target);
            break;
        }
        default:
            break;
        }
    };
    // The SECC reports the EV's transfer mode from AC_ChargeParameterDiscoveryReq: a BPT variant proves
    // the EV selected AC_BPT (its prioritized service).
    secc_callbacks.ac_limits = [&](const session::feedback::AcLimits& limits) {
        if (std::holds_alternative<dt::BPT_AC_CPDReqEnergyTransferMode>(limits)) {
            rec.secc_saw_bpt_transfer_mode.store(true);
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
        // Must never fire on the AC branch.
        if (not rec.ev_dc_power_on.exchange(true)) {
            rec.record_ev("dc_power_on");
        }
    };
    ev_callbacks.ac_evse_target_power = [&](const d20::AcTargetPower& target) {
        rec.ev_target_power_count++;
        if (target.target_active_power.has_value() and
            dt::from_RationalNumber(target.target_active_power.value()) == AC_TARGET_POWER) {
            rec.ev_target_power_expected_count++;
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

    EvConfig ev_config;
    ev_config.interface_name = "lo";
    ev_config.enable_sdp = false;
    ev_config.direct_secc_endpoint = loopback_endpoint(SECC_PORT);
    ev_config.use_tls = false;

    EvController ev(ev_config, ev_callbacks, make_ev_setup());

    std::thread ev_thread([&]() { ev.loop(); });

    // Give the SECC a moment to bind its listen socket, then arm the EV session.
    bool started = false;
    for (int attempt = 0; attempt < 10 and not started; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        rec.ev_dlink_error.store(false);
        if (not ev.start_charging(dt::ServiceCategory::AC_BPT)) {
            continue;
        }
        started = wait_for([&]() { return rec.ev_power_ready.load() or rec.ev_dlink_error.load(); },
                           std::chrono::seconds(10)) and
                  not rec.ev_dlink_error.load();
    }
    REQUIRE(started);

    // The AC charge loop starts once PowerDelivery(Start) completed (contactor closed).
    REQUIRE(wait_for([&]() { return rec.ev_charge_loop_started.load(); }, std::chrono::seconds(10)));

    // Keep pushing target power to the SECC so the responses keep carrying the expected target, until
    // the EV received the expected target enough times.
    const auto loop_target_reached = wait_for(
        [&]() {
            auto* secc = secc_ptr.load();
            if (secc) {
                d20::AcTargetPower target;
                target.target_active_power = dt::from_float(AC_TARGET_POWER);
                secc->send_control_event(target);
            }
            return rec.ev_target_power_expected_count.load() >= 3;
        },
        std::chrono::seconds(20));
    REQUIRE(loop_target_reached);

    // EV-initiated clean stop: PowerDelivery(Stop) -> SessionStop(Terminate), no welding detection.
    ev.stop_charging();

    REQUIRE(wait_for([&]() { return rec.ev_session_finished_count.load() >= 1; }, std::chrono::seconds(20)));

    // --- Assertions on the EV feedback ordering contract [flow spec §4] ---
    const auto order = rec.ev_order();
    REQUIRE(order.size() == 3);
    REQUIRE(order[0] == "ev_power_ready");
    REQUIRE(order[1] == "charge_loop_started");
    REQUIRE(order[2] == "v2g_session_finished");
    REQUIRE(rec.ev_session_finished_count.load() == 1);
    // No dc_power_on on the AC branch.
    REQUIRE(rec.ev_dc_power_on.load() == false);
    // ac_evse_target_power fired at least 3 times with the expected target.
    REQUIRE(rec.ev_target_power_expected_count.load() >= 3);
    REQUIRE(rec.ev_target_power_count.load() >= 3);

    // The EV selected AC_BPT (its prioritized service): the SECC saw a BPT transfer mode.
    REQUIRE(rec.secc_saw_bpt_transfer_mode.load());

    // --- SECC side confirms the AC (AC_BPT) path ran cleanly ---
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::REQUIRE_AUTH_EIM));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::SETUP_FINISHED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::AC_CLOSE_CONTACTOR));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::CHARGE_LOOP_STARTED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::CHARGE_LOOP_FINISHED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::AC_OPEN_CONTACTOR));

    // --- Teardown ---
    ev.shutdown();
    secc.shutdown();
    ev_thread.join();
    secc_thread.join();
}
