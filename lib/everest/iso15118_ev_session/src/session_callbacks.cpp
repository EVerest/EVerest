// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/iso15118_ev_session/session_callbacks.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include <everest/logging.hpp>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/session/protocol.hpp>

#include <everest/iso15118_ev_session/contract_store.hpp>
#include <everest/iso15118_ev_session/der_publish_names.hpp>

namespace everest::lib::iso15118_ev {

namespace {

std::string_view signal_to_string(iso15118::ev::feedback::Signal signal) {
    using Signal = iso15118::ev::feedback::Signal;
    switch (signal) {
    case Signal::DLINK_TERMINATE:
        return "DLINK_TERMINATE";
    case Signal::DLINK_PAUSE:
        return "DLINK_PAUSE";
    case Signal::DLINK_ERROR:
        return "DLINK_ERROR";
    }
    return "unknown";
}

// Calls `output` when it is set.
template <typename F, typename... Args> void emit(const F& output, Args&&... args) {
    if (output) {
        output(std::forward<Args>(args)...);
    }
}

} // namespace

iso15118::ev::feedback::Callbacks
make_session_callbacks(SessionOutputs outputs_, const std::string& pnc_contract_store_dir, const std::string& prefix) {
    namespace dt = iso15118::message_20::datatypes;
    const auto out = std::make_shared<const SessionOutputs>(std::move(outputs_));
    iso15118::ev::feedback::Callbacks callbacks;

    callbacks.connected = [prefix](const iso15118::io::Ipv6EndPoint&) {
        EVLOG_info << prefix << ": connected to SECC";
    };

    callbacks.v2g_message = [out](const iso15118::V2gMessageType& type) { emit(out->v2g_message, type); };

    callbacks.signal = [prefix](iso15118::ev::feedback::Signal signal) {
        EVLOG_debug << prefix << ": signal " << signal_to_string(signal);
    };

    callbacks.selected_protocol = [prefix](iso15118::ProtocolId protocol) {
        EVLOG_info << prefix << ": selected protocol " << iso15118::protocol_id_to_string(protocol);
    };

    callbacks.evse_id = [prefix](const std::string& evse_id) { EVLOG_info << prefix << ": EVSE id " << evse_id; };

    callbacks.dc_evse_present_limits = [prefix](const iso15118::ev::feedback::DcMaximumLimits& limits) {
        EVLOG_debug << prefix << ": DC EVSE present limits: " << limits.voltage << " V, " << limits.current << " A, "
                    << limits.power.value_or(NAN) << " W";
    };

    callbacks.dc_evse_present_values = [out](float voltage, float current) {
        emit(out->dc_evse_present_values, voltage, current);
    };

    callbacks.evse_session_info = [prefix](const iso15118::ev::d20::EVSESessionInfo&) {
        EVLOG_debug << prefix << ": EVSE session info received";
    };

    // The session continues with the in-memory contract whatever happens to the stored copy.
    callbacks.pnc_contract_installed = [out, prefix, store_dir = pnc_contract_store_dir](const std::string& chain_pem,
                                                                                         const std::string& key_pem,
                                                                                         const std::string& emaid) {
        std::string error;
        auto installed = decode_contract(chain_pem, key_pem, error);
        if (not installed) {
            EVLOG_error << prefix << ": Plug&Charge contract installed for eMAID " << emaid << " is unusable: " << error
                        << "; only this session presents it";
            return;
        }
        emit(out->contract_installed, std::move(*installed));
        if (store_dir.empty()) {
            EVLOG_info << prefix << ": Plug&Charge contract installed for eMAID " << emaid
                       << "; not stored, pnc_contract_store_dir is empty";
            return;
        }
        std::string warning;
        if (store_contract(store_dir, chain_pem, key_pem, emaid, error, warning)) {
            EVLOG_info << prefix << ": Plug&Charge contract installed for eMAID " << emaid << ", stored in "
                       << store_dir;
            if (not warning.empty()) {
                EVLOG_warning << prefix << ": " << store_dir << ": " << warning;
            }
        } else {
            EVLOG_error << prefix << ": Plug&Charge contract installed for eMAID " << emaid
                        << " could not be stored in " << store_dir << ": " << error
                        << "; only this session and its resume present it";
        }
    };

    callbacks.pause_from_charger = [out] { emit(out->pause_from_charger); };

    callbacks.timed_out = [prefix] { EVLOG_warning << prefix << ": response watchdog timed out"; };

    callbacks.power_delivery_stopped = [out] { emit(out->power_delivery_stopped); };

    callbacks.welding_detection_failed = [out, prefix](float last_voltage_v, int cycles) {
        EVLOG_error << prefix << ": welding detection failed after " << cycles << " cycles, last voltage "
                    << last_voltage_v << " V";
        types::iso15118::WeldingDetectionFailure failure;
        failure.cycles = cycles;
        if (not std::isnan(last_voltage_v)) {
            failure.last_voltage_V = last_voltage_v;
        }
        emit(out->welding_detection_failed, failure);
    };

    callbacks.stopped = [prefix] { EVLOG_info << prefix << ": session stopped"; };

    callbacks.ev_power_ready = [out] { emit(out->ev_power_ready); };

    callbacks.dc_power_on = [out] { emit(out->dc_power_on); };

    callbacks.stop_from_charger = [out] { emit(out->stop_from_charger); };

    callbacks.ac_evse_max_current = [out](float current) { emit(out->ac_evse_max_current, current); };

    callbacks.ac_limits = [prefix](const dt::AC_CPDResEnergyTransferMode& limits) {
        EVLOG_info << prefix << ": AC EVSE limits: max charge power "
                   << dt::from_RationalNumber(limits.max_charge_power) << " W, min charge power "
                   << dt::from_RationalNumber(limits.min_charge_power) << " W";
    };

    callbacks.ac_bpt_limits = [prefix](const dt::BPT_AC_CPDResEnergyTransferMode& limits) {
        EVLOG_info << prefix << ": AC BPT EVSE limits: max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, min discharge power "
                   << dt::from_RationalNumber(limits.min_discharge_power) << " W";
    };

    callbacks.dc_bpt_limits = [prefix](const dt::BPT_DC_CPDResEnergyTransferMode& limits) {
        EVLOG_info << prefix << ": DC BPT EVSE limits: max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, min discharge power "
                   << dt::from_RationalNumber(limits.min_discharge_power) << " W";
    };

    callbacks.ac_der_limits = [prefix](const dt::DER_AC_CPDResEnergyTransferMode& limits) {
        EVLOG_info << prefix << ": AC DER EVSE limits: nominal charge power "
                   << dt::from_RationalNumber(limits.nominal_charge_power) << " W, nominal discharge power "
                   << dt::from_RationalNumber(limits.nominal_discharge_power) << " W, max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, operating mode "
                   << (limits.operating_mode == dt::OperatingMode::GridForming ? "GridForming" : "GridFollowing")
                   << ", grid connection mode "
                   << (limits.grid_connection_mode == dt::GridConnectionMode::GridIslanded ? "GridIslanded"
                                                                                           : "GridConnected");
    };

    callbacks.ac_target_power = [out](const iso15118::d20::AcTargetPower& control) {
        const auto convert = [](const std::optional<dt::RationalNumber>& value) -> std::optional<float> {
            return value.has_value() ? std::make_optional(dt::from_RationalNumber(*value)) : std::nullopt;
        };
        types::iso15118::AcTargetPower target;
        target.target_active_power = convert(control.target_active_power);
        target.target_active_power_L2 = convert(control.target_active_power_L2);
        target.target_active_power_L3 = convert(control.target_active_power_L3);
        target.target_reactive_power = convert(control.target_reactive_power);
        target.target_reactive_power_L2 = convert(control.target_reactive_power_L2);
        target.target_reactive_power_L3 = convert(control.target_reactive_power_L3);
        emit(out->ac_evse_target_power, target);
    };

    // Every Dynamic response carries this block; the session's first is published, and a later one
    // only when its setpoints change. Callbacks run on the session's loop thread only.
    auto last_der_control = std::make_shared<std::optional<types::iso15118::DerControlReceived>>();
    callbacks.der_control = [out, last_der_control](const dt::DER_Dynamic_AC_CLResControlMode& control) {
        types::iso15118::DerControlReceived received;
        received.flavor = types::iso15118::DerFlavor::AC_DER_IEC;
        received.source = types::iso15118::DerControlSource::ChargeLoop;
        if (control.dso_q_setpoint) {
            received.dso_q_setpoint = dt::from_RationalNumber(control.dso_q_setpoint->dso_q_setpoint_value);
        }
        if (control.dso_cos_phi_setpoint) {
            received.dso_cos_phi_setpoint =
                dt::from_RationalNumber(control.dso_cos_phi_setpoint->dso_cos_phi_setpoint_value);
        }
        const auto& last = *last_der_control;
        if (last and last->dso_q_setpoint == received.dso_q_setpoint and
            last->dso_cos_phi_setpoint == received.dso_cos_phi_setpoint) {
            return;
        }
        *last_der_control = received;
        emit(out->der_control_received, received);
    };

    // Dictated DER curves are observed, not applied. The published summary has no curve fields.
    callbacks.der_curves = [out, prefix](const dt::DerControl& control) {
        std::ostringstream line;
        line << prefix << ": DER curves dictated:";
        bool any = false;
        const auto append = [&](bool present, const char* name) {
            if (present) {
                line << ' ' << name;
                any = true;
            }
        };
        append(control.over_voltage_fault_ride_through.has_value(), "over_voltage_fault_ride_through");
        append(control.under_voltage_fault_ride_through.has_value(), "under_voltage_fault_ride_through");
        append(control.zero_current.has_value(), "zero_current");
        append(control.reactive_power_support.has_value(), "reactive_power_support");
        append(control.active_power_support.has_value(), "active_power_support");
        append(control.max_level_dc_injection.has_value(), "max_level_dc_injection");
        if (not any) {
            line << " none";
        }
        EVLOG_info << line.str();
        emit(out->der_control_received, types::iso15118::DerControlReceived{
                                            .flavor = types::iso15118::DerFlavor::AC_DER_IEC,
                                            .source = types::iso15118::DerControlSource::ChargeParameterDiscovery});
    };

    // The enabled modes the EV echoes, as the session last reported them. A block's own Enables are not
    // the set in force: the EV masks them with what it supports, and a charge loop block omits what it
    // does not update. The engine reports the set before it forwards the block that caused it.
    auto enabled_modes = std::make_shared<std::uint32_t>(0);
    callbacks.sae_cpd_limits = [out, enabled_modes](const dt::sae::DER_SAE_AC_CPDResEnergyTransferMode& limits,
                                                    const iso15118::ev::d20::der::DerControlProblems& problems) {
        emit(out->der_control_received,
             sae_der_control_received(types::iso15118::DerControlSource::ChargeParameterDiscovery,
                                      limits.der_control_cpd_res.enter_service_cpd_res.permit_service, *enabled_modes,
                                      problems));
    };

    callbacks.sae_der_control = [out, enabled_modes](const dt::sae::DER_Dynamic_AC_CLResControlMode& mode,
                                                     const iso15118::ev::d20::der::DerControlProblems& problems) {
        emit(out->der_control_received,
             sae_der_control_received(types::iso15118::DerControlSource::ChargeLoop,
                                      mode.der_control_cl_res.enter_service_cl_res.permit_service, *enabled_modes,
                                      problems));
    };

    // Each annex publishes under its own flavor: a resume joins the session the charger retained, which
    // need not be the energy mode the start asked for.
    callbacks.sae_enabled_modes = [out, enabled_modes](std::uint32_t bitmap) {
        *enabled_modes = bitmap;
        emit(out->der_negotiated_functions, der_negotiated_functions(types::iso15118::DerFlavor::AC_DER_SAE, bitmap));
    };
    callbacks.iec_demanded_functions = [out](std::uint32_t bitmap) {
        emit(out->der_negotiated_functions, der_negotiated_functions(types::iso15118::DerFlavor::AC_DER_IEC, bitmap));
    };

    return callbacks;
}

} // namespace everest::lib::iso15118_ev
