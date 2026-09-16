// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>

namespace iso15118::ev {

namespace {

template <typename F, typename V> auto with_engine(V& engine, F&& f) {
    using EngineRef = std::conditional_t<std::is_const_v<V>, const d20::Engine&, d20::Engine&>;
    // Every engine offers the same surface; the -20 one names the return type.
    using R = std::invoke_result_t<F, EngineRef>;
    return std::visit(
        [&](auto& e) -> R {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, std::monostate>) {
                throw std::logic_error("EV Session has no engine");
            } else {
                return f(e);
            }
        },
        engine);
}

// Engine-neutral view of one feed.
struct NeutralOutcome {
    Disposition output;
    bool transitioned;
    int state_before;
    const char* violation;
};

} // namespace

Session::Session(feedback::Callbacks callbacks_, OutboundSend outbound_send_,
                 everest::lib::io::event::fd_event_handler& reactor_, SessionTiming timing_,
                 message_20::datatypes::Identifier evcc_id,
                 std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
                 everest::lib::util::monitor<DcChargeParams>* dc_params_,
                 everest::lib::util::monitor<AcChargeParams>* ac_params_,
                 message_20::datatypes::ServiceCategory energy_service, DerControlFunctions der_control_functions,
                 bool der_stop_on_unsupported_functions, d20::SessionOptions options, EvSessionParams params_) :
    callbacks(callbacks_),
    feedback(callbacks_),
    dc_params((dc_params_ != nullptr) ? *dc_params_ : owned_dc_params),
    params(std::move(params_)),
    has_cp_state_feedback(options.has_cp_state_feedback),
    resumed_session_id(options.resumed_session_id),
    outbound_send(std::move(outbound_send_)),
    reactor(reactor_),
    timing(timing_) {

    // The engine's Context keeps references to these monitors, so they must outlive it: the
    // caller's, or the owned fallbacks declared above the engine.
    engine.emplace<d20::Engine>(callbacks, std::move(evcc_id), std::move(advertised_app_protocols),
                                active_control_event, dc_params,
                                (ac_params_ != nullptr) ? *ac_params_ : owned_ac_params, energy_service,
                                der_control_functions, der_stop_on_unsupported_functions, std::move(options));

    send_delay_timer.set_single_shot(true);
    watchdog_timer.set_single_shot(true);
    ongoing_timer.set_single_shot(true);

    if (not reactor.register_event_handler(&send_delay_timer, [this]() { on_send_delay_expired(); })) {
        throw std::runtime_error("EV Session: failed to register the send-delay timer on the reactor");
    }
    if (not reactor.register_event_handler(&watchdog_timer, [this]() { on_watchdog_expired(); })) {
        reactor.unregister_event_handler(&send_delay_timer);
        throw std::runtime_error("EV Session: failed to register the response watchdog on the reactor");
    }
    if (not reactor.register_event_handler(&ongoing_timer, [this]() { on_ongoing_expired(); })) {
        reactor.unregister_event_handler(&send_delay_timer);
        reactor.unregister_event_handler(&watchdog_timer);
        throw std::runtime_error("EV Session: failed to register the ongoing guard on the reactor");
    }
}

Session::~Session() {
    reactor.unregister_event_handler(&send_delay_timer);
    reactor.unregister_event_handler(&watchdog_timer);
    reactor.unregister_event_handler(&ongoing_timer);
}

void Session::set_on_finished(std::function<void()> on_finished_) {
    on_finished = std::move(on_finished_);
}

template <typename F> void Session::guarded(const char* op, F&& f) {
    try {
        f();
    } catch (const std::exception& ex) {
        logf_error("EV %s failed (%s); stopping the session", op, ex.what());
        // A request left in the exchange would keep is_finished() false forever.
        with_engine(engine, [](auto& e) {
            e.stop();
            e.discard_request();
        });
    } catch (...) {
        logf_error("EV %s failed (non-std exception); stopping the session", op);
        with_engine(engine, [](auto& e) {
            e.stop();
            e.discard_request();
        });
    }

    check_finished();
}

