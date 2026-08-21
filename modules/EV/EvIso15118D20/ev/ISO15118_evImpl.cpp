// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include <chrono>
#include <exception>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <iso15118/ev/config_validation.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/sdp.hpp>

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

    check_config();
}

void ISO15118_evImpl::check_config() {
    namespace dt = iso15118::message_20::datatypes;

    // energy_service only picks the SAP namespace; DC is fine for validating transport fields.
    auto problems = iso15118::ev::validate_config(make_ev_config(dt::ServiceCategory::DC));

    const auto append = [&problems](std::vector<std::string> more) {
        problems.insert(problems.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
    };

    iso15118::ev::AcChargeParams ac_params;
    ac_params.max_charge_power = static_cast<float>(mod->config.ac_max_charge_power_w);
    ac_params.min_charge_power = static_cast<float>(mod->config.ac_min_charge_power_w);
    ac_params.max_discharge_power = static_cast<float>(mod->config.ac_max_discharge_power_w);
    ac_params.min_discharge_power = static_cast<float>(mod->config.ac_min_discharge_power_w);
    append(iso15118::ev::validate_ac_charge_params(ac_params));

    iso15118::ev::DcChargeParams dc_params;
    dc_params.max_discharge_power = static_cast<float>(mod->config.dc_max_discharge_power_w);
    dc_params.min_discharge_power = static_cast<float>(mod->config.dc_min_discharge_power_w);
    dc_params.max_discharge_current = static_cast<float>(mod->config.dc_max_discharge_current_a);
    append(iso15118::ev::validate_dc_charge_params(dc_params));

    config_valid = problems.empty();
    for (const auto& problem : problems) {
        EVLOG_error << "EvIso15118D20: invalid config: " << problem;
    }
    if (not config_valid) {
        EVLOG_error << "EvIso15118D20: start_charging is refused until the module config is corrected";
    }
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

    // ev::Controller throws on an unresolvable interface (incl. "auto"); caught in run_one_session().
    ev_config.interface_name = mod->config.device;
    ev_config.evcc_id = mod->config.evcc_id;
    ev_config.response_timeout = std::chrono::milliseconds(mod->config.response_timeout_ms);
    ev_config.advertised_security = iso15118::io::v2gtp::Security::NO_TRANSPORT_SECURITY;

    namespace dt = iso15118::message_20::datatypes;
    ev_config.energy_service = energy_service;
    const bool ac_family = energy_service == dt::ServiceCategory::AC || energy_service == dt::ServiceCategory::AC_BPT ||
                           energy_service == dt::ServiceCategory::AC_DER_IEC;
    const bool dc_family = energy_service == dt::ServiceCategory::DC || energy_service == dt::ServiceCategory::DC_BPT;
    if (ac_family) {
        ev_config.advertised_app_protocols = {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}};
    } else if (dc_family) {
        ev_config.advertised_app_protocols = {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};
    }

    if (energy_service == iso15118::message_20::datatypes::ServiceCategory::AC_DER_IEC) {
        auto& functions = ev_config.der_control_functions;
        functions.over_frequency_watt_mode = mod->config.der_over_frequency_watt_mode;
        functions.under_frequency_watt_mode = mod->config.der_under_frequency_watt_mode;
        functions.volt_watt_mode = mod->config.der_volt_watt_mode;
        functions.volt_var_mode = mod->config.der_volt_var_mode;
        functions.watt_var_mode = mod->config.der_watt_var_mode;
        functions.watt_cos_phi_mode = mod->config.der_watt_cos_phi_mode;
        functions.dso_q_setpoint_provision = mod->config.der_dso_q_setpoint_provision;
        functions.dso_cos_phi_setpoint_provision = mod->config.der_dso_cos_phi_setpoint_provision;
        functions.dc_injection_restriction = mod->config.der_dc_injection_restriction;
        functions.zero_current_mode = mod->config.der_zero_current_mode;
        functions.over_voltage_fault_ride_through_mode = mod->config.der_over_voltage_fault_ride_through_mode;
        functions.under_voltage_fault_ride_through_mode = mod->config.der_under_voltage_fault_ride_through_mode;
        ev_config.der_stop_on_unsupported_functions = mod->config.der_stop_on_unsupported_functions;
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

    callbacks.stopped = [] { EVLOG_info << "EvIso15118D20: session stopped"; };

    callbacks.ev_power_ready = [this] { publish_ev_power_ready(true); };

    callbacks.dc_power_on = [this] { publish_dc_power_on(nullptr); };

    callbacks.stop_from_charger = [this] { publish_stop_from_charger(nullptr); };

    callbacks.ac_limits = [](const iso15118::message_20::datatypes::AC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "EvIso15118D20: AC EVSE limits: max charge power "
                   << dt::from_RationalNumber(limits.max_charge_power) << " W, min charge power "
                   << dt::from_RationalNumber(limits.min_charge_power) << " W";
    };

    callbacks.ac_bpt_limits = [](const iso15118::message_20::datatypes::BPT_AC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "EvIso15118D20: AC BPT EVSE limits: max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, min discharge power "
                   << dt::from_RationalNumber(limits.min_discharge_power) << " W";
    };

    callbacks.dc_bpt_limits = [](const iso15118::message_20::datatypes::BPT_DC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "EvIso15118D20: DC BPT EVSE limits: max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, min discharge power "
                   << dt::from_RationalNumber(limits.min_discharge_power) << " W";
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

    // ISO15118_ev has no DER variable; log the directive rather than publish it
    callbacks.der_control = [](const iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode& control) {
        namespace dt = iso15118::message_20::datatypes;
        std::ostringstream line;
        line << "EvIso15118D20: DER directive: target active power "
             << dt::from_RationalNumber(control.target_active_power) << " W";
        if (control.dso_q_setpoint) {
            line << ", DSO Q setpoint " << dt::from_RationalNumber(control.dso_q_setpoint->dso_q_setpoint_value)
                 << " var";
        }
        if (control.dso_cos_phi_setpoint) {
            line << ", DSO cos phi setpoint "
                 << dt::from_RationalNumber(control.dso_cos_phi_setpoint->dso_cos_phi_setpoint_value);
        }
        EVLOG_info << line.str();
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
        // Published after phase resets to idle, so a consumer starting a new session
        // in response isn't rejected by the phase guard.
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
        // Declared after controller so it clears the off-thread pointer before ~Controller runs.
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
    switch (EnergyTransferMode) {
    case types::iso15118::EnergyTransferMode::DC:
    case types::iso15118::EnergyTransferMode::DC_core:
    case types::iso15118::EnergyTransferMode::DC_extended:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
        break;
    // The advertised AC power limits are three-phase totals either way, so the
    // single-phase and three-phase modes select the same service.
    case types::iso15118::EnergyTransferMode::AC_single_phase_core:
    case types::iso15118::EnergyTransferMode::AC_three_phase_core:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC;
        break;
    case types::iso15118::EnergyTransferMode::AC_BPT:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC_BPT;
        break;
    case types::iso15118::EnergyTransferMode::DC_BPT:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::DC_BPT;
        break;
    case types::iso15118::EnergyTransferMode::AC_DER_IEC:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC_DER_IEC;
        break;
    // Listed rather than folded into a default arm so -Wswitch flags a new mode.
    case types::iso15118::EnergyTransferMode::AC_two_phase:
    case types::iso15118::EnergyTransferMode::AC_BPT_DER:
    case types::iso15118::EnergyTransferMode::AC_DER_SAE:
    case types::iso15118::EnergyTransferMode::DC_combo_core:
    case types::iso15118::EnergyTransferMode::DC_unique:
    case types::iso15118::EnergyTransferMode::DC_ACDP:
    case types::iso15118::EnergyTransferMode::DC_ACDP_BPT:
    case types::iso15118::EnergyTransferMode::WPT:
    case types::iso15118::EnergyTransferMode::MCS:
    case types::iso15118::EnergyTransferMode::MCS_BPT:
        EVLOG_warning << "EvIso15118D20: rejecting start_charging with unsupported EnergyTransferMode '"
                      << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode)
                      << "'; only DC, DC BPT, AC single/three-phase, AC BPT and AC DER IEC are supported";
        return false;
    }
    if (not config_valid) {
        EVLOG_error << "EvIso15118D20: rejecting start_charging; the module config is invalid";
        return false;
    }
    {
        auto h = session.handle();
        if ((*h).phase != SessionPhase::idle) {
            EVLOG_warning << "EvIso15118D20: a session is already active; ignoring start_charging";
            return false;
        }
        (*h).energy_service = energy_service;
        namespace dt = iso15118::message_20::datatypes;
        const bool is_ac = energy_service == dt::ServiceCategory::AC || energy_service == dt::ServiceCategory::AC_BPT ||
                           energy_service == dt::ServiceCategory::AC_DER_IEC;
        if (is_ac) {
            (*h).ac_params.max_charge_power = static_cast<float>(mod->config.ac_max_charge_power_w);
            (*h).ac_params.min_charge_power = static_cast<float>(mod->config.ac_min_charge_power_w);
        }
        // AC_DER_IEC carries mandatory discharge limits on the wire just as AC_BPT does.
        if (energy_service == dt::ServiceCategory::AC_BPT or energy_service == dt::ServiceCategory::AC_DER_IEC) {
            (*h).ac_params.max_discharge_power = static_cast<float>(mod->config.ac_max_discharge_power_w);
            (*h).ac_params.min_discharge_power = static_cast<float>(mod->config.ac_min_discharge_power_w);
        }
        if (energy_service == dt::ServiceCategory::DC_BPT and not(*h).bpt_dc_params_set) {
            // set_bpt_dc_params discharge limits win; config knobs are the fallback
            (*h).dc_params.max_discharge_power = static_cast<float>(mod->config.dc_max_discharge_power_w);
            (*h).dc_params.min_discharge_power = static_cast<float>(mod->config.dc_min_discharge_power_w);
            (*h).dc_params.max_discharge_current = static_cast<float>(mod->config.dc_max_discharge_current_a);
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
    note_missing("min_voltage_limit", EvParameters.min_voltage_limit.has_value());
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
    params.min_voltage = EvParameters.min_voltage_limit.value_or(0.0f);
    params.energy_capacity = EvParameters.energy_capacity.value_or(0.0f);
    params.target_voltage = EvParameters.target_voltage.value_or(0.0f);
    params.target_current = EvParameters.target_current.value_or(0.0f);
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    EVLOG_info << "EvIso15118D20: set_bpt_dc_params";

    std::string missing;
    const auto note_missing = [&missing](const char* name, bool present) {
        if (not present) {
            missing.append(missing.empty() ? "" : ", ").append(name);
        }
    };
    note_missing("discharge_max_power_limit", EvBPTParameters.discharge_max_power_limit.has_value());
    note_missing("discharge_max_current_limit", EvBPTParameters.discharge_max_current_limit.has_value());
    if (not missing.empty()) {
        EVLOG_warning << "EvIso15118D20: set_bpt_dc_params missing " << missing
                      << "; keeping configured discharge knobs";
    }

    // discharge_target_current / discharge_minimal_soc are not consumed by the -20
    // Dynamic BPT request (reverse power flow is driven by SECC targets); log only
    if (EvBPTParameters.discharge_target_current) {
        EVLOG_debug << "EvIso15118D20: ignoring discharge_target_current " << *EvBPTParameters.discharge_target_current
                    << " A (SECC-target-driven)";
    }
    if (EvBPTParameters.discharge_minimal_soc) {
        EVLOG_debug << "EvIso15118D20: ignoring discharge_minimal_soc " << *EvBPTParameters.discharge_minimal_soc
                    << " % (SECC-target-driven)";
    }

    // command values override the config knobs seeded at start_charging
    auto h = session.handle();
    (*h).bpt_dc_params_set = true;
    auto& params = (*h).dc_params;
    if (EvBPTParameters.discharge_max_power_limit) {
        params.max_discharge_power = static_cast<float>(*EvBPTParameters.discharge_max_power_limit);
    }
    if (EvBPTParameters.discharge_max_current_limit) {
        params.max_discharge_current = static_cast<float>(*EvBPTParameters.discharge_max_current_limit);
    }
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

void ISO15118_evImpl::handle_update_present_values(types::iso15118::EvPresentValues& PresentValues) {
    // Stored on session params (seeds the next session) as well as pushed to the live
    // Controller. Absent fields keep their last value rather than reset to 0, which
    // would read as a real measurement.
    auto h = session.handle();
    if (PresentValues.present_voltage.has_value()) {
        const auto voltage = PresentValues.present_voltage.value();
        (*h).dc_params.present_voltage = voltage;
        if ((*h).current) {
            (*h).current->update_present_voltage(voltage);
        }
    }
    if (PresentValues.present_active_power.has_value()) {
        const auto power = PresentValues.present_active_power.value();
        (*h).ac_params.present_active_power = power;
        if ((*h).current) {
            (*h).current->update_present_active_power(power);
        }
    }
}

} // namespace ev
} // namespace module
