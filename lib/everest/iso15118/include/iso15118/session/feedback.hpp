// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <variant>

#include <iso15118/d20/ev_information.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/shared_datatypes.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace iso15118::session {

namespace dt = message_20::datatypes;

namespace feedback {

enum class Signal {
    REQUIRE_AUTH_EIM,
    START_CABLE_CHECK,
    SETUP_FINISHED,
    PRE_CHARGE_STARTED,
    CHARGE_LOOP_STARTED,
    CHARGE_LOOP_FINISHED,
    DC_OPEN_CONTACTOR,
    AC_CLOSE_CONTACTOR,
    AC_OPEN_CONTACTOR,
    DLINK_TERMINATE,
    DLINK_ERROR,
    DLINK_PAUSE,
};

struct DcMaximumLimits {
    float voltage{NAN};
    float current{NAN};
    // Optional, unlike voltage and current: DIN SPEC 70121 and ISO 15118-2 make EVMaximumPowerLimit an
    // optional field of DC_EVChargeParameter (and of CurrentDemandReq), and ISO 15118-20 always sends it.
    // An EV that omits it leaves this unset instead of having a voltage * current product invented for
    // it -- EvseV2G passes the EVMaximumPowerLimit_isUsed flag straight through (iso_server.cpp:510,
    // din_server.cpp:300), and the dc_ev_maximum_limits var declares all three fields optional.
    std::optional<float> power{std::nullopt};
};

// The EV's DC_EVStatus as carried by every DC request of DIN SPEC 70121 and ISO 15118-2 (both message
// layers use shared_datatypes::DcEvErrorCode). ISO 15118-20 has no counterpart -- it reports the state
// of charge in DisplayParameters -- so this feedback exists only for the pre-20 protocols.
struct DcEvStatus {
    bool ready{false};
    shared_datatypes::DcEvErrorCode error_code{shared_datatypes::DcEvErrorCode::NO_ERROR};
    int8_t ress_soc{0};
    // DIN SPEC 70121 only; ISO 15118-2 does not carry these.
    std::optional<bool> cabin_conditioning;
    std::optional<bool> ress_conditioning;
};

// The EV's DC_EVChargeParameter from a DIN SPEC 70121 / ISO 15118-2 ChargeParameterDiscoveryReq.
struct DcEvChargeParameters {
    float max_current{0.0f};
    float max_voltage{0.0f};
    std::optional<float> max_power;
    std::optional<float> energy_capacity; // Wh
    std::optional<float> energy_request;  // Wh
    std::optional<int8_t> full_soc;       // %
    std::optional<int8_t> bulk_soc;       // %
    // DC_EVStatus.EVRESSSOC of the same request, so a consumer building an OCPP ChargingNeeds does not
    // have to correlate it with the separate dc_ev_status feedback.
    int8_t ress_soc{0};
};

// The EV's AC_EVChargeParameter from an ISO 15118-2 ChargeParameterDiscoveryReq. DIN SPEC 70121 is DC
// only and never carries this.
struct AcEvChargeParameters {
    float e_amount{0.0f}; // Wh
    float max_voltage{0.0f};
    float max_current{0.0f};
    float min_current{0.0f};
};

// What the EV asked for in a DIN SPEC 70121 / ISO 15118-2 ChargeParameterDiscoveryReq: the requested
// energy transfer mode plus whichever of the two EVChargeParameter variants it sent. ISO 15118-20
// reports the equivalent through notify_ev_charging_needs, whose -20 datatypes have no pre-20
// counterpart, hence this separate feedback.
struct EvChargeParameters {
    shared_datatypes::EnergyTransferMode requested_energy_transfer{shared_datatypes::EnergyTransferMode::DC_extended};
    std::optional<DcEvChargeParameters> dc;
    std::optional<AcEvChargeParameters> ac;
    // Seconds from now until the EV intends to leave. ISO 15118-2 only: DIN SPEC 70121 has no
    // DepartureTime element.
    std::optional<uint32_t> departure_time;
};

// EV-reported charge progress. The remaining times come from a DIN SPEC 70121 / ISO 15118-2
// CurrentDemandReq; the two completion flags come from that request and from PowerDeliveryReq's
// DC_EVPowerDeliveryParameter, which carries no remaining times -- there they stay absent rather than
// being reported as zero. Emitted on change only, since the EV repeats the values in every charge-loop
// request.
struct DcEvChargeProgress {
    std::optional<float> remaining_time_to_full_soc; // s
    std::optional<float> remaining_time_to_bulk_soc; // s
    bool charging_complete{false};
    std::optional<bool> bulk_charging_complete;
};

using PresentVoltage = dt::RationalNumber;
using MeterInfoRequested = bool;
using DcReqControlMode = std::variant<dt::Scheduled_DC_CLReqControlMode, dt::BPT_Scheduled_DC_CLReqControlMode,
                                      dt::Dynamic_DC_CLReqControlMode, dt::BPT_Dynamic_DC_CLReqControlMode>;

using AcReqControlMode = std::variant<dt::Scheduled_AC_CLReqControlMode, dt::BPT_Scheduled_AC_CLReqControlMode,
                                      dt::DER_Scheduled_AC_CLReqControlMode, dt::Dynamic_AC_CLReqControlMode,
                                      dt::BPT_Dynamic_AC_CLReqControlMode, dt::DER_Dynamic_AC_CLReqControlMode>;

using DcChargeLoopReq = std::variant<DcReqControlMode, dt::DisplayParameters, PresentVoltage, MeterInfoRequested>;

using EvseTransferLimits = std::variant<d20::DcTransferLimits, d20::AcTransferLimits>;

using EvTransferLimits =
    std::variant<dt::DC_CPDReqEnergyTransferMode, dt::BPT_DC_CPDReqEnergyTransferMode, dt::AC_CPDReqEnergyTransferMode,
                 dt::BPT_AC_CPDReqEnergyTransferMode, dt::DER_AC_CPDReqEnergyTransferMode>;
using EvSEControlMode = std::variant<dt::Dynamic_SEReqControlMode, dt::Scheduled_SEReqControlMode>;

using AcChargeLoopReq = std::variant<AcReqControlMode, dt::DisplayParameters, MeterInfoRequested>;
using AcLimits = std::variant<dt::AC_CPDReqEnergyTransferMode, dt::BPT_AC_CPDReqEnergyTransferMode,
                              dt::DER_AC_CPDReqEnergyTransferMode>;

// Which PnC certificate exchange the SECC is relaying to the backend (ISO 15118-2).
enum class CertificateExchangeAction {
    Install,
    Update,
};

// How the V2G session ended on the wire, reported right after the session-ending response was
// written to the socket. Terminate/Pause mirror the ChargingSession of a positive SessionStopRes --
// the anchor for the CP-oscillator retain time (DIN 70121 [V2G-DC-968]); DIN has no ChargingSession
// parameter and always maps to Terminate. FailedTermination means the SECC ended the session with a
// FAILED_* response (sequence error, unknown session): the oscillator must go off without delay
// ([V2G-DC-942]) and the SECC closes the TCP connection itself ([V2G-DC-940], no linger).
enum class SessionStopAction {
    Terminate,
    Pause,
    FailedTermination,
};

struct Callbacks {
    std::function<void(Signal)> signal;
    std::function<void(float)> dc_pre_charge_target_voltage;
    std::function<void(const DcChargeLoopReq&)> dc_charge_loop_req;
    std::function<void(const DcMaximumLimits&)> dc_max_limits;