void Session::start() {
    guarded("session start", [this]() {
        with_engine(engine, [](auto& e) { e.start(); });
        arm_send_delay();
    });
}

void Session::on_bytes_received(const std::vector<uint8_t>& bytes) {
    // INVALID_HEADER / PAYLOAD_TOO_LONG are terminal for the accumulator.
    const auto stop_on_malformed_frame = [this]() {
        const auto state = packet.get_state();
        if (state == io::SdpPacket::State::INVALID_HEADER or state == io::SdpPacket::State::PAYLOAD_TOO_LONG) {
            logf_error("EV received a malformed V2GTP frame (SdpPacket state %d); stopping the session",
                       static_cast<int>(state));
            with_engine(engine, [](auto& e) { e.stop(); });
            check_finished();
            return true;
        }
        return false;
    };

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto wanted = packet.get_remaining_bytes_to_read();
        if (wanted == 0) {
            stop_on_malformed_frame();
            break;
        }

        const auto available = bytes.size() - offset;
        const auto chunk = std::min(wanted, available);

        std::memcpy(packet.get_current_buffer_pos(), bytes.data() + offset, chunk);
        packet.update_read_bytes(chunk);
        offset += chunk;

        if (stop_on_malformed_frame()) {
            break;
        }

        if (packet.is_complete()) {
            handle_complete_frame();
            packet = {};
        }
    }
}

void Session::on_peer_closed() {
    if (is_finished()) {
        return;
    }
    const bool stopped = with_engine(engine, [](auto& e) { return e.is_stopped(); });
    if (not stopped) {
        logf_warning("EV peer closed the connection mid-session; stopping the session");
        with_engine(engine, [](auto& e) {
            e.stop();
            e.discard_request();
        });
    } else {
        // Stopped with a final request still held: it can no longer be sent.
        with_engine(engine, [](auto& e) { e.discard_request(); });
    }
    watchdog_timer.disarm();
    ongoing_timer.disarm();
    ongoing_armed = false;
    check_finished();
}

void Session::deliver_control_event(const d20::ControlEvent& event) {
    if (is_finished()) {
        return;
    }
    guarded("control-event delivery", [&]() {
        active_control_event = event;
        struct ClearOnExit {
            std::optional<d20::ControlEvent>& ev;
            ~ClearOnExit() {
                ev.reset();
            }
        } clear_on_exit{active_control_event};

        with_engine(engine, [&](auto& e) {
            // Latched on the engine: the FSM state at delivery time may not consume the event.
            e.latch(event);
            if (e.started()) {
                feed_fsm(d20::Event::CONTROL_MESSAGE);
            }
        });
        // feed_fsm() may switch_engine(); the engine reference above is dead by now.
        if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
            arm_send_delay();
        }
    });
}

void Session::terminate() {
    if (is_finished()) {
        return;
    }
    logf_warning("EV terminating the V2G session without SessionStop");
    with_engine(engine, [](auto& e) {
        e.stop();
        e.discard_request();
    });
    send_delay_timer.disarm();
    watchdog_timer.disarm();
    ongoing_timer.disarm();
    ongoing_armed = false;
    check_finished();
}

void Session::feed_fsm(d20::Event ev) {
    const auto outcome = with_engine(engine, [&](auto& e) {
        const auto o = e.feed(ev);
        return NeutralOutcome{o.output, o.transitioned, static_cast<int>(o.state_before), o.violation};
    });

    if (outcome.violation != nullptr) {
        logf_error("EV state %d declared %d but %s; stopping the session", outcome.state_before,
                   static_cast<int>(outcome.output), outcome.violation);
        with_engine(engine, [](auto& e) {
            e.stop();
            e.discard_request();
        });
        return;
    }

    if (outcome.output == Disposition::Handover) {
        const auto protocol = with_engine(engine, [](auto& e) { return e.negotiated_protocol(); });
        switch_engine(protocol.value());
        return;
    }

    update_ongoing_guard(outcome.transitioned);
}

