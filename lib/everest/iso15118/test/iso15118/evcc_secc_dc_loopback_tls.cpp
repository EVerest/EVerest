// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// In-process EVCC <-> SECC loopback integration test over mutual TLS 1.3: the same full ISO 15118-20
// DC session as evcc_secc_dc_loopback.cpp, but the transport is ConnectionClientSSL (EV, vehicle leaf)
// <-> ConnectionSSL (SECC, SECC leaf) with V2G-root verification enabled and TLS 1.3 enforced on both
// sides.
//
// The SECC-side TbdController only wires a plain ConnectionPlain on its no-SDP path, so it cannot serve
// TLS directly. To keep the SECC behavior unchanged we drive a SECC-side Session directly over a
// ConnectionSSL, mirroring TbdController::loop() (single, non-re-armed session). No SECC logic is
// modified.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <arpa/inet.h>

#include <iso15118/config.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/ev_controller.hpp>
#include <iso15118/io/connection_ssl.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

constexpr auto SECC_PORT = 50000; // ConnectionSSL binds the TLS port 50000
constexpr auto EV_TARGET_VOLTAGE = 400.0f;
constexpr auto DEFAULT_PW = "123456";

std::string pki(const std::string& relative) {
    return std::string(PKI_PATH) + "/" + relative;
}

config::SSLConfig secc_ssl_config() {
    config::SSLConfig config;
    config.backend = config::CertificateBackend::EVEREST_LAYOUT;
    config.path_certificate_chain = pki("certs/client/cso/CPO_CERT_CHAIN.pem");
    config.path_certificate_key = pki("certs/client/cso/SECC_LEAF.key");
    config.private_key_password = DEFAULT_PW;
    config.path_certificate_v2g_root = pki("certs/ca/v2g/V2G_ROOT_CA.pem");
    config.path_certificate_mo_root = pki("certs/ca/oem/OEM_ROOT_CA.pem");
    config.enforce_tls_1_3 = true;
    return config;
}

config::SSLConfig vehicle_ssl_config() {
    config::SSLConfig config;
    config.backend = config::CertificateBackend::EVEREST_LAYOUT;
    config.path_certificate_chain = pki("certs/ca/vehicle/VEHICLE_CERT_CHAIN.pem");
    config.path_certificate_key = pki("certs/client/vehicle/VEHICLE_LEAF.key");
    config.private_key_password = DEFAULT_PW;
    config.path_certificate_v2g_root = pki("certs/ca/v2g/V2G_ROOT_CA.pem");
    config.path_certificate_mo_root = pki("certs/ca/oem/OEM_ROOT_CA.pem");
    config.enforce_tls_1_3 = true;
    return config;
}

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

    auto& discharge = limits.discharge_limits.emplace();
    discharge.power = {dt::from_float(11000.0f), dt::from_float(0.0f)};
    discharge.current = {dt::from_float(25.0f), dt::from_float(0.0f)};
    return limits;
}

session::EvseSetupConfig make_secc_setup() {
    session::EvseSetupConfig setup;
    setup.evse_id = "DE*PNX*E12345*1";
    setup.supported_energy_services = {dt::ServiceCategory::DC, dt::ServiceCategory::DC_BPT};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.enable_certificate_install_service = false;
    setup.dc_limits = make_secc_dc_limits();
    setup.powersupply_limits = make_secc_dc_limits();
    setup.control_mobility_modes = {{dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc}};
    return setup;
}

