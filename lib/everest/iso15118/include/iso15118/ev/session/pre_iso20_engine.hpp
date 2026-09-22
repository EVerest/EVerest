// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <everest/util/async/monitor.hpp>
#include <everest/util/fsm/fsm.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/v2g_message_type.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/v2gtp/sdp.hpp>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/ev/session/fsm.hpp>
#include <iso15118/ev/session/latches.hpp>

namespace iso15118::ev::session {

/**
 * The engine of a pre-20 protocol generation.
 *
 * ISO 15118-2 and DIN SPEC 70121 differ in their message set, their state graph, their timeout
 * table and their name. They do not differ in anything else an engine does: both carry every
 * message as V2GTP payload type 0x8001, latch the same control events, tear down the same way and
 * check the same disposition. `Traits` names what differs and this class is the rest.
 *
 */
template <typename Traits> class PreIso20Engine {
public:
    using Context = typename Traits::Context;
    using StateBase = typename Traits::StateBase;
    using StateID = typename Traits::StateID;
    using MessageExchange = typename Traits::MessageExchange;
    using Timeouts = typename Traits::Timeouts;

    PreIso20Engine(feedback::Callbacks callbacks, params::EvSessionParams params, Latches& latches,
                   everest::lib::util::monitor<params::DcChargeParams>& dc_params, bool has_cp_state_feedback,
                   std::optional<std::array<uint8_t, 8>> resumed_session_id,
                   config::WeldingDetectionConfig welding_detection) :
        ctx(std::move(callbacks), message_exchange, std::move(params), latches, dc_params, has_cp_state_feedback,
            resumed_session_id, welding_detection) {
    }

    PreIso20Engine(const PreIso20Engine&) = delete;
    PreIso20Engine& operator=(const PreIso20Engine&) = delete;

    void start() {
        Traits::send_first_request(ctx);
        fsm.emplace(ctx.template create_state<typename Traits::InitialState>());
    }

    bool started() const {
        return fsm.has_value();
    }

    // False when the frame is not a pre-20 payload (type 0x8001) and was dropped [V2G2-086].
    bool stage_response(v2gtp::PayloadType payload_type, const io::StreamInputView& view) {
        if (payload_type != v2gtp::PayloadType::SAP) {
            const auto name = Traits::name();
            logf_warning("Ignoring V2GTP payload type 0x%04x on an %.*s session", static_cast<unsigned>(payload_type),
                         static_cast<int>(name.size()), name.data());
            return false;
        }
        auto response = std::make_unique<typename Traits::Variant>(view);
        // Also false for a message that is not a valid response [V2G2-440]. A strict session keeps
        // stopping in the state that cannot use it.
        if constexpr (not Traits::STRICT_FRAMING) {
            if (not message_exchange.answers_request_on_wire(*response)) {
                const auto name = Traits::name();
                logf_warning("Ignoring an %.*s message that does not answer the pending request (message type id %d)",
                             static_cast<int>(name.size()), name.data(), static_cast<int>(response->get_type()));
                return false;
            }
        }
        message_exchange.set_response(std::move(response));
        return true;
    }

    V2gMessageType peek_response_type() const {
        return message_exchange.peek_response_type();
    }

    FeedOutcome<StateID> feed(Event ev) {
        FeedOutcome<StateID> outcome;
        if (not fsm.has_value()) {
            return outcome;
        }
        outcome.state_before = fsm->get_current_state_id();
        const auto fed = fsm->feed(ev);
        outcome.output = fed.output;
        outcome.transitioned = fed.transitioned();
        outcome.violation =
            disposition_violation(fed.output, ev == Event::V2GTP_MESSAGE, message_exchange.has_request(),
                                  ctx.is_session_stopped(), outcome.transitioned, false, ongoing_timeout().has_value());
        return outcome;
    }

    bool has_request() const {
        return message_exchange.has_request();
    }

    std::optional<std::pair<std::vector<uint8_t>, v2gtp::PayloadType>> take_request() {
        return message_exchange.take_request();
    }

    // Drops the pending request without encoding it: every caller is tearing the session down and
    // none of them wants the bytes, and encoding here would report a failure from a path that is
    // deliberately throwing the message away.
    void discard_request() {
        message_exchange.discard();
    }

    std::chrono::milliseconds response_timeout() const {
        return fsm.has_value() ? Timeouts::response_timeout(fsm->get_current_state_id()) : Timeouts::MESSAGE;
    }

    bool is_welding_detection() const {
        return current_state() == StateID::WeldingDetection;
    }

    std::optional<std::chrono::milliseconds> ongoing_timeout() const {
        if (is_welding_detection()) {
            return ctx.welding_detection.timeout;
        }
        if (not fsm.has_value()) {
            return std::nullopt;
        }
        return Timeouts::ongoing_timeout(fsm->get_current_state_id());
    }

    std::chrono::milliseconds min_request_interval() const {
        return Timeouts::MIN_REQUEST_INTERVAL;
    }

    std::optional<StateID> current_state() const {
        if (not fsm.has_value()) {
            return std::nullopt;
        }
        return fsm->get_current_state_id();
    }

    // Session-facing, engine-neutral surface.
    bool strict_framing() const {
        return Traits::STRICT_FRAMING;
    }

    void stop() {
        ctx.stop_session();
    }

    bool is_stopped() const {
        return ctx.is_session_stopped();
    }

    bool is_paused() const {
        return ctx.is_session_paused();
    }

    std::optional<std::array<uint8_t, 8>> session_id() const {
        return ctx.get_session_id();
    }

    std::optional<ProtocolId> negotiated_protocol() const {
        return Traits::PROTOCOL;
    }

    ProtocolId protocol() const {
        return Traits::PROTOCOL;
    }

    Context& context() {
        return ctx;
    }
    const Context& context() const {
        return ctx;
    }

private:
    MessageExchange message_exchange;
    Context ctx;
    std::optional<fsm::v2::FSM<StateBase>> fsm;
};

} // namespace iso15118::ev::session