void Session::switch_engine(ProtocolId protocol) {
    // Carry the latches of the SAP phase into the new engine.
    const bool stop_requested = with_engine(engine, [](auto& e) { return e.context().is_stop_charging_requested(); });
    const bool pause_requested = with_engine(engine, [](auto& e) { return e.context().is_pause_charging_requested(); });
    const bool cp_c_or_d = with_engine(engine, [](auto& e) { return e.context().cp_state_c_or_d(); });

    switch (protocol) {
    case ProtocolId::ISO15118_2:
        engine.emplace<d2::Engine>(callbacks, params, active_control_event, dc_params, has_cp_state_feedback,
                                   resumed_session_id);
        break;
    case ProtocolId::DIN70121:
        engine.emplace<din::Engine>(callbacks, params, active_control_event, dc_params, has_cp_state_feedback,
                                    resumed_session_id);
        break;
    case ProtocolId::ISO15118_20:
        logf_error("EV handover to ISO 15118-20 requested from the -20 engine; stopping the session");
        with_engine(engine, [](auto& e) { e.stop(); });
        return;
    }
    logf_info("EV switched to the %s engine", protocol_id_to_string(protocol));

    with_engine(engine, [&](auto& e) {
        if (stop_requested) {
            e.latch(d20::ControlEvent{d20::StopCharging{true}});
        }
        if (pause_requested) {
            e.latch(d20::ControlEvent{d20::PauseCharging{true}});
        }
        if (cp_c_or_d) {
            e.latch(d20::ControlEvent{d20::CpState{true}});
        }
        e.start();
    });
    update_ongoing_guard(true);
    if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
        arm_send_delay();
    }
}

void Session::update_ongoing_guard(bool transitioned) {
    if (not transitioned) {
        return;
    }
    if (ongoing_armed) {
        ongoing_timer.disarm();
        ongoing_armed = false;
    }
    const auto bound = with_engine(engine, [](auto& e) { return e.ongoing_timeout(); });
    if (bound.has_value()) {
        if (ongoing_timer.set_timeout(bound.value())) {
            ongoing_armed = true;
        } else {
            logf_warning("EV failed to arm the ongoing guard");
        }
    }
}

void Session::handle_complete_frame() {
    if (not watchdog_timer.disarm()) {
        logf_warning("EV failed to disarm the response watchdog after a response arrived");
    }

    guarded("V2G response handling", [this]() {
        with_engine(engine, [&](auto& e) {
            if (not e.stage_response(packet.get_payload_type(),
                                     io::StreamInputView{packet.get_payload_buffer(), packet.get_payload_length()})) {
                // Frame dropped: keep waiting for the real response.
                if (not e.is_stopped()) {
                    watchdog_timer.set_timeout(effective_response_timeout());
                }
                return;
            }
            feedback.v2g_message(e.peek_response_type());
            if (e.started()) {
                feed_fsm(d20::Event::V2GTP_MESSAGE);
            }
        });
        if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
            arm_send_delay();
        }
    });
}

void Session::arm_send_delay() {
    // Pre-20 engines pace requests (EvseV2G MAX_RES_TIME parity). A zero delay would disarm a timerfd
    // (it_value == 0); clamp to the smallest positive duration.
    const auto delay =
        std::max(timing.send_delay, with_engine(engine, [](auto& e) { return e.min_request_interval(); }));
    const bool armed = (delay.count() <= 0) ? send_delay_timer.set_timeout(std::chrono::nanoseconds(1))
                                            : send_delay_timer.set_timeout(delay);

    if (not armed) {
        logf_error("EV failed to arm the send-delay timer; stopping the session");
        pending_request_unsendable = true;
        with_engine(engine, [](auto& e) { e.stop(); });
        check_finished();
    }
}

