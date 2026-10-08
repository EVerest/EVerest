// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/message_handler.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

#include <everest/timer.hpp>
#include <everest/util/async/monitor.hpp>

#include <ocpp/v21/messages/ClearDERControl.hpp>
#include <ocpp/v21/messages/GetDERControl.hpp>
#include <ocpp/v21/messages/NotifyDERAlarm.hpp>
#include <ocpp/v21/messages/NotifyDERStartStop.hpp>
#include <ocpp/v21/messages/ReportDERControl.hpp>
#include <ocpp/v21/messages/SetDERControl.hpp>

#include <functional>
#include <optional>
#include <vector>

namespace ocpp::v21 {

/// \brief Callback carrying the full set of currently-active DER controls, emitted after every accepted
/// transition. The provider replaces its applied set wholesale from this argument.
using DERActiveDirectivesCallback = std::function<void(const std::vector<SetDERControlRequest>&)>;

class DERControlInterface : public v2::MessageHandlerInterface {
public:
    ~DERControlInterface() override = default;

    /// \brief Send a NotifyDERAlarm to the CSMS for a DER grid event.
    virtual void notify_der_alarm(const NotifyDERAlarmRequest& request) = 0;

    /// \brief Re-emit the current active-directive set on demand (no transition occurred). Lets a provider
    /// learn the standing set when, e.g., a newly-enabled EVSE joins an already-built block.
    virtual void republish_active_directives() const = 0;
};

/// Functional block R (DER control) for OCPP 2.1. libocpp stores and serves the DER controls (grid
/// codes) configured by the CSMS — primarily R04 "Configure DER control settings at Charging Station"
/// and R05 — while applying the controls to the hardware happens outside libocpp.
/// All DER controls are persisted in the database, no in-memory cache.
class DERControl : public DERControlInterface {
public:
    explicit DERControl(const v2::FunctionalBlockContext& context,
                        std::optional<DERActiveDirectivesCallback> active_directives_callback = std::nullopt);
    ~DERControl() override;

    void handle_message(const ocpp::EnhancedMessage<v2::MessageType>& message) override;

    /// Periodic check for expired scheduled controls. Call this to trigger a manual check.
    void check_scheduled_controls();

    void notify_der_alarm(const NotifyDERAlarmRequest& request) override;

    void republish_active_directives() const override;

private:
    const v2::FunctionalBlockContext& context;
    std::optional<DERActiveDirectivesCallback> active_directives_callback;
    // Destructor must block until any in-flight timer callback finishes;
    // the callback holds a handle on `stopping` so the destructor's
    // re-acquisition forces a wait. Required because the callback touches the
    // DatabaseHandler which ChargePoint frees before DERControl.
    everest::lib::util::monitor<bool> stopping{false};
    Everest::SteadyTimer scheduled_control_timer;

    void handle_set_der_control(ocpp::Call<SetDERControlRequest> call);
    void handle_get_der_control(ocpp::Call<GetDERControlRequest> call);
    void handle_clear_der_control(ocpp::Call<ClearDERControlRequest> call);

    /// Send NotifyDERStartStopRequest to CSMS
    void send_notify_start_stop(const CiString<36>& control_id, bool started, const ocpp::DateTime& timestamp,
                                const std::optional<std::vector<CiString<36>>>& superseded_ids = std::nullopt);

    /// Build and send ReportDERControlRequest(s) to CSMS
    void send_report(int32_t request_id, const std::vector<std::string>& control_jsons);

    /// Check if a controlType is supported by any EVSE (DC or AC DERCtrlr)
    bool is_control_type_supported(v2::DERControlEnum control_type) const;

    /// Validate that the correct control field is populated for the given controlType (R04.FR.16-17)
    bool validate_control_fields(const SetDERControlRequest& req) const;

    /// Compute the currently-effective DER controls from the database: not-superseded rows that are a
    /// default, or a scheduled control whose startTime has arrived and whose duration has not elapsed.
    std::vector<SetDERControlRequest> compute_active_directives() const;

    /// Emit the active-directives callback (if registered). The provider is third-party code run on the
    /// timer thread, so an escaping exception (→ std::terminate()) must never propagate out of here.
    void emit_active_directives() const;
};

} // namespace ocpp::v21
