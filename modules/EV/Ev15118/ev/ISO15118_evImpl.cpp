// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <everest/conversions/iso15118/v2g_message_id.hpp>
#include <everest/exceptions.hpp>

#include <iso15118/ev/config/validation.hpp>

#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>

#include <everest/iso15118_ev_session/session_callbacks.hpp>
#include <everest/iso15118_ev_session/session_setup.hpp>

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

namespace ev_session = everest::lib::iso15118_ev;

constexpr const char* LOG_PREFIX = "Ev15118";

ev_session::VehicleConfig vehicle_config_of(const module::Conf& config, const std::filesystem::path& etc) {
    ev_session::VehicleConfig vehicle;
    vehicle.evcc_id = config.evcc_id;
    vehicle.response_timeout_ms = config.response_timeout_ms;
    vehicle.authorization_timeout_ms = config.authorization_timeout_ms;
    vehicle.control_mode = config.control_mode;
    vehicle.tls = config.tls;
    vehicle.allow_pnc_without_tls = config.allow_pnc_without_tls;
    vehicle.tls_version = config.tls_version;
    vehicle.verify_server_certificate = config.verify_server_certificate;
    vehicle.enable_tls_key_logging = config.enable_tls_key_logging;
    vehicle.tls_key_logging_path = config.tls_key_logging_path;
    vehicle.pki_path = config.pki_path;
    vehicle.v2g_root_cert_path = config.v2g_root_cert_path;
    vehicle.device_cert_chain_path = config.device_cert_chain_path;
    vehicle.device_key_path = config.device_key_path;
    vehicle.device_key_password_path = config.device_key_password_path;
    vehicle.iso2_ac_e_amount_wh = config.iso2_ac_e_amount_wh;
    vehicle.iso2_ac_ev_max_voltage_v = config.iso2_ac_ev_max_voltage_v;
    vehicle.iso2_ac_ev_max_current_a = config.iso2_ac_ev_max_current_a;
    vehicle.iso2_ac_ev_min_current_a = config.iso2_ac_ev_min_current_a;
    vehicle.enable_pnc = config.enable_pnc;
    vehicle.pnc_contract_cert_chain_path = config.pnc_contract_cert_chain_path;
    vehicle.pnc_contract_key_path = config.pnc_contract_key_path;
    vehicle.pnc_contract_key_password_path = config.pnc_contract_key_password_path;
    vehicle.pnc_oem_prov_cert_path = config.pnc_oem_prov_cert_path;
    vehicle.pnc_oem_prov_key_path = config.pnc_oem_prov_key_path;
    vehicle.pnc_oem_prov_key_password_path = config.pnc_oem_prov_key_password_path;
    vehicle.pnc_force_cert_install = config.pnc_force_cert_install;
    vehicle.pnc_contract_store_dir = config.pnc_contract_store_dir;
    vehicle.ac_phase_count = config.ac_phase_count;
    vehicle.ac_max_charge_power_w = config.ac_max_charge_power_w;
    vehicle.ac_min_charge_power_w = config.ac_min_charge_power_w;
    auto& functions = vehicle.der_functions;
    functions.over_frequency_watt_mode = config.der_over_frequency_watt_mode;
    functions.under_frequency_watt_mode = config.der_under_frequency_watt_mode;
    functions.volt_watt_mode = config.der_volt_watt_mode;
    functions.volt_var_mode = config.der_volt_var_mode;
    functions.watt_var_mode = config.der_watt_var_mode;
    functions.watt_cos_phi_mode = config.der_watt_cos_phi_mode;
    functions.dso_q_setpoint_provision = config.der_dso_q_setpoint_provision;
    functions.dso_cos_phi_setpoint_provision = config.der_dso_cos_phi_setpoint_provision;
    functions.dc_injection_restriction = config.der_dc_injection_restriction;
    functions.zero_current_mode = config.der_zero_current_mode;
    functions.over_voltage_fault_ride_through_mode = config.der_over_voltage_fault_ride_through_mode;
    functions.under_voltage_fault_ride_through_mode = config.der_under_voltage_fault_ride_through_mode;
    vehicle.der_stop_on_unsupported_functions = config.der_stop_on_unsupported_functions;
    vehicle.ac_max_discharge_power_w = config.ac_max_discharge_power_w;
    vehicle.ac_min_discharge_power_w = config.ac_min_discharge_power_w;
    vehicle.dc_max_discharge_power_w = config.dc_max_discharge_power_w;
    vehicle.dc_min_discharge_power_w = config.dc_min_discharge_power_w;
    vehicle.dc_max_discharge_current_a = config.dc_max_discharge_current_a;
    // The manager validates config values against the manifest bounds, [1, 65535] here.
    vehicle.cpd_rounds = config.cpd_rounds;
    vehicle.der_stop_on_invalid_control = config.der_stop_on_invalid_control;
    vehicle.etc = etc;
    return vehicle;
}

} // namespace

