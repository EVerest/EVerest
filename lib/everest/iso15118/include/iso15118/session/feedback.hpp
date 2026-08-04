// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
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
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
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
    // Optional, unlike voltage and current: an EV that omits EVMaximumPowerLimit leaves this unset
    // rather than having a voltage * current product invented for it.
    std::optional<float> power{std::nullopt};
};

// ISO 15118-20 has no counterpart -- it reports the state of charge in DisplayParameters.
struct DcEvStatus {
    bool ready{false};
    shared_datatypes::DcEvErrorCode error_code{shared_datatypes::DcEvErrorCode::NO_ERROR};
    int8_t ress_soc{0};
    // DIN SPEC 70121 only; ISO 15118-2 does not carry these.
    std::optional<bool> cabin_conditioning;
    std::optional<bool> ress_conditioning;
};

struct DcEvChargeParameters {
    float max_current{0.0f};
    float max_voltage{0.0f};
    std::optional<float> max_power;
    std::optional<float> energy_capacity; // Wh
    std::optional<float> energy_request;  // Wh
    std::optional<int8_t> full_soc;       // %
    std::optional<int8_t> bulk_soc;       // %
    // Repeated from the same request so a consumer building an OCPP ChargingNeeds need not correlate it.
    int8_t ress_soc{0};
};

struct AcEvChargeParameters {
    float e_amount{0.0f}; // Wh
    float max_voltage{0.0f};
    float max_current{0.0f};
    float min_current{0.0f};
};

// ISO 15118-20 reports the equivalent through notify_ev_charging_needs, whose datatypes have no
// pre-20 counterpart.
struct EvChargeParameters {
    shared_datatypes::EnergyTransferMode requested_energy_transfer{shared_datatypes::EnergyTransferMode::DC_extended};
    std::optional<DcEvChargeParameters> dc;
    std::optional<AcEvChargeParameters> ac;
    // ISO 15118-2 only: DIN SPEC 70121 has no DepartureTime element.
    std::optional<uint32_t> departure_time;
};

// PowerDeliveryReq carries the completion flags without the remaining times, which stay absent
// there rather than being reported as zero. Emitted on change only.
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

using AcReqControlMode =
    std::variant<dt::Scheduled_AC_CLReqControlMode, dt::BPT_Scheduled_AC_CLReqControlMode,
                 dt::DER_Scheduled_AC_CLReqControlMode, dt::Dynamic_AC_CLReqControlMode,
                 dt::BPT_Dynamic_AC_CLReqControlMode, dt::DER_Dynamic_AC_CLReqControlMode,
                 dt::sae::DER_Scheduled_AC_CLReqControlMode, dt::sae::DER_Dynamic_AC_CLReqControlMode>;

using DcChargeLoopReq = std::variant<DcReqControlMode, dt::DisplayParameters, PresentVoltage, MeterInfoRequested>;

using EvseTransferLimits = std::variant<d20::DcTransferLimits, d20::AcTransferLimits>;

using EvTransferLimits =
    std::variant<dt::DC_CPDReqEnergyTransferMode, dt::BPT_DC_CPDReqEnergyTransferMode, dt::AC_CPDReqEnergyTransferMode,
                 dt::BPT_AC_CPDReqEnergyTransferMode, dt::DER_AC_CPDReqEnergyTransferMode,
                 dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode>;
using EvSEControlMode = std::variant<dt::Dynamic_SEReqControlMode, dt::Scheduled_SEReqControlMode>;

using AcChargeLoopReq = std::variant<AcReqControlMode, dt::DisplayParameters, MeterInfoRequested>;
using AcLimits = std::variant<dt::AC_CPDReqEnergyTransferMode, dt::BPT_AC_CPDReqEnergyTransferMode,
                              dt::DER_AC_CPDReqEnergyTransferMode, dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode>;

enum class CertificateExchangeAction {
    Install,
    Update,
};

// Reported right after the session-ending response was written to the socket. Terminate/Pause
// anchor the CP-oscillator retain time ([V2G-DC-968]); DIN has no ChargingSession and always maps to
// Terminate. FailedTermination means the oscillator goes off without delay and the SECC closes the
// TCP connection itself ([V2G-DC-942]/[V2G-DC-940], no linger).
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

    // Emitted on change only, since the EV repeats it in every DC request.
    std::function<void(const DcEvStatus&)> dc_ev_status;

    // Surfaces as the module's ev_info and as the OCPP ChargingNeeds notification.
    std::function<void(const EvChargeParameters&)> ev_charge_parameters;

    std::function<void(const DcEvChargeProgress&)> dc_ev_charge_progress;
    std::function<void(const AcChargeLoopReq&)> ac_charge_loop_req;
    // Request and response alike, with the complete V2GTP frame exactly as it went over the wire (what
    // EvseV2G publishes as v2g_messages). The frame view is only valid for the duration of the call.
    std::function<void(const V2gMessageType&, const io::StreamInputView& exi_frame)> v2g_message;
    // Reported before the SECC picks one, so a failed negotiation is reported too (EvseV2G parity).
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

    // Anchors the CP-oscillator retain time [V2G-DC-968]; does NOT imply link teardown.
    std::function<void(SessionStopAction)> session_stop_res_sent;

    // ISO 15118-20 has no counterpart -- it negotiates authorization services instead.
    std::function<void(shared_datatypes::PaymentOption)> selected_payment_option;

    std::function<void(const std::string& emaid, const std::string& contract_chain_pem)> require_auth_pnc;

    // The raw request EXI (base64) goes to the CSMS/CPS backend; the response is injected back
    // asynchronously via a CertificateResponse control event.
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
    // \p exi_frame is the full V2GTP frame; the engines omit it, the Session attaches it on the way through.
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
