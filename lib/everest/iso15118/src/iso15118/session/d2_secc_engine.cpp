// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/d2_secc_engine.hpp>

#include <memory>
#include <utility>

#include <iso15118/d2/state/session_setup.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_2/variant.hpp>

namespace iso15118 {

namespace {

namespace m2dt = message_2::datatypes;
namespace m20dt = message_20::datatypes;

// Builds the SECC-side ISO 15118-2 config from the generic d20 EvseSetupConfig-derived SessionConfig.
d2::SessionConfig make_d2_config(const session::SessionConfig& config) {
    d2::SessionConfig out;
    out.evse_id = config.evse_id;

    bool has_dc = false;
    bool has_ac = false;
    for (const auto& service : config.supported_energy_transfer_services) {
        if (service == m20dt::ServiceCategory::DC or service == m20dt::ServiceCategory::DC_BPT or
            service == m20dt::ServiceCategory::MCS or service == m20dt::ServiceCategory::MCS_BPT) {
            has_dc = true;
        } else if (service == m20dt::ServiceCategory::AC or service == m20dt::ServiceCategory::AC_BPT) {
            has_ac = true;
        }
    }
    // Fall back to DC when the config carries no recognised energy service.
    if (not has_dc and not has_ac) {
        has_dc = true;
    }

    if (has_dc) {
        out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::DC_extended);
        out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::DC_core);
    }
    if (has_ac) {
        out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::AC_three_phase_core);
        out.supported_energy_transfer_modes.push_back(m2dt::EnergyTransferMode::AC_single_phase_core);
    }

    // DC limits from the d20 DcTransferLimits (RationalNumber -> float).
    const auto& dc = config.dc_limits;
    out.dc_max_power = m20dt::from_RationalNumber(dc.charge_limits.power.max);
    out.dc_max_current = m20dt::from_RationalNumber(dc.charge_limits.current.max);
    out.dc_min_current = m20dt::from_RationalNumber(dc.charge_limits.current.min);
    out.dc_max_voltage = m20dt::from_RationalNumber(dc.voltage.max);
    out.dc_min_voltage = m20dt::from_RationalNumber(dc.voltage.min);
    if (out.dc_max_power <= 0.0f) {
        out.dc_max_power = 150000.0f;
    }
    if (out.dc_max_current <= 0.0f) {
        out.dc_max_current = 300.0f;
    }
    if (out.dc_max_voltage <= 0.0f) {
        out.dc_max_voltage = 900.0f;
    }

    // AC nominal voltage is not represented in the d20 limits; default to 230 V and derive the max
    // current from the advertised charge power.
    out.ac_nominal_voltage = 230.0f;
    const auto ac_power = m20dt::from_RationalNumber(config.ac_limits.charge_power.max);
    if (ac_power > 0.0f) {
        out.ac_max_current = ac_power / out.ac_nominal_voltage;
    }

    // Plug-and-Charge (Contract payment) config, threaded from the module via EvseSetupConfig.
    out.pnc_enabled = config.iso2_pnc_enabled;
    out.mo_root_cert_path = config.contract_mo_root_path;
    out.v2g_root_cert_path = config.contract_v2g_root_path;
    out.receipt_required = config.iso2_receipt_required;

    return out;
}

} // namespace

D2SeccEngine::D2SeccEngine(io::StreamOutputView output_view, const session::SessionConfig& config,
                           std::optional<d2::PauseContext>& pause_ctx, session::feedback::Callbacks callbacks,
                           session::SessionLogger& log, d20::Timeouts& timeouts) :
    message_exchange(output_view),
    ctx(std::move(callbacks), log, make_d2_config(config), pause_ctx, active_control_event, message_exchange, timeouts),
    fsm(ctx.create_state<d2::state::SessionSetup>()) {
}

void D2SeccEngine::on_packet(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
    // All ISO 15118-2 messages share the single SAP payload type (0x8001); a frame carrying any other
    // V2GTP payload type is ignored (on par with the EvseV2G stack / libiso15118 finding F-001).
    if (payload_type != io::v2gtp::PayloadType::SAP) {
        return;
    }
    // disambiguation of the concrete message happens at decode.
    message_exchange.set_request(std::make_unique<message_2::Variant>(view));

    // Report the concrete incoming ISO 15118-2 request type so the module logs its real name.
    ctx.feedback.v2g_message(ctx.peek_request_type());

    drive_request(fsm, message_exchange, d2::Event::V2GTP_MESSAGE);
}

void D2SeccEngine::on_control_event(const d20::ControlEvent& event) {
    active_control_event = event;
    [[maybe_unused]] const auto res = fsm.feed(d2::Event::CONTROL_MESSAGE);
    active_control_event.reset();
}

void D2SeccEngine::on_timeout(d20::TimeoutType timeout) {
    if (timeout == d20::TimeoutType::SEQUENCE) {
        logf_error("Sequence Timeout is reached. Stopping the session");
        ctx.session_stopped = true;
        return;
    }

    ctx.set_active_timeout(timeout);
    [[maybe_unused]] const auto res = fsm.feed(d2::Event::TIMEOUT);
}

bool D2SeccEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<SeccEngine::Outgoing> D2SeccEngine::take_outgoing() {
    const auto [got_response, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_response) {
        return std::nullopt;
    }
    // message_type is the concrete message_2::Type; report it so the module logs the real name.
    return Outgoing{payload_size, payload_type, message_type};
}

bool D2SeccEngine::is_finished() const {
    return (ctx.session_stopped or ctx.session_paused) and not message_exchange.has_response();
}

bool D2SeccEngine::is_paused() const {
    return ctx.session_paused;
}

void D2SeccEngine::request_shutdown() {
    ctx.request_shutdown();
}

} // namespace iso15118
