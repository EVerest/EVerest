// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/config/config.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <everest/util/async/monitor.hpp>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/response_variant.hpp>
#include <iso15118/message_2/type.hpp>

#include <iso15118/ev/d2/evse_info.hpp>
#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/ev/session/message_exchange.hpp>

namespace iso15118::ev::d2 {

using d20::ControlEvent;
using d20::CpState;
using d20::PauseCharging;
using d20::StopCharging;

struct Codec {
    using Variant = message_2::ResponseVariant;
    using Type = message_2::Type;
    template <typename Msg> static size_t serialize(const Msg& msg, const io::StreamOutputView& view) {
        return message_2::serialize(msg, view);
    }
    template <typename Msg> static constexpr Type type_of() {
        return message_2::TypeTrait<Msg>::type;
    }
    // Every ISO 15118-2 message travels as V2GTP payload type 0x8001.
    template <typename Msg> static constexpr v2gtp::PayloadType payload_type_of() {
        return v2gtp::PayloadType::SAP;
    }
};

using MessageExchange = ev::session::MessageExchange<Codec>;

// Plug & Charge runtime state.
struct PnCState {
    bool contract_selected{false};
    message_2::datatypes::GenChallenge gen_challenge{};
    // Contract in use: preloaded or obtained by CertificateInstallation.
    std::optional<ContractMaterial> contract;
};

// ISO 15118-2 EV session context, the -2 counterpart of ev::d20::Context.
class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, params::EvSessionParams params_,
            session::Latches& latches_, everest::lib::util::monitor<params::DcChargeParams>& dc_params_,
            bool has_cp_state_feedback_, std::optional<message_2::datatypes::SessionId> resumed_session_id_,
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

    // Signed EXI the EV encoded itself (AuthorizationReq, MeteringReceiptReq, CertificateInstallationReq).
    void send_raw(std::vector<uint8_t> exi, message_2::Type type) {
        message_exchange.set_raw_request(std::move(exi), type, v2gtp::PayloadType::SAP);
    }

    std::unique_ptr<message_2::ResponseVariant> pull_response() {
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
    // SessionStop(Pause) acknowledged.
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

    const message_2::datatypes::SessionId& get_session_id() const {
        return session_id;
    }
    void set_session_id(const message_2::datatypes::SessionId& id) {
        session_id = id;
    }
    const std::optional<message_2::datatypes::SessionId>& resumed_session_id() const {
        return resumed_session_id_;
    }

    const params::EvSessionParams& params() const {
        return params_;
    }

    // Locked-copy snapshot of the DC limits/targets shared with the module.
    params::DcChargeParams get_dc_params() const {
        auto h = dc_params.handle();
        return *h;
    }

    // [V2G2-286]: the SAScheduleTupleID ChargeParameterDiscovery selected, which PowerDelivery and
    // MeteringReceipt name.
    uint8_t sa_schedule_tuple_id() const {
        return sa_schedule_tuple_id_;
    }
    void set_sa_schedule_tuple_id(uint8_t tuple_id) {
        sa_schedule_tuple_id_ = tuple_id;
    }

    const config::WeldingDetectionConfig welding_detection;
    const iso15118::ev::Feedback feedback;
    EvseInfo evse_info;
    PnCState pnc;

private:
    MessageExchange& message_exchange;
    params::EvSessionParams params_;
    // Session-owned, so a handover keeps them.
    session::Latches& latches;
    everest::lib::util::monitor<params::DcChargeParams>& dc_params;
    bool has_cp_state_feedback_{false};
    std::optional<message_2::datatypes::SessionId> resumed_session_id_;

    message_2::datatypes::SessionId session_id{};
    bool session_stopped{false};
    bool session_paused{false};
    uint8_t sa_schedule_tuple_id_{0};
};

} // namespace iso15118::ev::d2
