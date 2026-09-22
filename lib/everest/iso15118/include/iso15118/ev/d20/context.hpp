// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/config/config.hpp>

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/response_variant.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>

#include <everest/util/async/monitor.hpp>

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/der/iec_functions.hpp>
#include <iso15118/ev/d20/der/sae_profile.hpp>
#include <iso15118/ev/d20/energy_request.hpp>
#include <iso15118/ev/d20/secc_clock.hpp>
#include <iso15118/ev/d20/selected_service.hpp>
#include <iso15118/ev/d20/service_family.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/energy_request.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/ev/session/message_exchange.hpp>
#include <iso15118/ev/session/sap_offer.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::ev::d20 {

// Codec seam for ev::session::MessageExchange: ISO 15118-20 splits its messages across V2GTP payload
// types, so the payload type comes from the message rather than from the generation.
struct Codec {
    using Variant = message_20::ResponseVariant;
    using Type = message_20::Type;
    template <typename Msg> static size_t serialize(const Msg& msg, const io::StreamOutputView& view) {
        return message_20::serialize(msg, view);
    }
    template <typename Msg> static constexpr Type type_of() {
        return message_20::TypeTrait<Msg>::type;
    }
    template <typename Msg> static constexpr v2gtp::PayloadType payload_type_of() {
        return message_20::PayloadTypeTrait<Msg>::type;
    }
};

using MessageExchange = ev::session::MessageExchange<Codec>;

// Every -20 message but SupportedAppProtocol carries a Header.
template <typename Msg, typename = void> struct HasHeader : std::false_type {};
template <typename Msg>
struct HasHeader<Msg, std::void_t<decltype(Msg::header)>> : std::is_same<decltype(Msg::header), message_20::Header> {};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// A paused session to re-join.
struct Resume {
    // SessionSetupReq carries it; OK_OldSessionJoined expected.
    std::array<uint8_t, SessionId::ID_LENGTH> session_id{};
    // The paused session's clock, so the resumed SessionSetupReq is already in SECC time.
    SeccClock::State secc_clock{};
    // The paused session's selection, restored when the SECC answers OK_OldSessionJoined.
    std::optional<SelectedService> selected_service{std::nullopt};
};

// Session-scoped options beyond the positional Context arguments.
struct SessionOptions {
    config::D20SessionConfig d20{};
    config::WeldingDetectionConfig welding_detection{};
    // The owner reports CpState events: DC_CableCheck holds its first request until state C/D.
    bool has_cp_state_feedback{false};
    std::optional<Resume> resume{std::nullopt};
    // schema_id -> protocol map of the SAP offer; empty = every offered entry is ISO 15118-20.
    std::vector<session::OfferedProtocol> offered_protocols{};
    params::EnergyRequest energy_request{};
};

