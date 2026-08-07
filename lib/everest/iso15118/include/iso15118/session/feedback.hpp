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
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_selection.hpp>
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
    float power{NAN};
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
    std::function<void(const AcChargeLoopReq&)> ac_charge_loop_req;
    std::function<void(const V2gMessageType&)> v2g_message;
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
    void ac_charge_loop_req(const feedback::AcChargeLoopReq&) const;
    void v2g_message(const V2gMessageType&) const;
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
    void require_auth_pnc(const std::string& emaid, const std::string& contract_chain_pem) const;
    void certificate_request(const std::string& exi_request_base64, feedback::CertificateExchangeAction action) const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::session