namespace module {
namespace ev {

ev_session::VehicleConfig ISO15118_evImpl::vehicle_config() const {
    return vehicle_config_of(mod->config, mod->info.paths.etc);
}

void ISO15118_evImpl::init() {
    ev_session::install_logging();

    // Read once, before any command can reach start_charging.
    std::string error;
    auto profile = ev_session::load_sae_profile(mod->config.sae_inverter_profile_path, error);
    if (not profile) {
        EVLOG_AND_THROW(Everest::EverestConfigError("Ev15118: sae_inverter_profile_path '" +
                                                    mod->config.sae_inverter_profile_path + "' is invalid: " + error));
    }
    vehicle.sae_profile = std::move(*profile);
}

void ISO15118_evImpl::ready() {
    std::string error;
    auto protocols = ev_session::parse_supported_protocols(mod->config.supported_protocols, error);
    if (not protocols) {
        EVLOG_AND_THROW(Everest::EverestConfigError("Ev15118: " + error));
    }
    vehicle.protocols = std::move(*protocols);

    // Resolve the HLC interface once ("auto" -> first usable ipv6 interface) so the connection and
    // the EVCCID MAC use the same device.
    vehicle.device = mod->config.device;
    if (not iso15118::ev::resolve_interface(vehicle.device)) {
        if (mod->config.device == "auto") {
            // "auto" has no usable as-is value; the Controller would only fail to resolve it again.
            EVLOG_AND_THROW(Everest::EverestConfigError(
                "Ev15118: device 'auto' found no usable ipv6 interface; set device to the HLC interface"));
        }
        EVLOG_warning << "Ev15118: could not resolve HLC interface '" << mod->config.device << "'; using it as-is";
        vehicle.device = mod->config.device;
    }

    // Only override the library EVCCID default when a real MAC was read.
    vehicle.evcc_mac = ev_session::read_interface_mac(vehicle.device);
    if (vehicle.evcc_mac) {
        EVLOG_info << "Ev15118: using the EVCC MAC of interface " << vehicle.device;
    } else {
        EVLOG_warning << "Ev15118: could not read the MAC of interface '" << vehicle.device
                      << "', keeping the library default EVCCID for ISO 15118-2 / DIN SPEC 70121";
    }

    ev_session::warn_tls_deviations(vehicle_config(), vehicle.protocols, LOG_PREFIX);

    // OEM and contract material are read once before starting the session worker.
    if (mod->config.enable_pnc) {
        vehicle.pnc_material = ev_session::load_pnc_material(vehicle_config(), LOG_PREFIX);
    }

    worker = std::thread([this] { session_worker(); });
    worker_started.store(true, std::memory_order_release);
}

// Idempotent, so the destructor still tears down cleanly if the framework never calls this.
void ISO15118_evImpl::shutdown() {
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

ISO15118_evImpl::~ISO15118_evImpl() {
    shutdown();
}

iso15118::ev::config::EvConfig ISO15118_evImpl::make_ev_config(const SessionState& state,
                                                               const ev_session::SessionFiles& files) const {
    ev_session::SessionRequest request;
    request.energy_service = state.energy_service;
    request.iso2_transfer_mode = state.iso2_transfer_mode;
    request.eim_requested = state.eim_requested;
    request.enforce_contract = state.enforce_contract;
    request.energy_request = state.energy_request;
    request.resume = state.paused;
    request.has_cp_state_feedback = state.cp_c_or_d.has_value();
    return ev_session::make_ev_config(vehicle_config(), vehicle, request, files, pnc_without_tls_warned, LOG_PREFIX);
}

iso15118::ev::feedback::Callbacks ISO15118_evImpl::make_callbacks() {
    ev_session::SessionOutputs outputs;
    outputs.v2g_message = [this](const iso15118::V2gMessageType& type) {
        publish_v2g_messages(types::iso15118::V2gMessages{.id = conversions::to_v2g_message_id(type)});
    };
    outputs.dc_evse_present_values = [this](float voltage, float current) {
        publish_dc_evse_present_voltage(voltage);
        publish_dc_evse_present_current(current);
    };
    outputs.ev_power_ready = [this] { publish_ev_power_ready(true); };
    outputs.dc_power_on = [this] { publish_dc_power_on(nullptr); };
    outputs.stop_from_charger = [this] { publish_stop_from_charger(nullptr); };
    outputs.pause_from_charger = [this] { publish_pause_from_charger(nullptr); };
    outputs.power_delivery_stopped = [this] { publish_power_delivery_stopped(nullptr); };
    outputs.welding_detection_failed = [this](const types::iso15118::WeldingDetectionFailure& failure) {
        publish_welding_detection_failed(failure);
    };
    outputs.ac_evse_max_current = [this](float current) { publish_ac_evse_max_current(current); };
    outputs.ac_evse_target_power = [this](const types::iso15118::AcTargetPower& target) {
        publish_ac_evse_target_power(target);
    };
    outputs.der_negotiated_functions = [this](const types::iso15118::DerNegotiatedFunctions& functions) {
        publish_der_negotiated_functions(functions);
    };
    outputs.der_control_received = [this](const types::iso15118::DerControlReceived& received) {
        publish_der_control_received(received);
    };
    outputs.contract_installed = [this](iso15118::ev::d2::ContractMaterial installed) {
        auto h = session.handle();
        (*h).installed_contract = std::move(installed);
    };
    return ev_session::make_session_callbacks(std::move(outputs), mod->config.pnc_contract_store_dir, LOG_PREFIX);
}

// This is a thread body: an exception that leaves it calls std::terminate and takes the whole
// manager down, not just the session. Everything the loop does can throw, the publish included.
void ISO15118_evImpl::session_worker() {
    // A throw from the wait itself would otherwise spin this loop at full speed forever, so give
    // up after a few in a row: the module stops serving sessions, the manager keeps running.
    constexpr int MAX_CONSECUTIVE_FAILURES = 5;
    int consecutive_failures = 0;
    // A throw loses only this publication; leaving the iteration would also drop a pending start.
    const auto guarded = [](const char* variable, const auto& publish) {
        try {
            publish();
        } catch (const std::exception& e) {
            EVLOG_error << "Ev15118: publishing " << variable << " failed: " << e.what();
        } catch (...) {
            EVLOG_error << "Ev15118: publishing " << variable << " failed (non-std exception)";
        }
    };
    // Readiness for power ends with the session, before its finish is announced.
    const auto publish_finish = [this, &guarded] {
        guarded("ev_power_ready", [this] { publish_ev_power_ready(false); });
        guarded("v2g_session_finished", [this] { publish_v2g_session_finished(nullptr); });
    };
    while (true) {
        try {
            bool requested = false;
            std::uint32_t finishes = 0;
            {
                auto h = session.handle();
                h.wait([&] {
                    return (*h).phase == SessionPhase::requested || (*h).shutting_down || (*h).finishes_owed > 0;
                });
                if ((*h).shutting_down) {
                    return;
                }
                requested = ((*h).phase == SessionPhase::requested);
                finishes = std::exchange((*h).finishes_owed, 0);
            }
            // A session's own finish follows its phase returning to idle. A debt's finish can go out
            // while a newer start holds the phase at requested, and a start sent in response to it is
            // then rejected. Debts go out before the pending session runs: run_one_session() defers
            // to them.
            for (; finishes > 0; --finishes) {
                publish_finish();
            }
            if (requested and run_one_session()) {
                publish_finish();
            }
            consecutive_failures = 0;
            continue;
        } catch (const std::exception& e) {
            EVLOG_error << "Ev15118: session worker iteration failed: " << e.what();
        } catch (...) {
            EVLOG_error << "Ev15118: session worker iteration failed (non-std exception)";
        }
        recover_worker_state();
        if (++consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
            EVLOG_error << "Ev15118: session worker failed " << MAX_CONSECUTIVE_FAILURES
                        << " times in a row; stopping it. The module will not serve further sessions.";
            try {
                auto h = session.handle();
                (*h).worker_dead = true;
            } catch (...) {
                // start_charging still rejects: it publishes nothing and the phase stays put.
            }
            return;
        }
    }
}

// Leave the worker able to serve the next request: a throw part way through an iteration can
// leave the phase stuck on requested or running, which the phase guard would reject forever.
void ISO15118_evImpl::recover_worker_state() noexcept {
    try {
        auto h = session.handle();
        (*h).current = nullptr;
        (*h).phase = SessionPhase::idle;
        (*h).finishes_owed = 0;
        (*h).discard_pause = false;
        (*h).paused.reset();
    } catch (...) {
        // The monitor itself is unusable; the next wait() will fail the same way and log again.
    }
}

bool ISO15118_evImpl::run_one_session() {
    try {
        std::optional<iso15118::ev::Controller> controller;
        // Declared after controller so a throw still clears the off-thread pointer before
        // ~Controller runs.
        ScopeGuard clear_current{[this] {
            auto h = session.handle();
            (*h).current = nullptr;
        }};
        // A resume presents the contract its paused session installed instead of the stored one.
        std::optional<iso15118::ev::d2::ContractMaterial> resume_contract;
        {
            auto h = session.handle();
            if ((*h).paused) {
                resume_contract = (*h).installed_contract;
            }
        }
        const bool resume_installed = resume_contract.has_value();
        // Per session, so a contract a previous session installed is presented by this one, and a
        // rotated key password is used.
        const auto files =
            ev_session::make_session_files(vehicle_config(), vehicle.pnc_material, resume_contract, LOG_PREFIX);
        {
            auto h = session.handle();
            // A stop in the requested window reset the phase to idle and owes its own finish.
            if ((*h).phase != SessionPhase::requested) {
                return false;
            }
            if ((*h).shutting_down) {
                (*h).phase = SessionPhase::idle;
                return true;
            }
            // A start cancelled after the worker took it is answered before this newer one runs;
            // the worker takes the debt and comes back for the request.
            if ((*h).finishes_owed > 0) {
                return false;
            }
            // The pause changed while the store was read; come back for the request as it is now.
            if (((*h).paused and (*h).installed_contract) != resume_installed) {
                return false;
            }
            // Running before construction: a stop arriving after a construction failure released
            // the lock must not cancel a start the failure path finishes itself.
            (*h).phase = SessionPhase::running;
            if (not(*h).paused) {
                (*h).installed_contract.reset();
            }
            // Config, parameters and registration in one lock hold: a cp_state_changed or a
            // parameter update landing in between would otherwise miss this session.
            controller.emplace(make_ev_config(*h, files), make_callbacks(), (*h).dc_params, (*h).ac_params);
            (*h).current = &controller.value();
            // The CP report is latched, so a session starting after it still gets the state.
            if ((*h).cp_c_or_d) {
                controller->set_cp_state(*(*h).cp_c_or_d);
            }
        }
        controller->loop();
        auto h = session.handle();
        // One hold: a stop or abort reaches current until here, so its discard_pause is seen,
        // and none lands after. paused_session() is empty unless the session ended with
        // SessionStop(Pause); the next session resumes it.
        (*h).paused = (*h).discard_pause ? std::nullopt : controller->paused_session();
        (*h).discard_pause = false;
        (*h).current = nullptr;
        (*h).phase = SessionPhase::idle;
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Ev15118: session failed: " << e.what();
    } catch (...) {
        EVLOG_error << "Ev15118: session failed (non-std exception)";
    }
    // A failed session leaves nothing to resume.
    auto h = session.handle();
    (*h).paused.reset();
    (*h).discard_pause = false;
    (*h).phase = SessionPhase::idle;
    return true;
}

bool ISO15118_evImpl::handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                            types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                            double& DepartureTime, double& EAmount) {
    if (not worker_started.load(std::memory_order_acquire)) {
        EVLOG_error << "Ev15118: the session worker is not running yet; rejecting start_charging";
        return false;
    }

    EVLOG_info << "Ev15118: start_charging requested (DepartureTime " << DepartureTime << " s, EAmount " << EAmount
               << " kWh)";

    // EAmount goes on the wire as float Wh.
    constexpr double max_e_amount_kwh = std::numeric_limits<float>::max() / 1000.0;
    if (not std::isfinite(DepartureTime) or DepartureTime < 0 or not std::isfinite(EAmount) or EAmount < 0 or
        EAmount > max_e_amount_kwh) {
        EVLOG_error << "Ev15118: DepartureTime and EAmount must be finite and not negative; rejecting start_charging";
        return false;
    }

    {
        auto h = session.handle();
        if ((*h).worker_dead) {
            EVLOG_error << "Ev15118: the session worker stopped after repeated failures; rejecting start_charging";
            return false;
        }
    }

    // A device ready() kept as-is may still be missing, and the Controller refuses to construct without it.
    auto interface_name = vehicle.device;
    if (not iso15118::ev::resolve_interface(interface_name)) {
        EVLOG_error << "Ev15118: HLC interface '" << vehicle.device << "' not found; rejecting start_charging";
        return false;
    }

    // ISO 15118-2 Plug&Charge. enable_pnc prefers Contract whenever the SECC offers it; an
    // explicit ExternalPayment overrides that for this session, and enforcement selects Contract
    // regardless of the offer [V2G2-465].
    const bool contract_requested =
        SelectedPaymentOption.payment_option.has_value() and
        SelectedPaymentOption.payment_option.value() == types::iso15118::PaymentOption::Contract;
    const bool eim_requested =
        SelectedPaymentOption.payment_option.has_value() and
        SelectedPaymentOption.payment_option.value() == types::iso15118::PaymentOption::ExternalPayment;
    const bool enforce_contract = contract_requested and SelectedPaymentOption.enforce_payment_option.value_or(false);
    if (contract_requested and not mod->config.enable_pnc) {
        if (enforce_contract) {
            EVLOG_warning << "Ev15118: Contract (Plug&Charge) enforced without PnC configuration; selecting "
                             "Contract regardless of the SECC offer";
        } else {
            EVLOG_warning << "Ev15118: Contract (Plug&Charge) requested but enable_pnc is false; falling back to EIM";
        }
    }

    // EAmount is in kWh here and in Wh on the wire.
    auto request = ev_session::make_session_request(EnergyTransferMode, vehicle.protocols, eim_requested,
                                                    enforce_contract, DepartureTime, EAmount * 1000.0, LOG_PREFIX);
    if (not request) {
        EVLOG_warning << "Ev15118: rejecting start_charging with unsupported EnergyTransferMode '"
                      << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode)
                      << "'; only DC, DC BPT, MCS, MCS BPT, AC single/three-phase, AC BPT, "
                         "AC DER IEC and AC DER SAE are supported";
        return false;
    }
    {
        auto h = session.handle();
        if ((*h).phase != SessionPhase::idle) {
            EVLOG_warning << "Ev15118: a session is already active; ignoring start_charging";
            return false;
        }
        // Built into a copy and committed only once it validates. Writing into the live state and
        // returning on a problem would leave the rejected request's energy service, transfer mode
        // and any discharge limits behind, and the next accepted start would inherit whichever of
        // them it does not itself overwrite: the discharge limits are written only for BPT and
        // the DER services, so they survive a change of energy service with nothing resetting them.
        auto candidate = *h;
        candidate.energy_service = request->energy_service;
        candidate.iso2_transfer_mode = request->iso2_transfer_mode;
        candidate.eim_requested = request->eim_requested;
        candidate.enforce_contract = request->enforce_contract;
        candidate.energy_request = request->energy_request;
        ev_session::seed_session_params(vehicle_config(), EnergyTransferMode, request->energy_service,
                                        candidate.cmd_max_discharge_power, candidate.cmd_max_discharge_current,
                                        candidate.ac_params, candidate.dc_params, LOG_PREFIX);
        // Validate what actually goes on the wire: the merged params, not the raw config.
        // The session's files are read when it runs; nothing validated here depends on them.
        const auto problems = ev_session::validate_session(make_ev_config(candidate, ev_session::SessionFiles{}),
                                                           candidate.ac_params, candidate.dc_params);
        if (not problems.empty()) {
            for (const auto& problem : problems) {
                EVLOG_error << "Ev15118: invalid session parameter: " << problem;
            }
            EVLOG_error << "Ev15118: rejecting start_charging; the session parameters are invalid";
            return false;
        }
        candidate.phase = SessionPhase::requested;
        *h = std::move(candidate);
    }
    session.notify_all();
    return true;
}

bool ISO15118_evImpl::end_session_locked(SessionState& state, ControllerAction action, bool drop_paused) {
    bool cancelled = false;
    if (state.current) {
        (state.current->*action)();
    } else if (state.phase == SessionPhase::requested) {
        // the request arrived before the worker constructed the controller; cancel the
        // pending session so run_one_session() skips it under the lock
        state.phase = SessionPhase::idle;
        // The worker still publishes v2g_session_finished for this start_charging.
        ++state.finishes_owed;
        cancelled = true;
    }
    // Independent of whether a controller is running: a stop or an abort discards the stored
    // pause, or the next start_charging silently resumes a session the driver ended.
    if (drop_paused) {
        state.paused.reset();
        // The running session's loop may already have ended with a pause the request can no
        // longer prevent; run_one_session() drops it when it takes the result.
        if (state.current) {
            state.discard_pause = true;
        }
    }
    return cancelled;
}

void ISO15118_evImpl::end_session(ControllerAction action, bool drop_paused) {
    bool cancelled = false;
    {
        auto h = session.handle();
        cancelled = end_session_locked(*h, action, drop_paused);
    }
    if (cancelled) {
        session.notify_all();
    }
}

void ISO15118_evImpl::handle_stop_charging() {
    end_session(&iso15118::ev::Controller::request_stop, true);
}

void ISO15118_evImpl::handle_pause_charging() {
    end_session(&iso15118::ev::Controller::request_pause, false);
}

void ISO15118_evImpl::handle_abort_charging() {
    end_session(&iso15118::ev::Controller::terminate, true);
}

void ISO15118_evImpl::handle_cp_state_changed(types::iso15118::CpState& cp_state) {
    using types::iso15118::CpState;
    const bool c_or_d = (cp_state == CpState::C) or (cp_state == CpState::D);
    bool cancelled = false;
    {
        auto h = session.handle();
        // Latched: the report may arrive before a controller exists, and reporting it at all is
        // what enables the CP-dependent checks (EvConfig::has_cp_state_feedback).
        (*h).cp_c_or_d = c_or_d;
        if ((*h).current) {
            (*h).current->set_cp_state(c_or_d);
        }
        // [V2G2-737]: E or F ends the session without SessionStop ([V2G2-728]), and cancels one that
        // was accepted but whose Controller is not built yet. Under the same hold as the phase read,
        // so a session started after it is not the one aborted.
        if ((*h).phase != SessionPhase::idle and (cp_state == CpState::E or cp_state == CpState::F)) {
            cancelled = end_session_locked(*h, &iso15118::ev::Controller::terminate, true);
        }
    }
    if (cancelled) {
        session.notify_all();
    }
}

void ISO15118_evImpl::handle_set_fault() {
    EVLOG_warning << "Ev15118: set_fault is not supported";
}

void ISO15118_evImpl::handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) {
    EVLOG_info << "Ev15118: set_dc_params";

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
        EVLOG_warning << "Ev15118: set_dc_params missing " << missing << "; keeping the previous values";
    }

    auto h = session.handle();
    // Merged into a copy and stored only once it validates: a rejected update changes nothing.
    std::vector<std::string> problems;
    const auto merged = ev_session::merge_dc_params((*h).dc_params, EvParameters, problems);
    if (not merged) {
        for (const auto& problem : problems) {
            EVLOG_error << "Ev15118: invalid set_dc_params: " << problem;
        }
        EVLOG_error << "Ev15118: rejecting set_dc_params; keeping the previous values";
        return;
    }
    const auto& params = *merged;
    (*h).dc_params = params;
    if ((*h).current) {
        // The targets steer the running Scheduled-mode charge loop.
        (*h).current->update_dc_params(params);
    }
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    EVLOG_info << "Ev15118: set_bpt_dc_params";

    std::string missing;
    const auto note_missing = [&missing](const char* name, bool present) {
        if (not present) {
            missing.append(missing.empty() ? "" : ", ").append(name);
        }
    };
    note_missing("discharge_max_power_limit", EvBPTParameters.discharge_max_power_limit.has_value());
    note_missing("discharge_max_current_limit", EvBPTParameters.discharge_max_current_limit.has_value());
    if (not missing.empty()) {
        EVLOG_warning << "Ev15118: set_bpt_dc_params missing " << missing << "; keeping the previous discharge limits";
    }

    // discharge_target_current / discharge_minimal_soc are not consumed by the -20
    // Dynamic BPT request (reverse power flow is driven by SECC targets); log only
    if (EvBPTParameters.discharge_target_current) {
        EVLOG_debug << "Ev15118: ignoring discharge_target_current " << *EvBPTParameters.discharge_target_current
                    << " A (SECC-target-driven)";
    }
    if (EvBPTParameters.discharge_minimal_soc) {
        EVLOG_debug << "Ev15118: ignoring discharge_minimal_soc " << *EvBPTParameters.discharge_minimal_soc
                    << " % (SECC-target-driven)";
    }

    // Command values override the config settings seeded at start_charging, for the active BPT
    // session and every later one. Merged into a copy and stored only once it validates, with or
    // without a session: a rejected update changes nothing.
    auto h = session.handle();
    auto cmd_power = (*h).cmd_max_discharge_power;
    auto cmd_current = (*h).cmd_max_discharge_current;
    if (EvBPTParameters.discharge_max_power_limit) {
        cmd_power = static_cast<float>(*EvBPTParameters.discharge_max_power_limit);
    }
    if (EvBPTParameters.discharge_max_current_limit) {
        cmd_current = static_cast<float>(*EvBPTParameters.discharge_max_current_limit);
    }
    namespace dt = iso15118::message_20::datatypes;
    const auto service = (*h).energy_service;
    const bool bpt_active = (*h).phase != SessionPhase::idle and
                            (service == dt::ServiceCategory::DC_BPT or service == dt::ServiceCategory::MCS_BPT);
    auto params = (*h).dc_params;
    std::vector<std::string> problems;
    if (bpt_active) {
        params.max_discharge_power = cmd_power.value_or(params.max_discharge_power);
        params.max_discharge_current = cmd_current.value_or(params.max_discharge_current);
        problems = iso15118::ev::config::validate_dc_charge_params(params);
    } else {
        // Only the limits the command carries: the charge limits may not be set yet, and
        // start_charging validates the whole set.
        const auto check = [&problems](const char* name, const std::optional<float>& value) {
            if (value and not(std::isfinite(*value) and *value >= 0.0f)) {
                problems.push_back(std::string(name) + " must be finite and not negative, got " +
                                   std::to_string(*value));
            }
        };
        const std::optional<float> power = EvBPTParameters.discharge_max_power_limit ? cmd_power : std::nullopt;
        const std::optional<float> current = EvBPTParameters.discharge_max_current_limit ? cmd_current : std::nullopt;
        check("discharge_max_power_limit", power);
        check("discharge_max_current_limit", current);
        const auto min_power = static_cast<float>(mod->config.dc_min_discharge_power_w);
        if (power and *power < min_power) {
            problems.push_back("discharge_max_power_limit " + std::to_string(*power) +
                               " is below dc_min_discharge_power_w " + std::to_string(min_power));
        }
    }
    if (not problems.empty()) {
        for (const auto& problem : problems) {
            EVLOG_error << "Ev15118: invalid set_bpt_dc_params: " << problem;
        }
        EVLOG_error << "Ev15118: rejecting set_bpt_dc_params; keeping the previous discharge limits";
        return;
    }
    if (bpt_active) {
        (*h).dc_params = params;
        // A requested session's Controller is built from dc_params, so it picks the update up.
        if ((*h).current) {
            (*h).current->update_dc_params(params);
        }
    }
    (*h).cmd_max_discharge_power = cmd_power;
    (*h).cmd_max_discharge_current = cmd_current;
}

void ISO15118_evImpl::handle_enable_sae_j2847_v2g_v2h() {
    EVLOG_info << "Ev15118: enable_sae_j2847_v2g_v2h is not supported";
}

void ISO15118_evImpl::handle_update_soc(double& SoC) {
    auto h = session.handle();
    (*h).dc_params.present_soc = SoC;
    (*h).dc_params.present_soc_known = true;
    if ((*h).current) {
        (*h).current->update_present_soc(SoC);
    }
}

void ISO15118_evImpl::handle_update_present_values(types::iso15118::EvPresentValues& PresentValues) {
    // Stored on session params (seeds the next session) as well as pushed to the live Controller.
    auto h = session.handle();
    ev_session::apply_present_values(PresentValues, (*h).ac_params, (*h).dc_params, (*h).current, LOG_PREFIX);
}

} // namespace ev
} // namespace module