    // DIN SPEC 70121 / ISO 15118-2: the EV's DC_EVStatus (ready flag, error code, RESS state of charge).
    // Emitted on change only, since the EV repeats it in every DC request (EvseV2G publish_DIN_DcEvStatus
    // / publish_iso_DcEvStatus parity).
    std::function<void(const DcEvStatus&)> dc_ev_status;

    // DIN SPEC 70121 / ISO 15118-2: what the EV asked for in ChargeParameterDiscoveryReq. Carries the
    // per-session EV facts the module surfaces as ev_info (battery capacity, energy request, full/bulk
    // SoC, departure time, the AC limits) and as the OCPP ChargingNeeds notification.
    std::function<void(const EvChargeParameters&)> ev_charge_parameters;

    // DIN SPEC 70121 / ISO 15118-2: the EV's charge progress from the charge loop and from
    // PowerDeliveryReq. Emitted on change only.
    std::function<void(const DcEvChargeProgress&)> dc_ev_charge_progress;
    std::function<void(const AcChargeLoopReq&)> ac_charge_loop_req;
    // Every V2G message the session handled, request and response alike, together with the complete
    // V2GTP frame it travelled in (8-byte header + EXI payload) exactly as it went over the wire --
    // what EvseV2G publishes as v2g_messages.exi / .exi_base64 (v2g_server.cpp:271). The frame view is
    // only valid for the duration of the call; it is empty when the bytes are not available.
    std::function<void(const V2gMessageType&, const io::StreamInputView& exi_frame)> v2g_message;
    // The protocol list the EV offered in SupportedAppProtocolReq, reported before the SECC picks one
    // (so a failed negotiation is reported too, EvseV2G v2g_server.cpp:467 parity).
    std::function<void(const message_20::SupportedAppProtocolRequest&)> ev_app_protocols;
    std::function<void(const std::string&)> evccid;
    std::function<void(const std::string&)> selected_protocol;