void Session::transmit_pending() {
    const bool has_request = with_engine(engine, [](auto& e) { return e.has_request(); });
    if (not has_request) {
        return;
    }

    const auto taken = with_engine(engine, [](auto& e) { return e.take_request(); });
    if (not taken.has_value()) {
        logf_error("EV request encoding failed; stopping the session");
        with_engine(engine, [](auto& e) { e.stop(); });
        check_finished();
        return;
    }

    const auto& [payload, payload_type] = taken.value();

    std::vector<uint8_t> frame(io::SdpPacket::V2GTP_HEADER_SIZE + payload.size());
    V2GTP20_WriteHeader(frame.data(), static_cast<uint32_t>(payload.size()), static_cast<uint16_t>(payload_type));
    std::copy(payload.begin(), payload.end(), frame.begin() + io::SdpPacket::V2GTP_HEADER_SIZE);

    if (not outbound_send(std::move(frame))) {
        logf_error("EV failed to send the request frame; stopping the session");
        with_engine(engine, [](auto& e) { e.stop(); });
        check_finished();
        return;
    }

    // No response is expected for a final message flushed after the stop.
    const bool stopped = with_engine(engine, [](auto& e) { return e.is_stopped(); });
    if (not stopped) {
        if (not watchdog_timer.set_timeout(effective_response_timeout())) {
            logf_error("EV failed to arm the response watchdog; stopping the session");
            with_engine(engine, [](auto& e) { e.stop(); });
            check_finished();
        }
    }
}

std::chrono::milliseconds Session::effective_response_timeout() const {
    return (timing.response_timeout.count() > 0)
               ? timing.response_timeout
               : with_engine(engine, [](const auto& e) { return e.response_timeout(); });
}

void Session::on_send_delay_expired() {
    guarded("send-delay expiry", [this]() { transmit_pending(); });
}

void Session::on_watchdog_expired() {
    logf_error("EV response watchdog expired; stopping the session");
    with_engine(engine, [](auto& e) { e.stop(); });
    ongoing_timer.disarm();
    ongoing_armed = false;

    guarded("watchdog FSM handling", [this]() {
        if (with_engine(engine, [](auto& e) { return e.started(); })) {
            feed_fsm(d20::Event::FAILED);
        }
        if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
            arm_send_delay();
        }
    });

    guarded("timed-out feedback", [this]() { feedback.timed_out(); });
}

void Session::on_ongoing_expired() {
    ongoing_armed = false;
    logf_error("EV ongoing guard expired in state %d; stopping the session", with_engine(engine, [](auto& e) {
                   const auto state = e.current_state();
                   return state.has_value() ? static_cast<int>(*state) : -1;
               }));
    watchdog_timer.disarm();
    with_engine(engine, [](auto& e) {
        e.stop();
        e.discard_request();
    });
    guarded("ongoing-guard FSM handling", [this]() {
        if (with_engine(engine, [](auto& e) { return e.started(); })) {
            feed_fsm(d20::Event::FAILED);
        }
        if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
            arm_send_delay();
        }
    });
    guarded("timed-out feedback", [this]() { feedback.timed_out(); });
}

void Session::check_finished() {
    if (is_finished() and not finished_signalled) {
        finished_signalled = true;
        try {
            feedback.signal(is_paused() ? feedback::Signal::DLINK_PAUSE : feedback::Signal::DLINK_TERMINATE);
        } catch (const std::exception& e) {
            logf_error("EV session signal callback threw (%s)", e.what());
        } catch (...) {
            logf_error("EV session signal callback threw a non-std exception");
        }
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
    return std::visit(
        [&](const auto& e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, std::monostate>) {
                return true;
            } else {
                return e.is_stopped() and (pending_request_unsendable or not e.has_request());
            }
        },
        engine);
}

bool Session::is_paused() const {
    return std::visit(
        [&](const auto& e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, std::monostate>) {
                return false;
            } else {
                return e.is_paused();
            }
        },
        engine);
}

std::optional<std::array<uint8_t, 8>> Session::session_id() const {
    return std::visit(
        [&](const auto& e) -> std::optional<std::array<uint8_t, 8>> {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, std::monostate>) {
                return std::nullopt;
            } else {
                return e.session_id();
            }
        },
        engine);
}

std::optional<ProtocolId> Session::selected_protocol() const {
    return std::visit(
        [&](const auto& e) -> std::optional<ProtocolId> {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, std::monostate>) {
                return std::nullopt;
            } else {
                return e.negotiated_protocol();
            }
        },
        engine);
}

} // namespace iso15118::ev
