// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <utility>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/state/supported_app_protocol.hpp>

namespace iso15118::ev {

Session::Session(feedback::Callbacks callbacks, OutboundSend outbound_send_, session::SessionLogger& logger,
                 everest::lib::io::event::fd_event_handler& reactor_, SessionTiming timing_,
                 message_20::datatypes::Identifier evcc_id,
                 std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
                 everest::lib::util::monitor<DcChargeParams>* dc_params,
                 everest::lib::util::monitor<AcChargeParams>* ac_params,
                 message_20::datatypes::ServiceCategory energy_service) :
    log(logger),
    context(std::move(callbacks), message_exchange, log, std::move(evcc_id), std::move(advertised_app_protocols),
            active_control_event, (dc_params != nullptr) ? *dc_params : owned_dc_params,
            (ac_params != nullptr) ? *ac_params : owned_ac_params, energy_service),
    outbound_send(std::move(outbound_send_)),
    reactor(reactor_),
    timing(timing_) {

    // Single-shot timers, re-armed on demand. Registering them on the reactor lets
    // their expiry be dispatched on the reactor thread alongside socket events.
    send_delay_timer.set_single_shot(true);
    watchdog_timer.set_single_shot(true);

    reactor.register_event_handler(&send_delay_timer, [this]() { on_send_delay_expired(); });
    reactor.register_event_handler(&watchdog_timer, [this]() { on_watchdog_expired(); });
}

Session::~Session() {
    reactor.unregister_event_handler(&send_delay_timer);
    reactor.unregister_event_handler(&watchdog_timer);
}

void Session::set_on_finished(std::function<void()> on_finished_) {
    on_finished = std::move(on_finished_);
}

template <typename F> void Session::guarded(const char* op, F&& f) {
    // Single reactor exception boundary. Every reactor-reachable callback reaches
    // state code (or a consumer callback) that can throw, and the reactor's poll_impl
    // has no try/catch: an escape would kill the reactor thread, so the session would
    // neither finish nor signal. Stop loudly on any throw, then finish unconditionally.
    try {
        f();
    } catch (const std::exception& e) {
        logf_error("EV %s failed (%s); stopping the session", op, e.what());
        context.stop_session();
    } catch (...) {
        logf_error("EV %s failed (non-std exception); stopping the session", op);
        context.stop_session();
    }

    check_finished();
}

void Session::start() {
    guarded("session start", [this]() {
        // Lazily construct the FSM here: its constructor enters the initial
        // SupportedAppProtocol state, which queues the SAP request. Holding it back to
        // start() makes start() the trigger for producing the first request.
        fsm.emplace(context.create_state<d20::state::SupportedAppProtocol>());

        // Uniform timing: the first request is held for the send delay too (connect is
        // treated like any other "request ready" trigger).
        arm_send_delay();
    });
}

void Session::on_bytes_received(const std::vector<uint8_t>& bytes) {
    // A rejected header (bad SDP version -> INVALID_HEADER, or a payload longer than
    // the buffer -> PAYLOAD_TOO_LONG) is terminal: get_remaining_bytes_to_read() then
    // returns 0 forever, so the accumulator can never advance and every later byte is
    // silently dropped, with the failure masquerading as a watchdog timeout. Stop
    // loudly the moment the accumulator lands in such a state.
    const auto stop_on_malformed_frame = [this]() {
        const auto state = packet.get_state();
        if (state == io::SdpPacket::State::INVALID_HEADER or state == io::SdpPacket::State::PAYLOAD_TOO_LONG) {
            logf_error("EV received a malformed V2GTP frame (SdpPacket state %d); stopping the session",
                       static_cast<int>(state));
            context.stop_session();
            check_finished();
            return true;
        }
        return false;
    };

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto wanted = packet.get_remaining_bytes_to_read();
        if (wanted == 0) {
            // Terminal state with bytes still pending: either a completed frame already
            // handled below (benign, accumulator reset) or a rejected header. Guard the
            // error case; otherwise nothing more can be read into this accumulator.
            stop_on_malformed_frame();
            break;
        }

        const auto available = bytes.size() - offset;
        const auto chunk = std::min(wanted, available);

        std::memcpy(packet.get_current_buffer_pos(), bytes.data() + offset, chunk);
        packet.update_read_bytes(chunk);
        offset += chunk;

        // The header may have been parsed by this update; catch a rejection now rather
        // than waiting for a follow-up call to re-enter the wanted == 0 branch.
        if (stop_on_malformed_frame()) {
            break;
        }

        if (packet.is_complete()) {
            handle_complete_frame();
            packet = {}; // reset for the next frame
        }
    }
}

void Session::deliver_control_event(const d20::ControlEvent& event) {
    guarded("control-event delivery", [&]() {
        // Apply-before-feed: set the active event so states can read it via
        // get_control_event<T>(), feed CONTROL_MESSAGE, then clear it on every path
        // (including a throw) so a stale event cannot leak into a later feed.
        active_control_event = event;
        struct ClearOnExit {
            std::optional<d20::ControlEvent>& ev;
            ~ClearOnExit() {
                ev.reset();
            }
        } clear_on_exit{active_control_event};

        // Record a StopCharging request on the Context so it survives regardless of the
        // FSM state at delivery time (the active event is cleared after this feed).
        if (const auto* stop = std::get_if<d20::StopCharging>(&event); stop != nullptr and *stop) {
            context.set_stop_charging_requested(true);
        }
        if (fsm.has_value()) {
            fsm->feed(d20::Event::CONTROL_MESSAGE);
        }
        if (message_exchange.has_request()) {
            arm_send_delay();
        }
    });
}

