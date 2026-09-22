// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/detail/session/session.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/v2gtp/sdp.hpp>

namespace iso15118::ev::session {

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
    std::optional<std::string_view> violation;
};

} // namespace

Session::Session(feedback::Callbacks callbacks_, OutboundSend outbound_send_,
                 everest::lib::io::event::fd_event_handler& reactor_, SessionTiming timing_,
                 message_20::datatypes::Identifier evcc_id,
                 std::vector<message_20::SupportedAppProtocol> advertised_app_protocols,
                 everest::lib::util::monitor<params::DcChargeParams>* dc_params_,
                 everest::lib::util::monitor<params::AcChargeParams>* ac_params_,
                 message_20::datatypes::ServiceCategory energy_service, d20::SessionOptions options,
                 params::EvSessionParams params_) :
    callbacks(callbacks_),
    feedback(callbacks_),
    dc_params((dc_params_ != nullptr) ? *dc_params_ : owned_dc_params),
    params(std::move(params_)),
    has_cp_state_feedback(options.has_cp_state_feedback),
    welding_detection(options.welding_detection),
    resumed_session_id(options.resume.has_value() ? std::optional{options.resume->session_id} : std::nullopt),
    outbound_send(std::move(outbound_send_)),
    reactor(reactor_),
    timing(timing_) {

    // The engine's Context keeps references to these monitors, so they must outlive it: the
    // caller's, or the owned fallbacks declared above the engine.
    engine.emplace<d20::Engine>(callbacks, std::move(evcc_id), std::move(advertised_app_protocols), latches, dc_params,
                                (ac_params_ != nullptr) ? *ac_params_ : owned_ac_params, energy_service,
                                std::move(options));

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

void Session::abort(std::string_view reason, LogLevel level) noexcept {
    logf(level, "%.*s", static_cast<int>(reason.size()), reason.data());
    // std::visit on a valueless variant throws, and with_engine throws on monostate by design.
    // Neither may happen here: this also runs from an exception handler.
    if (not engine.valueless_by_exception() and not std::holds_alternative<std::monostate>(engine)) {
        try {
            // A request left in the exchange would keep is_finished() false forever.
            with_engine(engine, [](auto& e) {
                e.stop();
                e.discard_request();
            });
        } catch (...) {
            // Nothing further is possible; is_finished() reports a valueless engine as finished.
        }
    }
    send_delay_timer.disarm();
    watchdog_timer.disarm();
    ongoing_timer.disarm();
    ongoing_armed = false;
    // Safe inside a feed: on_bytes_received, deliver_control_event, terminate and on_peer_closed
    // return early once is_finished(), and every timer is disarmed, so nothing is left to transmit.
    check_finished();
}

void Session::abort_after_throw(const char* op, const char* what) noexcept {
    try {
        abort("EV " + std::string(op) + " failed (" + what + "); stopping the session");
    } catch (...) {
        abort("EV operation failed; stopping the session");
    }
}

template <typename F> void Session::guarded(const char* op, F&& f) {
    try {
        f();
    } catch (const std::exception& ex) {
        abort_after_throw(op, ex.what());
    } catch (...) {
        abort_after_throw(op, "non-std exception");
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
    if (is_finished()) {
        return;
    }

    // INVALID_HEADER / PAYLOAD_TOO_LONG end the frame in the accumulator. ISO 15118-2 and -20 drop it
    // and keep the response deadline [V2G2-800] [V2G20-800]; a strict engine (DIN SPEC 70121) closes the
    // connection. True when the session stopped.
    const auto handle_malformed_frame = [this]() {
        const auto state = packet.get_state();
        if (state == io::SdpPacket::State::INVALID_HEADER or state == io::SdpPacket::State::PAYLOAD_TOO_LONG) {
            if (not with_engine(engine, [](const auto& e) { return e.strict_framing(); })) {
                logf_warning("EV dropping a malformed V2GTP frame (SdpPacket state %d)", static_cast<int>(state));
                packet.reset();
                return false;
            }
            abort("EV received a malformed V2GTP frame (SdpPacket state " + std::to_string(static_cast<int>(state)) +
                  "); stopping the session");
            return true;
        }
        return false;
    };

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto wanted = packet.get_remaining_bytes_to_read();
        if (wanted == 0) {
            handle_malformed_frame();
            break;
        }

        const auto available = bytes.size() - offset;
        const auto chunk = std::min(wanted, available);

        std::memcpy(packet.get_current_buffer_pos(), bytes.data() + offset, chunk);
        packet.update_read_bytes(chunk);
        offset += chunk;

        if (handle_malformed_frame()) {
            break;
        }

        if (packet.is_complete()) {
            handle_complete_frame();
            packet = {};
            if (is_finished()) {
                break;
            }
        }
    }
}

void Session::on_peer_closed() {
    if (is_finished()) {
        return;
    }
    const bool stopped = with_engine(engine, [](auto& e) { return e.is_stopped(); });
    // After the stop the close is regular, but a final request still held can no longer be sent.
    abort(stopped ? "EV peer closed the connection after the stop"
                  : "EV peer closed the connection mid-session; stopping the session",
          stopped ? LogLevel::Debug : LogLevel::Warning);
}

void Session::deliver_control_event(const d20::ControlEvent& event) {
    if (is_finished()) {
        return;
    }
    guarded("control-event delivery", [&]() {
        // Latched: the FSM state at delivery time may not consume the event.
        latch(latches, event);
        const auto handover = with_engine(engine, [&](auto& e) -> std::optional<ProtocolId> {
            if (e.started()) {
                return feed_fsm(d20::Event::CONTROL_MESSAGE);
            }
            return std::nullopt;
        });
        if (handover.has_value()) {
            switch_engine(*handover);
        }
        if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
            arm_send_delay();
        }
    });
}

