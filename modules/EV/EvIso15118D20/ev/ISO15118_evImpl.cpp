// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include <chrono>
#include <exception>
#include <utility>

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

iso15118::ev::EvConfig
ISO15118_evImpl::make_ev_config(iso15118::message_20::datatypes::ServiceCategory energy_service) const {
    iso15118::ev::EvConfig ev_config;

    // ev::Controller resolves the interface name (including "auto") and throws on failure;
    // run_one_session()'s catch reports it.
    ev_config.interface_name = mod->config.device;
    ev_config.evcc_id = mod->config.evcc_id;
    ev_config.response_timeout = std::chrono::milliseconds(mod->config.response_timeout_ms);
    ev_config.advertised_security = iso15118::io::v2gtp::Security::NO_TRANSPORT_SECURITY;
    ev_config.discover = true;

    ev_config.energy_service = energy_service;
    if (energy_service == iso15118::message_20::datatypes::ServiceCategory::AC) {
        ev_config.advertised_app_protocols = {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}};
    }

    return ev_config;
}

iso15118::ev::feedback::Callbacks ISO15118_evImpl::make_callbacks() {
    iso15118::ev::feedback::Callbacks callbacks;

    callbacks.connected = [](const iso15118::io::Ipv6EndPoint&) { EVLOG_info << "EvIso15118D20: connected to SECC"; };

    callbacks.v2g_message = [](iso15118::message_20::Type type) {
        EVLOG_debug << "EvIso15118D20: V2G message " << static_cast<int>(type);
    };

    callbacks.evse_session_info = [](const iso15118::ev::d20::EVSESessionInfo&) {
        EVLOG_debug << "EvIso15118D20: EVSE session info received";
    };

    callbacks.timed_out = [] { EVLOG_warning << "EvIso15118D20: response watchdog timed out"; };

    callbacks.stopped = [] { EVLOG_debug << "EvIso15118D20: session stopped"; };

    callbacks.ev_power_ready = [this] { publish_ev_power_ready(true); };

    callbacks.dc_power_on = [this] { publish_dc_power_on(nullptr); };

    callbacks.stop_from_charger = [this] { publish_stop_from_charger(nullptr); };

    callbacks.ac_limits = [](const iso15118::message_20::datatypes::AC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "EvIso15118D20: AC EVSE limits: max charge power "
                   << dt::from_RationalNumber(limits.max_charge_power) << " W, min charge power "
                   << dt::from_RationalNumber(limits.min_charge_power) << " W";
    };

    callbacks.ac_target_power = [this](const iso15118::message_20::datatypes::Dynamic_AC_CLResControlMode& control) {
        namespace dt = iso15118::message_20::datatypes;
        types::iso15118::AcTargetPower target;
        target.target_active_power = dt::from_RationalNumber(control.target_active_power);
        if (control.target_active_power_L2) {
            target.target_active_power_L2 = dt::from_RationalNumber(*control.target_active_power_L2);
        }
        if (control.target_active_power_L3) {
            target.target_active_power_L3 = dt::from_RationalNumber(*control.target_active_power_L3);
        }
        publish_ac_evse_target_power(target);
    };

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
        iso15118::ev::DcChargeParams cached_dc_params;
        iso15118::ev::AcChargeParams cached_ac_params;
        iso15118::message_20::datatypes::ServiceCategory energy_service{
            iso15118::message_20::datatypes::ServiceCategory::DC};
        {
            auto h = session.handle();
            // teardown or a stop in the requested window (phase reset to idle) beat us here
            if ((*h).shutting_down || (*h).phase != SessionPhase::requested) {
                return;
            }
            cached_dc_params = (*h).dc_params;
            cached_ac_params = (*h).ac_params;
            energy_service = (*h).energy_service;
        }
        iso15118::ev::Controller controller(make_ev_config(energy_service), make_callbacks(), cached_dc_params,
                                            cached_ac_params);
        // declared after the controller, so it runs before ~Controller on every
        // exit path, clearing the off-thread pointer while the object is still alive
        ScopeGuard clear_current{[this] {
            auto h = session.handle();
            (*h).current = nullptr;
        }};
        {
            auto h = session.handle();
            // re-confirm under the lock: teardown or a cancel may have landed during construction
            if ((*h).shutting_down || (*h).phase != SessionPhase::requested) {
                return;
            }
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
    EVLOG_info << "EvIso15118D20: start_charging requested (negotiation arguments ignored)";

    auto energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
    bool three_phase = false;
    switch (EnergyTransferMode) {
    case types::iso15118::EnergyTransferMode::DC:
    case types::iso15118::EnergyTransferMode::DC_core:
    case types::iso15118::EnergyTransferMode::DC_extended:
    case types::iso15118::EnergyTransferMode::DC_BPT:
    case types::iso15118::EnergyTransferMode::DC_ACDP:
    case types::iso15118::EnergyTransferMode::DC_ACDP_BPT:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
        break;
    case types::iso15118::EnergyTransferMode::AC_single_phase_core:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC;
        three_phase = false;
        break;
    case types::iso15118::EnergyTransferMode::AC_three_phase_core:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC;
        three_phase = true;
        break;
    default:
        EVLOG_warning << "EvIso15118D20: rejecting start_charging with unsupported EnergyTransferMode '"
                      << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode)
                      << "'; only DC and AC single/three-phase are supported";
        return false;
    }
    {
        auto h = session.handle();
        if ((*h).phase != SessionPhase::idle) {
            EVLOG_warning << "EvIso15118D20: a session is already active; ignoring start_charging";
            return false;
        }
        (*h).energy_service = energy_service;
        if (energy_service == iso15118::message_20::datatypes::ServiceCategory::AC) {
            (*h).ac_params.max_charge_power = static_cast<float>(mod->config.ac_max_charge_power_w);
            (*h).ac_params.min_charge_power = static_cast<float>(mod->config.ac_min_charge_power_w);
            (*h).ac_params.three_phase = three_phase;
        }
        (*h).phase = SessionPhase::requested;
    }
    session.notify_all();
    return true;
}

