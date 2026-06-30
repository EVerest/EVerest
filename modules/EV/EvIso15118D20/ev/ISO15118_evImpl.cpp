// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include <chrono>
#include <exception>
#include <utility>

#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/session/logger.hpp>

namespace {
template <class F> class ScopeGuard {
public:
    explicit ScopeGuard(F f) : m_f(std::move(f)) {
    }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ~ScopeGuard() {
        m_f();
    }

private:
    F m_f;
};
} // namespace

namespace module {
namespace ev {

void ISO15118_evImpl::init() {
    iso15118::io::set_logging_callback([](const iso15118::LogLevel& level, const std::string& msg) {
        switch (level) {
        case iso15118::LogLevel::Error:
            EVLOG_error << msg;
            break;
        case iso15118::LogLevel::Warning:
            EVLOG_warning << msg;
            break;
        case iso15118::LogLevel::Info:
            EVLOG_info << msg;
            break;
        case iso15118::LogLevel::Debug:
            EVLOG_debug << msg;
            break;
        case iso15118::LogLevel::Trace:
            EVLOG_verbose << msg;
            break;
        default:
            EVLOG_critical << "(Loglevel not defined) - " << msg;
            break;
        }
    });

    iso15118::session::logging::set_session_log_callback([](std::size_t id,
                                                            const iso15118::session::logging::Event& event) {
        if (const auto* simple = std::get_if<iso15118::session::logging::SimpleEvent>(&event)) {
            EVLOG_debug << "EV session " << id << ": " << simple->info;
        } else if (const auto* exi = std::get_if<iso15118::session::logging::ExiMessageEvent>(&event)) {
            EVLOG_debug << "EV session " << id << ": EXI "
                        << (exi->direction == iso15118::session::logging::ExiMessageDirection::FROM_EV ? "FROM_EV"
                                                                                                       : "TO_EV")
                        << " payload 0x" << std::hex << exi->payload_type << std::dec << " (" << exi->len << " bytes)";
        }
    });
}

void ISO15118_evImpl::ready() {
    worker = std::thread([this] { session_worker(); });
}

ISO15118_evImpl::~ISO15118_evImpl() {
    {
        auto h = session.handle();
        (*h).shutting_down = true;
        if ((*h).current) {
            (*h).current->shutdown();
        }
    }
    session.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

iso15118::ev::EvConfig ISO15118_evImpl::make_ev_config() const {
    iso15118::ev::EvConfig ev_config;

    std::string interface_name = mod->config.device;
    if (not iso15118::io::check_and_update_interface(interface_name)) {
        EVLOG_error << "EvIso15118D20: no usable IPv6 interface for device '" << mod->config.device << "'";
    }
    ev_config.interface_name = interface_name;

    ev_config.evcc_id = mod->config.evcc_id;
    ev_config.response_timeout = std::chrono::milliseconds(mod->config.response_timeout_ms);
    ev_config.advertised_security = iso15118::io::v2gtp::Security::NO_TRANSPORT_SECURITY;

    if (not mod->config.fixed_endpoint.empty()) {
        EVLOG_warning << "EvIso15118D20: fixed_endpoint is configured but not yet supported; using SDP discovery";
    }
    ev_config.discover = true;

    return ev_config;
}

iso15118::ev::feedback::Callbacks ISO15118_evImpl::make_callbacks() {
    iso15118::ev::feedback::Callbacks callbacks;

    callbacks.connected = [](const iso15118::io::Ipv6EndPoint&) { EVLOG_info << "EvIso15118D20: connected to SECC"; };

    callbacks.v2g_message = [](iso15118::message_20::Type type) {
        EVLOG_debug << "EvIso15118D20: V2G message " << static_cast<int>(type);
    };

    callbacks.session_setup_response = [](const iso15118::message_20::SessionSetupResponse&) {
        EVLOG_debug << "EvIso15118D20: SessionSetupResponse received";
    };

    callbacks.evse_session_info = [](const iso15118::ev::d20::EVSESessionInfo&) {
        EVLOG_debug << "EvIso15118D20: EVSE session info received";
    };

    callbacks.timed_out = [] { EVLOG_warning << "EvIso15118D20: response watchdog timed out"; };

    callbacks.stopped = [] { EVLOG_debug << "EvIso15118D20: session stopped"; };

    return callbacks;
}

void ISO15118_evImpl::session_worker() {
    while (true) {
        {
            auto h = session.handle();
            h.wait([&] { return (*h).phase == SessionPhase::requested || (*h).shutting_down; });
            if ((*h).shutting_down) {
                return;
            }
        }
        run_one_session();
        // published after run_one_session() has reset phase to idle, so a consumer
        // that starts a new session in response is not rejected by the phase guard
        publish_v2g_session_finished(nullptr);
    }
}

void ISO15118_evImpl::run_one_session() {
    try {
        iso15118::ev::Controller controller(make_ev_config(), make_callbacks());
        // declared after the controller, so it runs before ~Controller on every
        // exit path, clearing the off-thread pointer while the object is still alive
        ScopeGuard clear_current{[this] {
            auto h = session.handle();
            (*h).current = nullptr;
        }};
        {
            auto h = session.handle();
            (*h).current = &controller;
            (*h).phase = SessionPhase::running;
        }
        controller.loop();
    } catch (const std::exception& e) {
        EVLOG_error << "EvIso15118D20: session failed: " << e.what();
    }
    auto h = session.handle();
    (*h).phase = SessionPhase::idle;
}

bool ISO15118_evImpl::handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                            types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                            double& DepartureTime, double& EAmount) {
    EVLOG_info << "EvIso15118D20: start_charging requested (DC-only M0; negotiation arguments ignored)";
    {
        auto h = session.handle();
        if ((*h).phase != SessionPhase::idle) {
            EVLOG_warning << "EvIso15118D20: a session is already active; ignoring start_charging";
            return false;
        }
        (*h).phase = SessionPhase::requested;
    }
    session.notify_all();
    return true;
}

void ISO15118_evImpl::handle_stop_charging() {
    auto h = session.handle();
    if ((*h).current) {
        (*h).current->post_control_event(iso15118::ev::d20::ControlEvent{iso15118::ev::d20::StopCharging{true}});
        (*h).current->shutdown();
    }
}

void ISO15118_evImpl::handle_pause_charging() {
    EVLOG_info << "EvIso15118D20: pause_charging: deferred to M1+";
}

void ISO15118_evImpl::handle_set_fault() {
    EVLOG_info << "EvIso15118D20: set_fault: deferred to M1+";
}

void ISO15118_evImpl::handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) {
    EVLOG_info << "EvIso15118D20: set_dc_params: deferred to M1+";
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    EVLOG_info << "EvIso15118D20: set_bpt_dc_params: deferred to M1+";
}

void ISO15118_evImpl::handle_enable_sae_j2847_v2g_v2h() {
    EVLOG_info << "EvIso15118D20: enable_sae_j2847_v2g_v2h: deferred to M1+";
}

void ISO15118_evImpl::handle_update_soc(double& SoC) {
    EVLOG_info << "EvIso15118D20: update_soc: deferred to M1+";
}

} // namespace ev
} // namespace module