void Session::terminate() {
    if (is_finished()) {
        return;
    }
    abort("EV terminating the V2G session without SessionStop", LogLevel::Warning);
}

std::optional<ProtocolId> Session::feed_fsm(d20::Event ev) {
    const auto outcome = with_engine(engine, [&](auto& e) {
        const auto o = e.feed(ev);
        return NeutralOutcome{o.output, o.transitioned, static_cast<int>(o.state_before), o.violation};
    });

    if (outcome.violation.has_value()) {
        abort("EV state " + std::to_string(outcome.state_before) + " declared " +
              std::to_string(static_cast<int>(outcome.output)) + " but " + std::string(*outcome.violation) +
              "; stopping the session");
        return std::nullopt;
    }

    if (outcome.output == Disposition::Handover) {
        // Reported rather than performed: replacing the engine here would destroy the alternative
        // the caller's visitor still holds a reference to.
        return with_engine(engine, [](auto& e) { return e.negotiated_protocol(); });
    }

    update_ongoing_guard(outcome.transitioned);
    return std::nullopt;
}

void Session::switch_engine(ProtocolId protocol) {
    // An arm per generation, so -Wswitch catches one nobody wired up. The ISO 15118-20 arm is
    // unreachable by construction: d20::Engine reports a handover only for a protocol other than its
    // own, and disposition_violation rejects a Handover without one. It gives up through abort()
    // like every other stop path in this file.
    switch (protocol) {
    case ProtocolId::ISO15118_2:
        engine.emplace<d2::Engine>(callbacks, params, latches, dc_params, has_cp_state_feedback, resumed_session_id,
                                   welding_detection);
        break;
    case ProtocolId::DIN70121:
        engine.emplace<din::Engine>(callbacks, params, latches, dc_params, has_cp_state_feedback, resumed_session_id,
                                    welding_detection);
        break;
    case ProtocolId::ISO15118_20:
        abort("EV handover to ISO 15118-20 requested from the -20 engine; stopping the session");
        return;
    }
    const auto name = protocol_id_to_string(protocol);
    logf_info("EV switched to the %.*s engine", static_cast<int>(name.size()), name.data());

    with_engine(engine, [](auto& e) { e.start(); });
    update_ongoing_guard(true);
    if (with_engine(engine, [](auto& e) { return e.has_request(); })) {
        arm_send_delay();
    }
}

void Session::set_transport_security(v2gtp::Security security) {
    params.secure_transport = (security == v2gtp::Security::TLS);
}

void Session::set_offer(std::vector<OfferedProtocol> offer) {
    std::get<d20::Engine>(engine).set_offer(std::move(offer));
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
            // Unbounded, a state waiting on CP or on an Ongoing SECC would never end.
            abort("EV failed to arm the ongoing guard; stopping the session");
        }
    }
}