session::EvSetupConfig make_ev_setup() {
    session::EvSetupConfig setup;
    setup.evcc_id = "WMIV1234567890ABCDEX";
    setup.supported_energy_services = {dt::ServiceCategory::DC};
    setup.preferred_control_mode = dt::ControlMode::Dynamic;
    setup.supported_auth_options = {dt::Authorization::EIM};

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

// Minimal SECC harness driving a single Session over ConnectionSSL, mirroring TbdController::loop()
// without changing SECC behavior. Control events are only pushed from within the loop thread (via the
// feedback callbacks), exactly like TbdController.
class SeccSslHarness {
public:
    SeccSslHarness(config::SSLConfig ssl, session::feedback::Callbacks callbacks_, session::EvseSetupConfig setup) :
        ssl_config(std::move(ssl)), callbacks(std::move(callbacks_)), evse_setup(std::move(setup)) {
    }

    void loop() {
        auto connection = std::make_unique<io::ConnectionSSL>(poll_manager, "lo", ssl_config);
        session = std::make_unique<Session>(std::move(connection), session::SessionConfig(evse_setup), callbacks, pause_ctx,
                                            d2_pause_ctx);

        auto next_event = get_current_time_point();
        bool shutdown_signaled = false;

        while (session or not shutdown_active.load()) {
            const auto poll_timeout_ms = get_timeout_ms_until(next_event, 50);
            try {
                poll_manager.poll(poll_timeout_ms);
            } catch (const std::runtime_error&) {
                break;
            }
            next_event = offset_time_point_by_ms(get_current_time_point(), 50);

            if (session and shutdown_active.load() and not shutdown_signaled) {
                session->request_shutdown();
                shutdown_signaled = true;
            }

            if (session) {
                try {
                    next_event = std::min(next_event, session->poll());
                } catch (const std::runtime_error&) {
                    session->close();
                }
                if (session->is_finished()) {
                    session.reset(); // single session, do not re-arm
                }
            }
        }
    }

    void shutdown() {
        shutdown_active.store(true);
        poll_manager.abort();
    }

    void send_control_event(const d20::ControlEvent& event) {
        if (session) {
            session->push_control_event(event);
        }
    }

private:
    io::PollManager poll_manager;
    config::SSLConfig ssl_config;
    session::feedback::Callbacks callbacks;
    session::EvseSetupConfig evse_setup;
    std::unique_ptr<Session> session;
    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    std::optional<d2::PauseContext> d2_pause_ctx{std::nullopt};
    std::atomic_bool shutdown_active{false};
};

struct Recorder {
    std::mutex mutex;
    std::vector<std::string> ev_feedback_order;
    std::vector<session::feedback::Signal> secc_signals;
    std::atomic<int> ev_charge_loop_res_count{0};
    std::atomic<int> ev_session_finished_count{0};
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

SCENARIO("EVCC and SECC complete a full DC session over an in-process mutual-TLS 1.3 loopback") {
    session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
    io::set_logging_callback([](LogLevel, std::string) {});

    Recorder rec;

    // --- SECC side (TLS) ---
    std::atomic<SeccSslHarness*> secc_ptr{nullptr};

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

    SeccSslHarness secc(secc_ssl_config(), secc_callbacks, make_secc_setup());
    secc_ptr.store(&secc);

    std::thread secc_thread([&]() { secc.loop(); });

    // --- EV side (TLS) ---
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
    ev_callbacks.v2g_message = [&](const V2gMessageType& type) {
        if (type == V2gMessageType{message_20::Type::DC_ChargeLoopRes}) {
            rec.ev_charge_loop_res_count++;
        }
    };

    EvConfig ev_config;
    ev_config.interface_name = "lo";
    ev_config.enable_sdp = false;
    ev_config.direct_secc_endpoint = loopback_endpoint(SECC_PORT);
    ev_config.use_tls = true;
    ev_config.verify_server_certificate = true;
    ev_config.ssl = vehicle_ssl_config();

    EvController ev(ev_config, ev_callbacks, make_ev_setup());

    std::thread ev_thread([&]() { ev.loop(); });

    bool started = false;
    for (int attempt = 0; attempt < 10 and not started; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        rec.ev_dlink_error.store(false);
        if (not ev.start_charging(dt::ServiceCategory::DC)) {
            continue;
        }
        started = wait_for([&]() { return rec.ev_power_ready.load() or rec.ev_dlink_error.load(); },
                           std::chrono::seconds(10)) and
                  not rec.ev_dlink_error.load();
    }
    REQUIRE(started);

    REQUIRE(wait_for([&]() { return rec.ev_dc_power_on.load(); }, std::chrono::seconds(10)));
    REQUIRE(wait_for([&]() { return rec.ev_charge_loop_started.load(); }, std::chrono::seconds(10)));

    uint8_t soc = 42;
    const auto loop_target_reached = wait_for(
        [&]() {
            ev.update_soc(soc);
            ev.update_present_voltage_current(EV_TARGET_VOLTAGE, 20.0f);
            return rec.ev_charge_loop_res_count.load() >= 10;
        },
        std::chrono::seconds(20));
    REQUIRE(loop_target_reached);

    ev.stop_charging();

    REQUIRE(wait_for([&]() { return rec.ev_session_finished_count.load() >= 1; }, std::chrono::seconds(20)));

    // --- Same feedback ordering contract as the plain DC loopback [flow spec §4] ---
    const auto order = rec.ev_order();
    REQUIRE(order.size() == 4);
    REQUIRE(order[0] == "ev_power_ready");
    REQUIRE(order[1] == "dc_power_on");
    REQUIRE(order[2] == "charge_loop_started");
    REQUIRE(order[3] == "v2g_session_finished");
    REQUIRE(rec.ev_session_finished_count.load() == 1);
    REQUIRE(rec.ev_charge_loop_res_count.load() >= 10);

    REQUIRE(rec.secc_has_signal(session::feedback::Signal::REQUIRE_AUTH_EIM));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::START_CABLE_CHECK));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::PRE_CHARGE_STARTED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::SETUP_FINISHED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::CHARGE_LOOP_STARTED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::CHARGE_LOOP_FINISHED));
    REQUIRE(rec.secc_has_signal(session::feedback::Signal::DC_OPEN_CONTACTOR));

    ev.shutdown();
    secc.shutdown();
    ev_thread.join();
    secc_thread.join();
}