class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_,
            message_20::datatypes::Identifier evcc_id_,
            std::vector<message_20::SupportedAppProtocol> advertised_app_protocols_, session::Latches& latches_,
            everest::lib::util::monitor<params::DcChargeParams>& dc_params_,
            everest::lib::util::monitor<params::AcChargeParams>& ac_params_,
            message_20::datatypes::ServiceCategory requested_service_, SessionOptions options_ = {});
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_20::ResponseVariant> pull_response();
    message_20::Type peek_response_type() const;

    // Stamps the header, if the message has one, with the session id and SECC time.
    template <typename MessageType> void send_request(MessageType msg) {
        if constexpr (HasHeader<MessageType>::value) {
            msg.header.session_id = session.get_id();
            msg.header.timestamp = secc_clock_.stamp();
        }
        message_exchange.set_request(msg);
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

    // EV-initiated stop latch. session::latch sets it from the Session's control events, and tests
    // set it directly; states only read it.
    // Latched before PowerDeliveryReq(Start), it goes straight to SessionStop (stop_before_start).
    // Read at a charge-loop response, it drives PowerDelivery(Stop) (leave_charge_loop).
    // Latched after the loop, it only raises a carried pause to a stop at SessionStop (raise_to_latched_stop).
    void set_stop_charging_requested(bool requested) {
        latches.stop = requested;
    }

    bool is_stop_charging_requested() const {
        return latches.stop;
    }

    // EV-initiated pause flag; same latch semantics as the stop flag, except that before
    // PowerDeliveryReq(Start) it terminates too (stop_before_start).
    void set_pause_charging_requested(bool requested) {
        latches.pause = requested;
    }

    bool is_pause_charging_requested() const {
        return latches.pause;
    }

    // SessionStop(Pause) acknowledged: stopped, and the session id can be re-joined.
    void pause_session() {
        session_stopped = true;
        session_paused = true;
    }

    bool is_session_paused() const {
        return session_paused;
    }

    const SessionOptions& options() const {
        return session_options;
    }

    message_20::datatypes::ControlMode preferred_control_mode() const {
        return session_options.d20.control_mode;
    }

    // Control mode of the parameter set ServiceDetail selected; the preferred mode until then.
    message_20::datatypes::ControlMode selected_control_mode() const {
        return selected_control_mode_.value_or(session_options.d20.control_mode);
    }

    void set_selected_control_mode(message_20::datatypes::ControlMode mode) {
        selected_control_mode_ = mode;
    }

    // [V2G20-1546]: the ScheduleTupleID ScheduleExchange picked, which every PowerDelivery names.
    std::optional<std::uint8_t> schedule_tuple_id() const {
        return schedule_tuple_id_;
    }

    void set_schedule_tuple_id(std::uint8_t tuple_id) {
        schedule_tuple_id_ = tuple_id;
    }

    bool has_cp_state_feedback() const {
        return session_options.has_cp_state_feedback;
    }

    // Last reported control pilot state (CpState event); false until reported.
    bool cp_state_c_or_d() const {
        return latches.cp_c_or_d;
    }

    void set_cp_state(bool c_or_d) {
        latches.cp_c_or_d = c_or_d;
    }

    // Set by SupportedAppProtocol from the negotiated schema_id.
    std::optional<ProtocolId> negotiated_protocol() const {
        return negotiated_protocol_;
    }

    void set_negotiated_protocol(ProtocolId protocol) {
        negotiated_protocol_ = protocol;
    }

    const message_20::datatypes::Identifier& get_evcc_id() const {
        return evcc_id;
    }

    SessionId& get_session() {
        return session;
    }

    const SessionId& get_session() const {
        return session;
    }

    // Synchronized by SessionSetup; stamps every request header in send_request.
    SeccClock& secc_clock() {
        return secc_clock_;
    }
    const SeccClock& secc_clock() const {
        return secc_clock_;
    }

    // Locked-copy snapshot of the EV DC charge params (module -> FSM channel).
    params::DcChargeParams get_dc_params() const {
        auto h = dc_params.handle();
        return *h;
    }

    params::AcChargeParams get_ac_params() const {
        auto h = ac_params.handle();
        return *h;
    }

    // start_charging's request as of the secc_clock's steady time.
    EnergyRequest energy_request() const;

    // Energy service requested at construction, which ServiceSelection sends, or the one a
    // re-joined session restored.
    message_20::datatypes::ServiceCategory selected_service() const {
        return selected_service_;
    }

    bool is_ac_family() const {
        return ev::d20::is_ac_family(selected_service_);
    }

    bool is_dc_family() const {
        return ev::d20::is_dc_family(selected_service_);
    }

    bool is_bpt() const {
        return ev::d20::is_bpt(selected_service_);
    }

    // Unset until an AC parameter set is chosen, and never set for DC.
    std::optional<message_20::datatypes::AcConnector> selected_ac_connector() const {
        return selected_ac_connector_;
    }

    void set_selected_ac_connector(message_20::datatypes::AcConnector connector) {
        selected_ac_connector_ = connector;
    }

    // Set once ServiceDetail has selected a parameter set.
    std::optional<SelectedService> service_selection() const {
        if (not selected_control_mode_.has_value()) {
            return std::nullopt;
        }
        return SelectedService{selected_service_, *selected_control_mode_, selected_ac_connector_};
    }

    // A re-joined session continues the selection its SECC retained.
    void restore_service_selection(const SelectedService& selection) {
        selected_service_ = selection.service;
        selected_control_mode_ = selection.control_mode;
        selected_ac_connector_ = selection.ac_connector;
    }

    // SinglePhase is the reading under which the base element is never a sum.
    message_20::datatypes::AcConnector ac_connector() const {
        return selected_ac_connector_.value_or(message_20::datatypes::AcConnector::SinglePhase);
    }

    std::bitset<d20::der::DER_CONTROL_FUNCTION_COUNT> der_supported_functions() const {
        return session_options.d20.der_control_functions.to_bitset();
    }

    bool der_stop_on_unsupported_functions() const {
        return session_options.d20.der_stop_on_unsupported_functions;
    }

    const d20::der::SaeInverterProfile& sae_profile() const {
        return session_options.d20.sae_profile;
    }

    // [V2G20-3409]: bits unused by AMD1 Table M.6 are dropped.
    std::uint32_t sae_supported_modes() const {
        return session_options.d20.sae_profile.supported_modes & sae::SAE_MODE_BITMAP_MASK;
    }

    std::uint16_t cpd_rounds() const {
        return session_options.d20.cpd_rounds;
    }

    bool der_stop_on_invalid_control() const {
        return session_options.d20.der_stop_on_invalid_control;
    }

    // Advertised SupportedAppProtocol list, set from the ctor (config-driven via
    // EvConfig). Read by the SupportedAppProtocol state. Only -20 is wired.
    const std::vector<message_20::SupportedAppProtocol>& get_advertised_app_protocols() const {
        return advertised_app_protocols;
    }

    void set_offer(std::vector<session::OfferedProtocol> offer) {
        advertised_app_protocols.clear();
        for (const auto& entry : offer) {
            advertised_app_protocols.push_back(entry.entry);
        }
        session_options.offered_protocols = std::move(offer);
    }

    const config::WeldingDetectionConfig welding_detection{};
    const iso15118::ev::Feedback feedback;

private:
    MessageExchange& message_exchange;

    message_20::datatypes::Identifier evcc_id;

    // Session-owned, so a handover keeps them.
    session::Latches& latches;

    // Module -> FSM DC-params channel. Non-const because acquiring the monitor lock
    // mutates its mutex; read access is a locked-copy snapshot.
    everest::lib::util::monitor<params::DcChargeParams>& dc_params;

    everest::lib::util::monitor<params::AcChargeParams>& ac_params;

    message_20::datatypes::ServiceCategory selected_service_;

    std::optional<message_20::datatypes::AcConnector> selected_ac_connector_{};

    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols;

    SessionId session{std::array<uint8_t, SessionId::ID_LENGTH>{}};
    SeccClock secc_clock_{};

    bool session_stopped{false};
    bool session_paused{false};

    SessionOptions session_options;
    std::optional<message_20::datatypes::ControlMode> selected_control_mode_{std::nullopt};
    std::optional<std::uint8_t> schedule_tuple_id_{std::nullopt};
    std::optional<ProtocolId> negotiated_protocol_{std::nullopt};
};

} // namespace iso15118::ev::d20