    std::function<void(const dt::ServiceCategory&, const std::optional<dt::AcConnector>&, const dt::ControlMode&,
                       const dt::MobilityNeedsMode&, const EvseTransferLimits&, const EvTransferLimits&,
                       const EvSEControlMode&, const std::vector<message_20::datatypes::ServiceCategory>&)>
        notify_ev_charging_needs;
    std::function<void(const d20::SelectedServiceParameters&)> selected_service_parameters;
    std::function<void(const d20::EVInformation&)> ev_information;
    std::function<std::optional<dt::ServiceParameterList>(uint16_t)> get_vas_parameters;
    std::function<void(const dt::VasSelectedServiceList&)> selected_vas_services;
    std::function<void(const AcLimits&)> ac_limits;
    std::function<void(const std::string&, const std::string&)> ev_termination;

    // A positive SessionStopRes was written to the socket (all protocols). Anchors the CP-oscillator
    // retain time [V2G-DC-968]; does NOT imply link teardown (DLINK_* signals still follow later).
    std::function<void(SessionStopAction)> session_stop_res_sent;

    // DIN SPEC 70121 / ISO 15118-2: the payment option the EV selected and the SECC accepted
    // (ServicePaymentSelectionReq / PaymentServiceSelectionReq). ISO 15118-20 has no counterpart -- it
    // negotiates authorization services instead.
    std::function<void(shared_datatypes::PaymentOption)> selected_payment_option;

    // ISO 15118-2 Plug-and-Charge: the SECC verified a signed AuthorizationReq and requests PnC
    // authorization for the given eMAID (and PEM contract certificate chain) from the higher layer.
    std::function<void(const std::string& emaid, const std::string& contract_chain_pem)> require_auth_pnc;

    // ISO 15118-2 Plug-and-Charge certificate relay: the SECC received a CertificateInstallationReq or
    // CertificateUpdateReq and forwards the raw request EXI (base64) plus which action it is to the higher
    // layer (CSMS/CPS backend). The response is injected back asynchronously via a CertificateResponse
    // control event.
    std::function<void(const std::string& exi_request_base64, CertificateExchangeAction action)> certificate_request;
};

} // namespace feedback

class Feedback {
public:
    Feedback(feedback::Callbacks);

    void signal(feedback::Signal) const;
    void dc_pre_charge_target_voltage(float) const;
    void dc_charge_loop_req(const feedback::DcChargeLoopReq&) const;
    void dc_max_limits(const feedback::DcMaximumLimits&) const;
    void dc_ev_status(const feedback::DcEvStatus&) const;
    void ev_charge_parameters(const feedback::EvChargeParameters&) const;
    void dc_ev_charge_progress(const feedback::DcEvChargeProgress&) const;
    void ac_charge_loop_req(const feedback::AcChargeLoopReq&) const;
    // \p exi_frame is the full V2GTP frame the message travelled in; the engines omit it (the Session
    // owns the wire bytes and attaches them on the way through, see Session's callback wrapping).
    void v2g_message(const V2gMessageType&, const io::StreamInputView& exi_frame = {}) const;
    void ev_app_protocols(const message_20::SupportedAppProtocolRequest&) const;
    void evcc_id(const std::string&) const;
    void selected_protocol(const std::string&) const;

    void notify_ev_charging_needs(const dt::ServiceCategory&, const std::optional<dt::AcConnector>&,
                                  const dt::ControlMode&, const dt::MobilityNeedsMode&,
                                  const feedback::EvseTransferLimits&, const feedback::EvTransferLimits&,
                                  const feedback::EvSEControlMode&,
                                  const std::vector<message_20::datatypes::ServiceCategory>&) const;
    void selected_service_parameters(const d20::SelectedServiceParameters&) const;
    void ev_information(const d20::EVInformation&) const;
    std::optional<dt::ServiceParameterList> get_vas_parameters(uint16_t) const;
    void selected_vas_services(const dt::VasSelectedServiceList&) const;
    void ac_limits(const feedback::AcLimits&) const;
    void ev_termination(const std::string&, const std::string&) const;
    void session_stop_res_sent(feedback::SessionStopAction) const;
    void selected_payment_option(shared_datatypes::PaymentOption) const;
    void require_auth_pnc(const std::string& emaid, const std::string& contract_chain_pem) const;
    void certificate_request(const std::string& exi_request_base64, feedback::CertificateExchangeAction action) const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::session