void Session::handle_complete_frame() {
    // A response arrived: stop the watchdog for the request it answers. A failed
    // disarm is lower-impact (a stale single-shot timer that would only fire once)
    // but must not be fully silent.
    if (not watchdog_timer.disarm()) {
        logf_warning("EV failed to disarm the response watchdog after a response arrived");
    }

    // set_response / peek_response_type and the state code reached by feed() can throw
    // (MessageExchange runtime_errors, Variant "Illegal message type access",
    // std::bad_alloc); guarded() stops loudly and finishes on any escape.
    guarded("V2G response handling", [this]() {
        auto variant = std::make_unique<message_20::Variant>(
            packet.get_payload_type(), io::StreamInputView{packet.get_payload_buffer(), packet.get_payload_length()});

        message_exchange.set_response(std::move(variant));

        context.feedback.v2g_message(message_exchange.peek_response_type());

        if (fsm.has_value()) {
            fsm->feed(d20::Event::V2GTP_MESSAGE);
        }

        // The FSM may have produced the next request; hold it for the send delay.
        if (message_exchange.has_request()) {
            arm_send_delay();
        } else if (not context.is_session_stopped()) {
            // The watchdog was disarmed above, the state consumed the response, yet it
            // produced neither a follow-up request nor a session stop. No timer is left
            // armed, so the session would hang silently. Stop loudly instead.
            logf_warning("EV: state consumed a response without producing a request or stopping; "
                         "stopping the session");
            context.stop_session();
        }
    });
}

void Session::arm_send_delay() {
    // A zero (or negative) delay would disarm a timerfd (it_value == 0), so clamp
    // to the smallest positive duration: the request then transmits on the next
    // reactor pass.
    const bool armed = (timing.send_delay.count() <= 0) ? send_delay_timer.set_timeout(std::chrono::nanoseconds(1))
                                                        : send_delay_timer.set_timeout(timing.send_delay);

    // A failed arm of the send-delay timer means the pending request never
    // transmits: a silent hang. Stop the session loudly instead. The request stays
    // queued but can never be sent, so mark it unsendable to unwedge is_finished().
    if (not armed) {
        logf_error("EV failed to arm the send-delay timer; stopping the session");
        pending_request_unsendable = true;
        context.stop_session();
        check_finished();
    }
}

void Session::transmit_pending() {
    if (not message_exchange.has_request()) {
        return;
    }

    // take_request() returns nullopt only on a real error (encode failure). Stop the
    // session instead of transmitting a truncated frame.
    const auto taken = message_exchange.take_request();
    if (not taken.has_value()) {
        logf_error("EV request encoding failed; stopping the session");
        context.stop_session();
        check_finished();
        return;
    }

    const auto& [payload, payload_type] = taken.value();

    std::vector<uint8_t> frame(io::SdpPacket::V2GTP_HEADER_SIZE + payload.size());
    V2GTP20_WriteHeader(frame.data(), static_cast<uint32_t>(payload.size()), static_cast<uint16_t>(payload_type));
    std::copy(payload.begin(), payload.end(), frame.begin() + io::SdpPacket::V2GTP_HEADER_SIZE);

    // The outbound seam reports the send result. A refused transmit leaves no frame
    // on the wire, so arming the watchdog would only masquerade the failure as a
    // response timeout. Stop loudly instead.
    if (not outbound_send(std::move(frame))) {
        logf_error("EV failed to send the request frame; stopping the session");
        context.stop_session();
        check_finished();
        return;
    }

    // Arm the response watchdog for the request just sent, unless this was a final
    // message flushed after the session was already stopped (no response expected).
    if (not context.is_session_stopped()) {
        if (not watchdog_timer.set_timeout(timing.response_timeout)) {
            logf_error("EV failed to arm the response watchdog; stopping the session");
            context.stop_session();
            check_finished();
        }
    }
}

void Session::on_send_delay_expired() {
    guarded("send-delay expiry", [this]() { transmit_pending(); });
}

void Session::on_watchdog_expired() {
    // No response arrived in time: the session is over regardless of what the FSM does.
    context.stop_session();

    // Let a state react (future states may emit a SessionStop) and flush any final
    // message it produces before finishing.
    guarded("watchdog FSM handling", [this]() {
        if (fsm.has_value()) {
            fsm->feed(d20::Event::FAILED);
        }
        if (message_exchange.has_request()) {
            arm_send_delay();
        }
    });

    // Signal the timeout exactly once. timed_out() invokes a consumer-supplied
    // callback that can throw; a separate guarded step fires it regardless of the FSM
    // handling above and keeps a throw from escaping into the reactor's poll_impl.
    guarded("timed-out feedback", [this]() { context.feedback.timed_out(); });
}

void Session::check_finished() {
    if (is_finished() and not finished_signalled) {
        // Set the flag BEFORE the callback so it fires at most once even if it throws: a
        // re-entry could otherwise double-fire it. on_finished is owner-supplied
        // (the Controller clears `online`) and runs on the reactor thread, which has
        // no try/catch, so guard it the way the other reactor-reachable seams are.
        finished_signalled = true;
        if (on_finished) {
            try {
                on_finished();
            } catch (const std::exception& e) {
                logf_error("EV session on_finished callback threw (%s)", e.what());
            } catch (...) {
                logf_error("EV session on_finished callback threw a non-std exception");
            }
        }
    }
}

bool Session::is_finished() const {
    // Finished once stopped with nothing left to flush. A request that can never
    // transmit (send-delay arm failed) must not keep the session wedged open.
    return context.is_session_stopped() and (pending_request_unsendable or not message_exchange.has_request());
}

} // namespace iso15118::ev