void Session::handle_complete_frame() {
    guarded("V2G response handling", [this]() {
        bool accepted = false;
        const auto handover = with_engine(engine, [&](auto& e) -> std::optional<ProtocolId> {
            if (not e.stage_response(packet.get_payload_type(),
                                     io::StreamInputView{packet.get_payload_buffer(), packet.get_payload_length()})) {
                return std::nullopt;
            }
            accepted = true;
            if (not watchdog_timer.disarm()) {
                logf_warning("EV failed to disarm the response watchdog after a response arrived");
            }
            feedback.v2g_message(e.peek_response_type());
            if (e.started()) {
                return feed_fsm(d20::Event::V2GTP_MESSAGE);
            }
            return std::nullopt;
        });
        // An ignored message leaves every timer as it was, the send delay of a held request included.
        if (not accepted) {
            return;
        }
        if (handover.has_value()) {
            switch_engine(*handover);
        }
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
        abort("EV failed to arm the send-delay timer; stopping the session");
    }
}

void Session::transmit_pending() {
    const bool has_request = with_engine(engine, [](auto& e) { return e.has_request(); });
    if (not has_request) {
        return;
    }

    const auto taken = with_engine(engine, [](auto& e) { return e.take_request(); });
    if (not taken.has_value()) {
        abort("EV request encoding failed; stopping the session");
        return;
    }

    const auto& [payload, payload_type] = taken.value();

    std::vector<uint8_t> frame(io::SdpPacket::V2GTP_HEADER_SIZE + payload.size());
    V2GTP20_WriteHeader(frame.data(), static_cast<uint32_t>(payload.size()), static_cast<uint16_t>(payload_type));
    std::copy(payload.begin(), payload.end(), frame.begin() + io::SdpPacket::V2GTP_HEADER_SIZE);

    if (not outbound_send(std::move(frame))) {
        abort("EV failed to send the request frame; stopping the session");
        return;
    }

    // No response is expected for a final message flushed after the stop.
    const bool stopped = with_engine(engine, [](auto& e) { return e.is_stopped(); });
    if (not stopped) {
        if (not watchdog_timer.set_timeout(effective_response_timeout())) {
            abort("EV failed to arm the response watchdog; stopping the session");
        }
    }
}

std::chrono::milliseconds Session::effective_response_timeout() const {
    return (timing.response_timeout.count() > 0)
               ? timing.response_timeout
               : with_engine(engine, [](const auto& e) { return e.response_timeout(); });
}

bool Session::in_welding_detection() const {
    return with_engine(engine, [](const auto& e) { return e.is_welding_detection(); });
}

void Session::on_send_delay_expired() {
    guarded("send-delay expiry", [this]() { transmit_pending(); });
}

void Session::on_watchdog_expired() {
    if (in_welding_detection()) {
        guarded("welding response timeout", [this]() { feed_fsm(d20::Event::RESPONSE_TIMEOUT); });
    }
    guarded("timed-out feedback", [this]() { feedback.timed_out(); });
    abort("EV response watchdog expired; stopping the session");
}

void Session::on_ongoing_expired() {
    ongoing_armed = false;
    if (in_welding_detection()) {
        guarded("welding detection timeout", [this]() {
            feed_fsm(d20::Event::WELDING_TIMEOUT);
            if (with_engine(engine, [](const auto& e) { return e.has_request(); })) {
                arm_send_delay();
            }
        });
        return;
    }
    const auto state = with_engine(engine, [](auto& e) {
        const auto current = e.current_state();
        return current.has_value() ? static_cast<int>(*current) : -1;
    });
    guarded("timed-out feedback", [this]() { feedback.timed_out(); });
    abort("EV ongoing guard expired in state " + std::to_string(state) + "; stopping the session");
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
    // switch_engine emplaces, so a throwing engine constructor leaves no engine at all. There is
    // nothing left to run the session, and std::visit on a valueless variant would throw here,
    // inside the path that is meant to wind the session down.
    if (engine.valueless_by_exception()) {
        return true;
    }
    return std::visit(
        [&](const auto& e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, std::monostate>) {
                return true;
            } else {
                return e.is_stopped() and not e.has_request();
            }
        },
        engine);
}

bool Session::is_paused() const {
    if (engine.valueless_by_exception()) {
        return false;
    }
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
    if (engine.valueless_by_exception()) {
        return std::nullopt;
    }
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
    if (engine.valueless_by_exception()) {
        return std::nullopt;
    }
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

d20::SeccClock::State Session::secc_clock_state() const {
    if (const auto* e = std::get_if<d20::Engine>(&engine)) {
        return e->context().secc_clock().state();
    }
    return {};
}

std::optional<d20::SelectedService> Session::service_selection() const {
    if (const auto* e = std::get_if<d20::Engine>(&engine)) {
        return e->context().service_selection();
    }
    return std::nullopt;
}

} // namespace iso15118::ev::session