void ISO15118_evImpl::handle_stop_charging() {
    bool cancelled = false;
    {
        auto h = session.handle();
        if ((*h).current) {
            (*h).current->request_stop();
        } else if ((*h).phase == SessionPhase::requested) {
            // stop arrived before the worker constructed the controller; cancel the
            // pending session so run_one_session() skips it under the lock
            (*h).phase = SessionPhase::idle;
            cancelled = true;
        }
    }
    if (cancelled) {
        session.notify_all();
    }
}

void ISO15118_evImpl::handle_pause_charging() {
    EVLOG_info << "EvIso15118D20: pause_charging: deferred to M1+";
}

void ISO15118_evImpl::handle_set_fault() {
    EVLOG_info << "EvIso15118D20: set_fault";
}

void ISO15118_evImpl::handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) {
    EVLOG_info << "EvIso15118D20: set_dc_params";

    std::string missing;
    const auto note_missing = [&missing](const char* name, bool present) {
        if (not present) {
            missing.append(missing.empty() ? "" : ", ").append(name);
        }
    };
    note_missing("max_power_limit", EvParameters.max_power_limit.has_value());
    note_missing("max_current_limit", EvParameters.max_current_limit.has_value());
    note_missing("max_voltage_limit", EvParameters.max_voltage_limit.has_value());
    note_missing("energy_capacity", EvParameters.energy_capacity.has_value());
    note_missing("target_voltage", EvParameters.target_voltage.has_value());
    note_missing("target_current", EvParameters.target_current.has_value());
    if (not missing.empty()) {
        // absent fields fold to 0 and would advertise a 0 W limit to the SECC
        EVLOG_warning << "EvIso15118D20: set_dc_params missing " << missing << "; defaulting to 0";
    }

    auto h = session.handle();
    auto& params = (*h).dc_params;
    params.max_charge_power = EvParameters.max_power_limit.value_or(0.0f);
    params.max_charge_current = EvParameters.max_current_limit.value_or(0.0f);
    params.max_voltage = EvParameters.max_voltage_limit.value_or(0.0f);
    params.energy_capacity = EvParameters.energy_capacity.value_or(0.0f);
    params.target_voltage = EvParameters.target_voltage.value_or(0.0f);
    params.target_current = EvParameters.target_current.value_or(0.0f);
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    EVLOG_info << "EvIso15118D20: set_bpt_dc_params: deferred to M1+";
}

void ISO15118_evImpl::handle_enable_sae_j2847_v2g_v2h() {
    EVLOG_info << "EvIso15118D20: enable_sae_j2847_v2g_v2h: deferred to M1+";
}

void ISO15118_evImpl::handle_update_soc(double& SoC) {
    auto h = session.handle();
    (*h).dc_params.present_soc = SoC;
    if ((*h).current) {
        (*h).current->update_present_soc(SoC);
    }
}

} // namespace ev
} // namespace module
