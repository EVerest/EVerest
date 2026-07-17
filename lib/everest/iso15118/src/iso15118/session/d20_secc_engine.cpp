// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/d20_secc_engine.hpp>

#include <memory>
#include <utility>

#include <iso15118/d20/state/session_setup.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/message/variant.hpp>

namespace iso15118 {

D20SeccEngine::D20SeccEngine(io::StreamOutputView output_view, session::SessionConfig config,
                             std::optional<d20::PauseContext>& pause_ctx, session::feedback::Callbacks callbacks,
                             session::SessionLogger& log, d20::Timeouts& timeouts,
                             const d20::EVSupportedAppProtocols& offered_protocols,
                             const message_20::SupportedAppProtocol& selected_protocol,
                             std::optional<io::sha512_hash_t> vehicle_cert_hash) :
    message_exchange(output_view),
    ctx(std::move(callbacks), log, std::move(config), pause_ctx, active_control_event, message_exchange, timeouts),
    fsm(ctx.create_state<d20::state::SessionSetup>()) {
    // The SupportedAppProtocol negotiation ran in the driver before this engine existed; hand its result
    // to the context so the ev_information feedback emitted in SessionSetup carries it [flow spec].
    ctx.ev_info.ev_supported_app_protocols = offered_protocols;
    ctx.ev_info.selected_app_protocol = selected_protocol;
    ctx.set_new_vehicle_cert_hash(vehicle_cert_hash);
}

void D20SeccEngine::on_packet(io::v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
    message_exchange.set_request(std::make_unique<message_20::Variant>(payload_type, view));

    const auto request_msg_type = ctx.peek_request_type();
    ctx.feedback.v2g_message(request_msg_type);

    drive_request(fsm, message_exchange, d20::Event::V2GTP_MESSAGE);
}

void D20SeccEngine::on_control_event(const d20::ControlEvent& event) {
    active_control_event = event;

    if (const auto control_data = ctx.get_control_event<d20::DcTransferLimits>()) {
        ctx.session_config.dc_limits = *control_data;
    } else if (const auto control_data = ctx.get_control_event<d20::EnergyServices>()) {
        ctx.session_config.supported_energy_transfer_services = *control_data;
    } else if (const auto control_data = ctx.get_control_event<d20::SupportedVASs>()) {
        ctx.session_config.supported_vas_services = *control_data;
    } else if (const auto control_data = ctx.get_control_event<d20::AcTransferLimits>()) {
        ctx.session_config.ac_limits = *control_data;
    } else if (const auto control_data = ctx.get_control_event<d20::UpdateDynamicModeParameters>()) {
        ctx.cache_dynamic_mode_parameters.emplace(*control_data);
    } else if (const auto control_data = ctx.get_control_event<d20::AcTargetPower>()) {
        ctx.cache_ac_target_power.emplace(*control_data);
    } else if (const auto control_data = ctx.get_control_event<d20::AcPresentPower>()) {
        ctx.cache_ac_present_power.emplace(*control_data);
    } else if (const auto control_data = ctx.get_control_event<d20::EvseError>()) {
        // ISO 15118-20 uses a different EVSE-status model; the DC status-code stamp is a -20-specific
        // follow-up, but an emergency shutdown must still abort the session.
        if (control_data->code == d20::EvseErrorCode::EmergencyShutdown) {
            logf_error("EVSE emergency shutdown reported; aborting the ISO 15118-20 session");
            ctx.session_stopped = true;
        }
    }
    // Save some control events. It can happen that these events are sent before the corresponding state. They are
    // stored temporarily here.

    [[maybe_unused]] const auto res = fsm.feed(d20::Event::CONTROL_MESSAGE);
    active_control_event.reset();
}

void D20SeccEngine::on_timeout(d20::TimeoutType timeout) {
    if (timeout == d20::TimeoutType::SEQUENCE) {
        logf_error("Sequence Timeout 40secs is reached. Stopping the session");
        ctx.session_stopped = true;
        return;
    }

    ctx.set_active_timeout(timeout);
    [[maybe_unused]] const auto res = fsm.feed(d20::Event::TIMEOUT);
}

bool D20SeccEngine::has_outgoing() const {
    return message_exchange.has_response();
}

std::optional<SeccEngine::Outgoing> D20SeccEngine::take_outgoing() {
    const auto [got_response, payload_size, payload_type, message_type] = message_exchange.check_and_clear_response();
    if (not got_response) {
        return std::nullopt;
    }
    return Outgoing{payload_size, payload_type, message_type};
}

bool D20SeccEngine::is_finished() const {
    return (ctx.session_stopped or ctx.session_paused) and not message_exchange.has_response();
}

bool D20SeccEngine::is_paused() const {
    return ctx.session_paused;
}

std::optional<session::feedback::SessionStopAction> D20SeccEngine::pop_session_stop_res_pending() {
    return std::exchange(ctx.session_stop_res_pending, std::nullopt);
}

void D20SeccEngine::request_shutdown() {
    ctx.request_shutdown();
}

} // namespace iso15118
