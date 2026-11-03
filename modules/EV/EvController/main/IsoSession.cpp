// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "IsoSession.hpp"

#include <everest/exceptions.hpp>
#include <everest/iso15118_ev_session/session_callbacks.hpp>
#include <everest/logging.hpp>

#include <algorithm>
#include <exception>
#include <iterator>
#include <utility>

namespace module {

namespace {

namespace ev_session = everest::lib::iso15118_ev;
using CM = everest::lib::API::V1_0::types::ev_controller::ChargeMode;
using everest::lib::API::V1_0::types::ev_controller::PaymentOption;

constexpr const char* LOG_PREFIX = "EvController";

std::vector<iso15118::ProtocolId> parse_protocols(const std::string& list) {
    std::string error;
    auto protocols = ev_session::parse_supported_protocols(list, error);
    if (not protocols) {
        throw Everest::EverestConfigError("EvController: " + error);
    }
    return std::move(*protocols);
}

iso15118::ev::d20::der::SaeInverterProfile load_profile(const std::string& path) {
    std::string error;
    auto profile = ev_session::load_sae_profile(path, error);
    if (not profile) {
        throw Everest::EverestConfigError("EvController: sae_inverter_profile_path '" + path +
                                          "' is invalid: " + error);
    }
    return std::move(*profile);
}

const char* mode_name(CM mode) {
    switch (mode) {
    case CM::AcIec:
        return "AcIec";
    case CM::AcIso2:
        return "AcIso2";
    case CM::AcIsoD20:
        return "AcIsoD20";
    case CM::AcIsoD20DerIec:
        return "AcIsoD20DerIec";
    case CM::AcIsoD20DerSae:
        return "AcIsoD20DerSae";
    case CM::DcIso2:
        return "DcIso2";
    case CM::DcIsoD20:
        return "DcIsoD20";
    }
    return "unknown";
}

bool is_d20(CM mode) {
    return mode == CM::AcIsoD20 or mode == CM::AcIsoD20DerIec or mode == CM::AcIsoD20DerSae or mode == CM::DcIsoD20;
}

Event tagged(EventPayload payload, std::uint32_t session) {
    Event ev{std::move(payload)};
    ev.iso_session = session;
    return ev;
}

void log_problems(const std::vector<std::string>& problems) {
    for (const auto& problem : problems) {
        EVLOG_error << "EvController: invalid session parameter: " << problem;
    }
}

} // namespace

IsoSession::IsoSession(Settings settings_, Enqueue enqueue_) :
    settings(std::move(settings_)),
    protocols(parse_protocols(settings.supported_protocols)),
    sae_profile(load_profile(settings.sae_inverter_profile_path)),
    enqueue(std::move(enqueue_)) {
    ev_session::install_logging();
    ev_session::warn_tls_deviations(settings.vehicle, protocols, LOG_PREFIX);
}

void IsoSession::check(const Settings& settings) {
    parse_protocols(settings.supported_protocols);
    load_profile(settings.sae_inverter_profile_path);
}

IsoSession::~IsoSession() {
    if (live) {
        live->controller->shutdown();
        live->worker.join();
    }
}

std::vector<iso15118::ProtocolId> IsoSession::protocols_for(CM mode) const {
    using iso15118::ProtocolId;
    // -20 modes offer the configured list as it is, so a SECC without -20 can fall back to an
    // earlier protocol. -2 modes leave -20 out, and AC leaves DIN SPEC 70121 out, which is DC only.
    const bool drop_20 = not is_d20(mode);
    const bool drop_din = mode != CM::DcIso2 and mode != CM::DcIsoD20;
    std::vector<ProtocolId> offered;
    std::copy_if(protocols.begin(), protocols.end(), std::back_inserter(offered), [&](ProtocolId protocol) {
        return not(drop_20 and protocol == ProtocolId::ISO15118_20) and
               not(drop_din and protocol == ProtocolId::DIN70121);
    });
    return offered;
}

std::optional<std::string> IsoSession::cannot_run(CM mode) const {
    if (mode == CM::AcIec) {
        return std::nullopt;
    }
    const auto offered = protocols_for(mode);
    const bool has_20 = std::find(offered.begin(), offered.end(), iso15118::ProtocolId::ISO15118_20) != offered.end();
    if ((is_d20(mode) and not has_20) or offered.empty()) {
        return std::string{"supported_protocols '"} + settings.supported_protocols + "' offers no protocol for " +
               mode_name(mode);
    }
    return std::nullopt;
}

iso15118::ev::feedback::Callbacks IsoSession::make_callbacks(Live& session) {
    const auto forward = [this, id = session.id](EventPayload payload) { enqueue(tagged(std::move(payload), id)); };
    ev_session::SessionOutputs outputs;
    outputs.ev_power_ready = [forward] { forward(IsoPowerReadyEvt{}); };
    outputs.dc_power_on = [forward] { forward(IsoDcPowerOnEvt{}); };
    outputs.stop_from_charger = [forward] { forward(IsoStopFromChargerEvt{}); };
    outputs.pause_from_charger = [forward] { forward(IsoPauseFromChargerEvt{}); };
    outputs.power_delivery_stopped = [forward] { forward(IsoPowerDeliveryStoppedEvt{}); };
    outputs.dc_evse_present_values = [forward](float voltage, float current) {
        forward(DcEvsePresentVoltagePayload{voltage});
        forward(DcEvsePresentCurrentPayload{current});
    };
    outputs.welding_detection_failed = [forward](const ::types::iso15118::WeldingDetectionFailure& failure) {
        forward(IsoWeldingDetectionFailedEvt{failure});
    };
    outputs.ac_evse_max_current = [forward](float current) { forward(IsoAcMaxCurrentEvt{current}); };
    outputs.ac_evse_target_power = [forward](const ::types::iso15118::AcTargetPower& target) {
        forward(IsoAcTargetPowerEvt{target});
    };
    outputs.der_negotiated_functions = [forward](const ::types::iso15118::DerNegotiatedFunctions& functions) {
        forward(IsoDerNegotiatedEvt{functions});
    };
    outputs.der_control_received = [forward](const ::types::iso15118::DerControlReceived& received) {
        forward(IsoDerControlEvt{received});
    };
    // The worker writes it; release() reads it after the join.
    outputs.contract_installed = [&session](iso15118::ev::d2::ContractMaterial installed) {
        session.installed_contract = std::move(installed);
    };
    return ev_session::make_session_callbacks(std::move(outputs), settings.vehicle.pnc_contract_store_dir, LOG_PREFIX);
}

bool IsoSession::start_charging(const IsoStart& start) {
    const auto etm = transfer_mode(start);
    if (not etm or not ev_session::energy_service(*etm)) {
        EVLOG_error << "EvController: " << mode_name(start.mode) << " runs no ISO 15118 session; rejecting the start";
        return false;
    }
    if (const auto reason = cannot_run(start.mode)) {
        EVLOG_error << "EvController: " << *reason << "; rejecting the start";
        return false;
    }
    if (start.departure_time_s < 0 or start.e_amount_wh < 0) {
        EVLOG_error << "EvController: departure time and energy amount must not be negative; rejecting the start";
        return false;
    }
    if (live) {
        if (not live->loop_returned) {
            EVLOG_warning << "EvController: an ISO 15118 session is still running; rejecting the start";
            return false;
        }
        // Its finish is queued and is dropped as stale when the loop reaches it.
        release();
    }

    // Per session: an interface that comes up later is found, and the EVCCID follows it.
    ev_session::Vehicle vehicle;
    vehicle.device = settings.device;
    if (not iso15118::ev::resolve_interface(vehicle.device)) {
        EVLOG_error << "EvController: HLC interface '" << settings.device << "' not found; rejecting the start";
        return false;
    }
    vehicle.evcc_mac = ev_session::read_interface_mac(vehicle.device);
    vehicle.protocols = protocols_for(start.mode);
    vehicle.sae_profile = sae_profile;

    auto config = settings.vehicle;
    config.enable_pnc = start.payment == PaymentOption::Contract;
    config.iso2_ac_e_amount_wh = start.e_amount_wh;

    // Read per session, so a contract an earlier session stored is presented and a rotated key
    // password is used.
    ev_session::SessionFiles files;
    if (config.enable_pnc) {
        files = ev_session::make_session_files(config, ev_session::load_pnc_material(config, LOG_PREFIX),
                                               paused ? installed_contract : std::nullopt, LOG_PREFIX);
    } else {
        files.key_password = ev_session::vehicle_key_password(config);
    }

    auto request =
        ev_session::make_session_request(*etm, vehicle.protocols, start.payment == PaymentOption::ExternalPayment,
                                         start.payment == PaymentOption::Contract and start.force_payment_option,
                                         start.departure_time_s, start.e_amount_wh, LOG_PREFIX);
    request->resume = paused;
    request->has_cp_state_feedback = cp_c_or_d.has_value();

    // Seeded into copies and committed only once the session is built: a rejected start leaves no
    // discharge limits or AC parameters behind for the next one.
    auto ac = ac_params;
    auto dc = dc_params;
    std::optional<float> cmd_discharge_power;
    std::optional<float> cmd_discharge_current;
    if (start.bpt) {
        cmd_discharge_power = start.bpt->discharge_max_power_limit;
        cmd_discharge_current = start.bpt->discharge_max_current_limit;
    }
    ev_session::seed_session_params(config, *etm, request->energy_service, cmd_discharge_power, cmd_discharge_current,
                                    ac, dc, LOG_PREFIX);
    auto ev_config = ev_session::make_ev_config(config, vehicle, *request, files, pnc_without_tls_warned, LOG_PREFIX);
    const auto problems = ev_session::validate_session(ev_config, ac, dc);
    if (not problems.empty()) {
        log_problems(problems);
        EVLOG_error << "EvController: rejecting the start; the session parameters are invalid";
        return false;
    }

    auto next = std::make_unique<Live>();
    next->id = ++last_id;
    try {
        next->controller =
            std::make_unique<iso15118::ev::Controller>(std::move(ev_config), make_callbacks(*next), dc, ac);
    } catch (const std::exception& e) {
        EVLOG_error << "EvController: cannot start the ISO 15118 session: " << e.what();
        return false;
    }
    ac_params = ac;
    dc_params = dc;
    if (cp_c_or_d) {
        next->controller->set_cp_state(*cp_c_or_d);
    }
    next->worker = std::thread([this, session = next.get()] {
        try {
            session->controller->loop();
        } catch (const std::exception& e) {
            EVLOG_error << "EvController: ISO 15118 session failed: " << e.what();
            session->failed = true;
        } catch (...) {
            EVLOG_error << "EvController: ISO 15118 session failed (non-std exception)";
            session->failed = true;
        }
        session->loop_returned = true;
        enqueue(tagged(IsoV2GFinishedEvt{}, session->id));
    });
    live = std::move(next);
    return true;
}

void IsoSession::release() {
    // Not live from here on, whatever the rest throws.
    const auto ended = std::move(live);
    ended->worker.join();
    // A failed session leaves nothing to resume.
    paused = (ended->discard_pause or ended->failed) ? std::nullopt : ended->controller->paused_session();
    if (ended->installed_contract) {
        installed_contract = std::move(ended->installed_contract);
    }
    // Kept along a chain of pauses, so each resume presents the contract the chain installed.
    if (not paused) {
        installed_contract.reset();
    }
}

bool IsoSession::admit(std::uint32_t session, bool ended) {
    if (not live or live->id != session) {
        return false;
    }
    if (ended) {
        release();
    }
    return true;
}

void IsoSession::end(void (iso15118::ev::Controller::*action)()) {
    // A stop or abort also discards a pause: the next start would otherwise resume a session
    // the driver ended.
    paused.reset();
    installed_contract.reset();
    if (live) {
        (live->controller.get()->*action)();
        live->discard_pause = true;
    }
}

void IsoSession::stop_charging() {
    end(&iso15118::ev::Controller::request_stop);
}

void IsoSession::abort_charging() {
    end(&iso15118::ev::Controller::terminate);
}

void IsoSession::pause_charging() {
    if (live) {
        live->controller->request_pause();
    }
}

void IsoSession::cp_state_changed(::types::iso15118::CpState cp_state) {
    using ::types::iso15118::CpState;
    const bool c_or_d = (cp_state == CpState::C) or (cp_state == CpState::D);
    cp_c_or_d = c_or_d;
    if (live) {
        live->controller->set_cp_state(c_or_d);
    }
    // [V2G2-737]: E or F ends a live session without SessionStop ([V2G2-728]).
    if (live and (cp_state == CpState::E or cp_state == CpState::F)) {
        end(&iso15118::ev::Controller::terminate);
    }
}

void IsoSession::set_dc_params(const ::types::iso15118::DcEvParameters& params) {
    std::vector<std::string> problems;
    const auto merged = ev_session::merge_dc_params(dc_params, params, problems);
    if (not merged) {
        log_problems(problems);
        EVLOG_error << "EvController: keeping the previous DC parameters";
        return;
    }
    dc_params = *merged;
    if (live) {
        live->controller->update_dc_params(dc_params);
    }
}

void IsoSession::update_soc(float soc_pct) {
    dc_params.present_soc = soc_pct;
    if (live) {
        live->controller->update_present_soc(soc_pct);
    }
}

void IsoSession::update_present_values(const ::types::iso15118::EvPresentValues& values) {
    ev_session::apply_present_values(values, ac_params, dc_params, live ? live->controller.get() : nullptr, LOG_PREFIX);
}

} // namespace module
