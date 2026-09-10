// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <everest/util/async/monitor.hpp>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/variant.hpp>

#include <iso15118/ev/d2/evse_info.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/message_exchange.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/ev/session_params.hpp>

namespace iso15118::ev::d2 {

using d20::ControlEvent;
using d20::CpState;
using d20::PauseCharging;
using d20::StopCharging;

struct Codec {
    using Variant = message_2::Variant;
    using Type = message_2::Type;
    template <typename Msg> static size_t serialize(const Msg& msg, const io::StreamOutputView& view) {
        return message_2::serialize(msg, view);
    }
    template <typename Msg> static constexpr Type type_of() {
        return message_2::TypeTrait<Msg>::type;
    }
};

using MessageExchange = ev::MessageExchange<Codec>;

// Plug & Charge runtime state.
struct PnCState {
    bool contract_selected{false};
    message_2::datatypes::GenChallenge gen_challenge{};
    // Contract in use: preloaded or obtained by CertificateInstallation.
    std::vector<uint8_t> contract_cert_der;
    std::vector<std::vector<uint8_t>> contract_sub_certs_der;
    std::string contract_key_pem;
    std::optional<std::string> contract_key_password;
    std::string emaid;
};

// ISO 15118-2 EV session context, the -2 counterpart of ev::d20::Context.
class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, EvSessionParams params_,
            const std::optional<ControlEvent>& current_control_event_,
            everest::lib::util::monitor<DcChargeParams>& dc_params_, bool has_cp_state_feedback_,
            std::optional<message_2::datatypes::SessionId> resumed_session_id_);
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    template <typename MessageType> void send_request(MessageType msg) {
        msg.header.session_id = session_id;
        message_exchange.set_request(msg);
    }

    // Signed EXI the EV encoded itself (AuthorizationReq, MeteringReceiptReq, CertificateInstallationReq).
    void send_raw(std::vector<uint8_t> exi, message_2::Type type) {
        message_exchange.set_raw_request(std::move(exi), type);
    }

    std::unique_ptr<message_2::Variant> pull_response() {
        return message_exchange.pull_response();
    }

    template <typename T> T const* get_control_event() {
        if (not current_control_event.has_value() or not std::holds_alternative<T>(*current_control_event)) {
            return nullptr;
        }
        return &std::get<T>(*current_control_event);
    }

    void stop_session() {
        session_stopped = true;
    }
    bool is_session_stopped() const {
        return session_stopped;
    }
    // SessionStop(Pause) acknowledged.
    void pause_session() {
        session_stopped = true;
        session_paused = true;
    }
    bool is_session_paused() const {
        return session_paused;
    }

    void set_stop_charging_requested(bool requested) {
        stop_charging_requested = requested;
    }
    bool is_stop_charging_requested() const {
        return stop_charging_requested;
    }
    void set_pause_charging_requested(bool requested) {
        pause_charging_requested = requested;
    }
    bool is_pause_charging_requested() const {
        return pause_charging_requested;
    }
    message_2::datatypes::ChargingSession requested_stop_reason() const {
        return (pause_charging_requested and not stop_charging_requested)
                   ? message_2::datatypes::ChargingSession::Pause
                   : message_2::datatypes::ChargingSession::Terminate;
    }

    bool has_cp_state_feedback() const {
        return has_cp_state_feedback_;
    }
    bool cp_state_c_or_d() const {
        return cp_state_c_or_d_;
    }
    void set_cp_state(bool c_or_d) {
        cp_state_c_or_d_ = c_or_d;
    }

    const message_2::datatypes::SessionId& get_session_id() const {
        return session_id;
    }
    void set_session_id(const message_2::datatypes::SessionId& id) {
        session_id = id;
    }
    const std::optional<message_2::datatypes::SessionId>& resumed_session_id() const {
        return resumed_session_id_;
    }

    const EvSessionParams& params() const {
        return params_;
    }

    // Locked-copy snapshot of the DC limits/targets shared with the module.
    DcChargeParams get_dc_params() const {
        auto h = dc_params.handle();
        return *h;
    }

    const iso15118::ev::Feedback feedback;
    EvseInfo evse_info;
    PnCState pnc;

private:
    MessageExchange& message_exchange;
    EvSessionParams params_;
    const std::optional<ControlEvent>& current_control_event;
    everest::lib::util::monitor<DcChargeParams>& dc_params;
    bool has_cp_state_feedback_{false};
    std::optional<message_2::datatypes::SessionId> resumed_session_id_;

    message_2::datatypes::SessionId session_id{};
    bool session_stopped{false};
    bool session_paused{false};
    bool stop_charging_requested{false};
    bool pause_charging_requested{false};
    bool cp_state_c_or_d_{false};
};

} // namespace iso15118::ev::d2
