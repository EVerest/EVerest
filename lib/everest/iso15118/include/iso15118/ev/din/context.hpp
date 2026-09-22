// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/config/config.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include <everest/util/async/monitor.hpp>

#include <iso15118/message_din/common_types.hpp>
#include <iso15118/message_din/response_variant.hpp>
#include <iso15118/message_din/type.hpp>

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/din/evse_info.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/ev/session/message_exchange.hpp>

namespace iso15118::ev::din {

using d20::ControlEvent;
using d20::CpState;
using d20::PauseCharging;
using d20::StopCharging;

struct Codec {
    using Variant = message_din::ResponseVariant;
    using Type = message_din::Type;
    template <typename Msg> static size_t serialize(const Msg& msg, const io::StreamOutputView& view) {
        return message_din::serialize(msg, view);
    }
    template <typename Msg> static constexpr Type type_of() {
        return message_din::TypeTrait<Msg>::type;
    }
    // Every DIN SPEC 70121 message travels as V2GTP payload type 0x8001.
    template <typename Msg> static constexpr v2gtp::PayloadType payload_type_of() {
        return v2gtp::PayloadType::SAP;
    }
};

using MessageExchange = ev::session::MessageExchange<Codec>;

// DIN SPEC 70121 EV session context, the DIN counterpart of ev::d20::Context. DIN has no pause on
// the wire; a pause is a SessionStop whose session id is re-joined later.
class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, params::EvSessionParams params_,
            session::Latches& latches_, everest::lib::util::monitor<params::DcChargeParams>& dc_params_,
            bool has_cp_state_feedback_, std::optional<message_din::datatypes::SessionId> resumed_session_id_,
            config::WeldingDetectionConfig welding_detection_);
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    template <typename MessageType> void send_request(MessageType msg) {
        msg.header.session_id = session_id;
        message_exchange.set_request(msg);
    }

    std::unique_ptr<message_din::ResponseVariant> pull_response() {
        return message_exchange.pull_response();
    }

    bool has_request() const {
        return message_exchange.has_request();
    }

    void discard_request() {
        message_exchange.discard();
    }

    void stop_session() {
        session_stopped = true;
    }
    bool is_session_stopped() const {
        return session_stopped;
    }
    void pause_session() {
        session_stopped = true;
        session_paused = true;
    }
    bool is_session_paused() const {
        return session_paused;
    }

    void set_stop_charging_requested(bool requested) {
        latches.stop = requested;
    }
    bool is_stop_charging_requested() const {
        return latches.stop;
    }
    void set_pause_charging_requested(bool requested) {
        latches.pause = requested;
    }
    bool is_pause_charging_requested() const {
        return latches.pause;
    }

    bool has_cp_state_feedback() const {
        return has_cp_state_feedback_;
    }
    bool cp_state_c_or_d() const {
        return latches.cp_c_or_d;
    }
    void set_cp_state(bool c_or_d) {
        latches.cp_c_or_d = c_or_d;
    }

    const message_din::datatypes::SessionId& get_session_id() const {
        return session_id;
    }
    void set_session_id(const message_din::datatypes::SessionId& id) {
        session_id = id;
    }
    const std::optional<message_din::datatypes::SessionId>& resumed_session_id() const {
        return resumed_session_id_;
    }

    const params::EvSessionParams& params() const {
        return params_;
    }

    params::DcChargeParams get_dc_params() const {
        auto h = dc_params.handle();
        return *h;
    }

    const config::WeldingDetectionConfig welding_detection;
    const iso15118::ev::Feedback feedback;
    EvseInfo evse_info;

private:
    MessageExchange& message_exchange;
    params::EvSessionParams params_;
    // Session-owned, so a handover keeps them.
    session::Latches& latches;
    everest::lib::util::monitor<params::DcChargeParams>& dc_params;
    bool has_cp_state_feedback_{false};
    std::optional<message_din::datatypes::SessionId> resumed_session_id_;

    message_din::datatypes::SessionId session_id{};
    bool session_stopped{false};
    bool session_paused{false};
};

} // namespace iso15118::ev::din
