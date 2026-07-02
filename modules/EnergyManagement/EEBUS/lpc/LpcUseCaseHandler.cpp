// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <lpc/LpcUseCaseHandler.hpp>

#include <everest/logging.hpp>
#include <utils/date.hpp>

namespace module::lpc {

namespace {
// Per LPC-911 / LPC-912 the CS enters Failsafe after 120 s without a heartbeat.
constexpr auto HEARTBEAT_TIMEOUT = std::chrono::seconds(120);
// LPC_INIT_TIMEOUT: time limit for reaching a controlled state from Init [LPC-906].
constexpr auto LPC_INIT_TIMEOUT = std::chrono::seconds(120);
// LPC_RECOVERY_TIMEOUT: window after the first post-Failsafe heartbeat; if no limit arrives
// within this window, the CS MAY exit to Unlimited/autonomous [LPC-921].
constexpr auto LPC_RECOVERY_TIMEOUT = std::chrono::seconds(120);
// Default failsafe duration; matches the value pushed to the gRPC service in configure_use_case().
constexpr auto LPC_DEFAULT_FAILSAFE_DURATION = std::chrono::hours(2);
// [LPC-022/1] Vendor-configured lower bound for accepted Failsafe Duration Minimum writes.
// Spec requires a value in [2h, 24h]; we pick the floor.
constexpr auto LPC_VENDOR_CONFIG_MIN = std::chrono::hours(2);
// [LPC-022/4] The CS's maximum value for the Failsafe Duration Minimum. Spec-wide 24h ceiling.
constexpr auto LPC_CS_MAXIMUM = std::chrono::hours(24);

using TP = std::chrono::time_point<std::chrono::steady_clock>;
} // namespace

types::energy::ExternalLimits translate_to_external_limits(const common_types::LoadLimit& load_limit) {
    types::energy::ExternalLimits limits;
    std::vector<types::energy::ScheduleReqEntry> schedule_import;

    auto create_active_req = [](std::chrono::time_point<date::utc_clock> timestamp, double total_power_W) {
        types::energy::ScheduleReqEntry schedule_req_entry;
        types::energy::LimitsReq limits_req;
        schedule_req_entry.timestamp = Everest::Date::to_rfc3339(timestamp);
        types::energy::NumberWithSource total_power;
        total_power.value = total_power_W;
        total_power.source = "EEBUS LPC";
        limits_req.total_power_W = total_power;
        schedule_req_entry.limits_to_leaves = limits_req;
        schedule_req_entry.limits_to_root = limits_req;
        return schedule_req_entry;
    };

    auto create_inactive_req = [](std::chrono::time_point<date::utc_clock> timestamp) {
        types::energy::ScheduleReqEntry schedule_req_entry;
        schedule_req_entry.timestamp = Everest::Date::to_rfc3339(timestamp);
        schedule_req_entry.limits_to_leaves = types::energy::LimitsReq();
        schedule_req_entry.limits_to_root = types::energy::LimitsReq();
        return schedule_req_entry;
    };

    const auto now = date::utc_clock::from_sys(std::chrono::system_clock::now());

    if (load_limit.is_active()) {
        schedule_import.push_back(create_active_req(now, load_limit.value()));
        if (load_limit.duration_nanoseconds() > 0) {
            const auto timestamp = now + std::chrono::nanoseconds(load_limit.duration_nanoseconds());
            schedule_import.push_back(create_inactive_req(timestamp));
        }
    } else {
        schedule_import.push_back(create_inactive_req(now));
    }
    limits.schedule_import = schedule_import;
    return limits;
}

LpcUseCaseHandler::LpcUseCaseHandler(double failsafe_control_limit, double max_nominal_power, EebusCallbacks callbacks,
                                     ClockFn clock_fn) :
    m_callbacks(std::move(callbacks)),
    m_clock_fn(std::move(clock_fn)),
    m_heartbeat_timeout(HEARTBEAT_TIMEOUT),
    m_failsafe_duration_timeout(LPC_DEFAULT_FAILSAFE_DURATION),
    m_failsafe_control_limit(failsafe_control_limit),
    m_max_nominal_power(max_nominal_power) {
    initialize_event_handlers();
}

void LpcUseCaseHandler::initialize_event_handlers() {
    m_event_handlers = {
        {"DataUpdateHeartbeat", &LpcUseCaseHandler::handle_data_update_heartbeat},
        {"DataUpdateLimit", &LpcUseCaseHandler::handle_data_update_limit},
        {"DataUpdateFailsafeDurationMinimum", &LpcUseCaseHandler::handle_data_update_failsafe_duration_minimum},
        {"DataUpdateFailsafeConsumptionActivePowerLimit",
         &LpcUseCaseHandler::handle_data_update_failsafe_consumption_active_power_limit},
        {"WriteApprovalRequired", &LpcUseCaseHandler::handle_write_approval_required},
        {"UseCaseSupportUpdate", &LpcUseCaseHandler::handle_use_case_support_update},
    };
}

void LpcUseCaseHandler::start() {
    m_init_timestamp = m_clock_fn();
    m_last_heartbeat_timestamp = m_clock_fn(); // Seed to prevent spurious timeout on startup
    start_heartbeat();
    // Per [LPC-901/1] the CS SHALL start in Init already limited by the Failsafe Consumption
    // Active Power Limit, so apply it immediately rather than waiting for the first state change.
    apply_limit_for_current_state();
}

void LpcUseCaseHandler::set_stub(std::shared_ptr<cs_lpc::ControllableSystemLPCControl::Stub> stub) {
    m_stub = std::move(stub);
}

void LpcUseCaseHandler::handle_event(const control_service::SubscribeUseCaseEventsResponse& response) {
    const std::string& ski = response.remote_ski();
    const auto& event = response.use_case_event();
    const auto& event_str = event.event();

    // Record the first-seen data-carrying event's SKI as the connected EG. Non-data
    // events (UseCaseSupportUpdate, …) do not establish the connection on their
    // own — they are relayed only once an EG has been connected.
    if (!m_active_ems_ski.has_value() && is_data_event(event_str)) {
        m_active_ems_ski = ski;
        EVLOG_info << "LPC connected to active EG SKI=" << ski;
    }

    // Filter events from non-active EGs once a connection is established.
    if (m_active_ems_ski.has_value() && ski != *m_active_ems_ski) {
        EVLOG_debug << "Ignoring event '" << event_str << "' from non-connected EG SKI=" << ski;
        return;
    }

    auto it = m_event_handlers.find(event_str);
    if (it != m_event_handlers.end()) {
        (this->*(it->second))();
    } else if (!event_str.empty()) {
        EVLOG_error << "Unknown event received: " << event_str;
    }

    run_state_machine();
}

bool LpcUseCaseHandler::is_data_event(const std::string& event_name) const {
    return event_name == "DataUpdateHeartbeat" || event_name == "DataUpdateLimit" ||
           event_name == "DataUpdateFailsafeDurationMinimum" ||
           event_name == "DataUpdateFailsafeConsumptionActivePowerLimit" || event_name == "WriteApprovalRequired";
}

void LpcUseCaseHandler::handle_data_update_heartbeat() {
    m_last_heartbeat_timestamp = m_clock_fn();
    EVLOG_debug << "Heartbeat received";
}

void LpcUseCaseHandler::handle_data_update_limit() {
    update_limit_from_event();
}

void LpcUseCaseHandler::handle_data_update_failsafe_duration_minimum() {
    cs_lpc::FailsafeDurationMinimumRequest read_duration_req;
    cs_lpc::FailsafeDurationMinimumResponse read_duration_res;
    auto read_status = cs_lpc::CallFailsafeDurationMinimum(m_stub, read_duration_req, &read_duration_res);
    if (!read_status.ok()) {
        EVLOG_warning << "Could not re-read FailsafeDurationMinimum after update event: "
                      << read_status.error_message();
        return;
    }

    const auto new_duration = std::chrono::nanoseconds(read_duration_res.duration_nanoseconds());
    if (!failsafe_duration_is_valid(new_duration)) {
        EVLOG_warning << "Ignoring out-of-range FailsafeDurationMinimum " << read_duration_res.duration_nanoseconds()
                      << " ns — spec [LPC-022] requires [2h, 24h]. Keeping previous value "
                      << m_failsafe_duration_timeout.count() << " ns.";
        return;
    }

    m_failsafe_duration_timeout = new_duration;
    EVLOG_info << "FailsafeDurationMinimum updated to " << read_duration_res.duration_nanoseconds() << "ns";
}

void LpcUseCaseHandler::handle_data_update_failsafe_consumption_active_power_limit() {
    cs_lpc::FailsafeConsumptionActivePowerLimitRequest read_limit_req;
    cs_lpc::FailsafeConsumptionActivePowerLimitResponse read_limit_res;
    auto read_status = cs_lpc::CallFailsafeConsumptionActivePowerLimit(m_stub, read_limit_req, &read_limit_res);
    if (!read_status.ok()) {
        EVLOG_warning << "Could not re-read FailsafeConsumptionActivePowerLimit after update event: "
                      << read_status.error_message();
        return;
    }

    if (!failsafe_limit_is_valid(read_limit_res.limit())) {
        EVLOG_warning << "Ignoring negative FailsafeConsumptionActivePowerLimit " << read_limit_res.limit()
                      << " W — spec [LPC-001] requires >= 0. Keeping previous value " << m_failsafe_control_limit
                      << " W.";
        return;
    }

    m_failsafe_control_limit = read_limit_res.limit();
    EVLOG_info << "FailsafeConsumptionActivePowerLimit updated to " << m_failsafe_control_limit;
}

void LpcUseCaseHandler::handle_write_approval_required() {
    approve_pending_writes();
}

void LpcUseCaseHandler::handle_use_case_support_update() {
    // The EG's SPINE-level use case support has changed (e.g. EG reconnected while the gRPC
    // channel to the sidecar stayed alive). Re-push our configuration so the sidecar has current
    // values for the EG's Initial Scenario reads, then re-subscribe to heartbeats so the 120 s
    // timeout tracks the newly-active EG. Both calls are idempotent and fail-safe if the EG has
    // actually withdrawn support — the heartbeat will simply stop arriving and Failsafe triggers
    // after 120 s as normal.
    EVLOG_info << "UseCaseSupportUpdate received — re-configuring LPC and re-subscribing to heartbeat";
    configure_use_case();
    start_heartbeat();
}

void LpcUseCaseHandler::run_state_machine() {
    if (m_init_timestamp == TP::min()) {
        return;
    }

    auto now = m_clock_fn();

    bool heartbeat_has_timeout = (now - m_last_heartbeat_timestamp) > m_heartbeat_timeout;
    // Capture a local snapshot of m_current_limit for stable evaluation; see process_received_limit().
    std::optional<common_types::LoadLimit> limit = m_current_limit;
    const bool limit_is_active = limit.has_value() && limit->is_active();
    const bool limit_is_deactivated = limit.has_value() && !limit->is_active();
    const bool limit_expired =
        limit.has_value() && !limit->delete_duration() && limit->duration_nanoseconds() != 0 &&
        now >= (m_last_limit_received_timestamp + std::chrono::nanoseconds(limit->duration_nanoseconds()));

    if (limit_expired) {
        m_current_limit = std::nullopt;
    }

    switch (m_state) {
    case State::Init:
        handle_init_state(now, limit_is_active, limit_is_deactivated);
        break;
    case State::Limited:
        handle_limited_state(heartbeat_has_timeout, limit_is_deactivated, limit_expired);
        break;
    case State::UnlimitedControlled:
        handle_unlimited_controlled_state(heartbeat_has_timeout, limit_is_active);
        break;
    case State::UnlimitedAutonomous:
        handle_unlimited_autonomous_state(heartbeat_has_timeout, limit_is_active, limit_is_deactivated);
        break;
    case State::Failsafe:
        handle_failsafe_state(now, heartbeat_has_timeout, limit_is_active);
        break;
    }

    if (m_state_changed || m_limit_value_changed) {
        m_state_changed = false;
        m_limit_value_changed = false;
        apply_limit_for_current_state();
    }
}

void LpcUseCaseHandler::handle_init_state(TP now, bool limit_is_active, bool limit_is_deactivated) {
    // Three outgoing edges per spec Figure 2: [LPC-902/904/905/906]
    if ((now - m_init_timestamp) > LPC_INIT_TIMEOUT) {
        set_state(State::UnlimitedAutonomous);
    } else if (limit_is_active) {
        set_state(State::Limited);
    } else if (limit_is_deactivated) {
        set_state(State::UnlimitedControlled);
    }
}

void LpcUseCaseHandler::handle_limited_state(bool heartbeat_has_timeout, bool limit_is_deactivated,
                                             bool limit_expired) {
    if (heartbeat_has_timeout) {
        set_state(State::Failsafe);
    } else if (limit_is_deactivated || limit_expired) {
        // Either the HEMS explicitly deactivated the limit, or its duration elapsed.
        set_state(State::UnlimitedControlled);
    }
}

void LpcUseCaseHandler::handle_unlimited_controlled_state(bool heartbeat_has_timeout, bool limit_is_active) {
    if (heartbeat_has_timeout) {
        set_state(State::Failsafe);
    } else if (limit_is_active) {
        set_state(State::Limited);
    }
}

void LpcUseCaseHandler::handle_unlimited_autonomous_state(bool heartbeat_has_timeout, bool limit_is_active,
                                                          bool limit_is_deactivated) {
    // Per [LPC-918/919/920]: the CS SHALL only leave this state upon receipt of a
    // Heartbeat AND a following write on the Active Power Consumption Limit.
    if (heartbeat_has_timeout) {
        return;
    }
    if (limit_is_deactivated) {
        set_state(State::UnlimitedControlled);
    } else if (limit_is_active) {
        set_state(State::Limited);
    }
}

void LpcUseCaseHandler::handle_failsafe_state(TP now, bool heartbeat_has_timeout, bool limit_is_active) {
    // [LPC-922]: duration minimum expired — exit regardless of heartbeat.
    if (now >= (m_failsafe_ctx.entry + m_failsafe_duration_timeout)) {
        set_state(State::UnlimitedAutonomous);
        return;
    }

    // [LPC-921]: 120 s after first post-Failsafe heartbeat with no new limit — exit.
    if (m_failsafe_ctx.first_heartbeat != TP::min() && now >= (m_failsafe_ctx.first_heartbeat + LPC_RECOVERY_TIMEOUT)) {
        set_state(State::UnlimitedAutonomous);
        return;
    }

    // No heartbeat from EG — stay in Failsafe.
    if (heartbeat_has_timeout) {
        return;
    }

    // Heartbeat received — check whether a new limit arrived after Failsafe entry [LPC-916].
    if (m_last_limit_received_timestamp >= m_failsafe_ctx.entry) {
        set_state(limit_is_active ? State::Limited : State::UnlimitedControlled);
        return;
    }

    // Heartbeat received but no new limit yet — start the LPC-921 window on first occurrence.
    if (m_failsafe_ctx.first_heartbeat == TP::min()) {
        m_failsafe_ctx.first_heartbeat = m_last_heartbeat_timestamp;
    }
}

control_service::UseCase LpcUseCaseHandler::get_use_case_info() {
    return control_service::CreateUseCase(
        control_service::UseCase_ActorType_Enum::UseCase_ActorType_Enum_ControllableSystem,
        control_service::UseCase_NameType_Enum::UseCase_NameType_Enum_limitationOfPowerConsumption);
}

bool LpcUseCaseHandler::limit_value_is_valid(const common_types::LoadLimit& limit) {
    return limit.value() >= 0.0; // [LPC-001]
}

bool LpcUseCaseHandler::failsafe_duration_is_valid(std::chrono::nanoseconds duration) {
    return duration >= LPC_VENDOR_CONFIG_MIN && duration <= LPC_CS_MAXIMUM; // [LPC-022/1, LPC-022/4]
}

bool LpcUseCaseHandler::failsafe_limit_is_valid(double watts) {
    return watts >= 0.0; // [LPC-001]
}

void LpcUseCaseHandler::configure_use_case() {
    if (!m_stub) {
        return;
    }

    // Helper: execute a gRPC call and log on failure. Returns true if the call succeeded.
    auto call = [](std::string_view name, const grpc::Status& status) {
        if (!status.ok()) {
            EVLOG_error << name << " failed: " << status.error_message();
        }
        return status.ok();
    };

    cs_lpc::SetConsumptionNominalMaxRequest req1 = cs_lpc::CreateSetConsumptionNominalMaxRequest(m_max_nominal_power);
    cs_lpc::SetConsumptionNominalMaxResponse resp1;
    call("SetConsumptionNominalMax", cs_lpc::CallSetConsumptionNominalMax(m_stub, req1, &resp1));

    common_types::LoadLimit load_limit = common_types::CreateLoadLimit(0, true, false, m_max_nominal_power, false);
    cs_lpc::SetConsumptionLimitRequest req2 = cs_lpc::CreateSetConsumptionLimitRequest(&load_limit);
    cs_lpc::SetConsumptionLimitResponse resp2;
    call("SetConsumptionLimit", cs_lpc::CallSetConsumptionLimit(m_stub, req2, &resp2));
    // Release the stack-allocated load_limit from Protobuf ownership before req2 is destroyed,
    // to prevent Protobuf from attempting to delete memory it doesn't own.
    std::ignore = req2.release_load_limit();

    cs_lpc::SetFailsafeConsumptionActivePowerLimitRequest req3 =
        cs_lpc::CreateSetFailsafeConsumptionActivePowerLimitRequest(m_failsafe_control_limit, true);
    cs_lpc::SetFailsafeConsumptionActivePowerLimitResponse resp3;
    call("SetFailsafeConsumptionActivePowerLimit",
         cs_lpc::CallSetFailsafeConsumptionActivePowerLimit(m_stub, req3, &resp3));

    // Convert the default failsafe duration to nanoseconds for the gRPC request.
    const auto failsafe_duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(LPC_DEFAULT_FAILSAFE_DURATION).count();
    cs_lpc::SetFailsafeDurationMinimumRequest req4 =
        cs_lpc::CreateSetFailsafeDurationMinimumRequest(static_cast<double>(failsafe_duration_ns), true);
    cs_lpc::SetFailsafeDurationMinimumResponse resp4;
    call("SetFailsafeDurationMinimum", cs_lpc::CallSetFailsafeDurationMinimum(m_stub, req4, &resp4));
}

void LpcUseCaseHandler::approve_pending_writes() {
    if (!m_stub) {
        return;
    }

    cs_lpc::PendingConsumptionLimitRequest request = cs_lpc::CreatePendingConsumptionLimitRequest();
    cs_lpc::PendingConsumptionLimitResponse response;
    auto status = cs_lpc::CallPendingConsumptionLimit(m_stub, request, &response);
    if (!status.ok()) {
        EVLOG_error << "PendingConsumptionLimit failed: " << status.error_message();
        return;
    }

    for (const auto& entry : response.load_limits()) {
        const uint64_t msg_counter = entry.first;
        const auto& limit = entry.second;

        bool approve = true;
        std::string reason;
        if (!limit_value_is_valid(limit)) {
            approve = false;
            reason = "LPC-001: Active Power Consumption Limit must be >= 0 W";
            EVLOG_warning << "Rejecting pending limit msg_counter=" << msg_counter << " value=" << limit.value()
                          << " W: " << reason;
        }

        cs_lpc::ApproveOrDenyConsumptionLimitRequest approve_request =
            cs_lpc::CreateApproveOrDenyConsumptionLimitRequest(msg_counter, approve, reason);
        cs_lpc::ApproveOrDenyConsumptionLimitResponse approve_response;
        auto approve_status = cs_lpc::CallApproveOrDenyConsumptionLimit(m_stub, approve_request, &approve_response);
        if (!approve_status.ok()) {
            EVLOG_error << "ApproveOrDenyConsumptionLimit failed for msg_counter " << msg_counter << ": "
                        << approve_status.error_message();
        }
    }
}

void LpcUseCaseHandler::process_received_limit(const common_types::LoadLimit& limit) {
    m_current_limit = limit;
    m_last_limit_received_timestamp = m_clock_fn();
    m_limit_value_changed = true;
    run_state_machine();
}

void LpcUseCaseHandler::update_limit_from_event() {
    if (!m_stub) {
        return;
    }

    cs_lpc::ConsumptionLimitRequest request = cs_lpc::CreateConsumptionLimitRequest();
    cs_lpc::ConsumptionLimitResponse response;
    auto status = cs_lpc::CallConsumptionLimit(m_stub, request, &response);
    if (!status.ok()) {
        EVLOG_error << "ConsumptionLimit failed: " << status.error_message()
                    << ". Continuing with stale limit data; state machine may be holding a power "
                    << "constraint the remote energy manager has already revoked.";
        return;
    }
    process_received_limit(response.load_limit());
}

void LpcUseCaseHandler::start_heartbeat() {
    if (!m_stub) {
        return;
    }

    cs_lpc::StartHeartbeatRequest request;
    cs_lpc::StartHeartbeatResponse response;
    auto status = cs_lpc::CallStartHeartbeat(m_stub, request, &response);
    if (!status.ok()) {
        EVLOG_error << "StartHeartbeat subscription failed: " << status.error_message()
                    << ". System will enter Failsafe state in approximately 120 seconds unless the stub "
                    << "connection is restored.";
    }
}

void LpcUseCaseHandler::apply_limit_for_current_state() {
    if (!m_callbacks.all_callbacks_valid()) {
        EVLOG_error << "LPC Use Case Handler callbacks not initialized; skipping limit update";
        return;
    }

    types::energy::ExternalLimits limits;

    switch (m_state) {
    case State::Init: {
        // Per [LPC-901/1] the CS SHALL already be limited by the Failsafe Consumption Active
        // Power Limit while in Init. The Active Power Consumption Limit data point is
        // deactivated [LPC-009/2], but the physical power cap still applies.
        common_types::LoadLimit failsafe_limit;
        failsafe_limit.set_is_active(true);
        failsafe_limit.set_value(m_failsafe_control_limit);
        limits = translate_to_external_limits(failsafe_limit);
        break;
    }
    case State::Limited: {
        if (m_current_limit.has_value()) {
            limits = translate_to_external_limits(m_current_limit.value());
        } else {
            // No valid limit data in Limited state — fall back to failsafe limit to avoid
            // unexpected unlimited power.
            common_types::LoadLimit failsafe_limit;
            failsafe_limit.set_is_active(true);
            failsafe_limit.set_value(m_failsafe_control_limit);
            limits = translate_to_external_limits(failsafe_limit);
            EVLOG_warning << "Limited state without valid limit; applying failsafe limit";
        }
        break;
    }
    case State::Failsafe: {
        common_types::LoadLimit failsafe_limit;
        failsafe_limit.set_is_active(true);
        failsafe_limit.set_value(m_failsafe_control_limit);
        limits = translate_to_external_limits(failsafe_limit);
        break;
    }
    case State::UnlimitedControlled:
    case State::UnlimitedAutonomous: {
        common_types::LoadLimit unlimited_limit;
        unlimited_limit.set_is_active(false);
        limits = translate_to_external_limits(unlimited_limit);
        break;
    }
    }

    m_callbacks.update_limits_callback(limits);
}

void LpcUseCaseHandler::set_state(State new_state) {
    State old_state = m_state;
    if (old_state != new_state) {
        EVLOG_info << "LPC Use Case Handler changing state from " << state_to_string(old_state) << " to "
                   << state_to_string(new_state);
        m_state = new_state;
        m_state_changed = true;

        if (new_state == State::Failsafe) {
            // reset() sets entry timestamp and clears the LPC-921 heartbeat window.
            m_failsafe_ctx.reset(m_clock_fn());
        }

        // Both Failsafe and UnlimitedAutonomous represent states in which the CS no
        // longer trusts any cached EG-derived data:
        //   - Failsafe: heartbeat lost; EG presumed gone.
        //   - UnlimitedAutonomous: reachable from Init via LPC-906 timeout (no EG
        //     ever connected) or from Failsafe via LPC-922 / LPC-921 (no fresh
        //     write within the recovery window). Per [LPC-918/919/920] the CS
        //     SHALL only leave this state on a heartbeat AND a following write,
        //     so any cached limit predating that write must be discarded.
        // m_last_limit_received_timestamp is reset for contract symmetry even though
        // its only consumer (the Failsafe -> controlled transition gate) is already
        // bounded by m_failsafe_ctx.entry being reset per Failsafe entry.
        if (new_state == State::Failsafe || new_state == State::UnlimitedAutonomous) {
            EVLOG_info << "LPC clearing cached EG state on entry to " << state_to_string(new_state);
            m_current_limit = std::nullopt;
            m_last_limit_received_timestamp = TP::min();
            m_active_ems_ski.reset();
        }
    }
}

std::string LpcUseCaseHandler::state_to_string(State state) {
    switch (state) {
    case State::Init:
        return "Init";
    case State::UnlimitedControlled:
        return "Unlimited/controlled";
    case State::Limited:
        return "Limited";
    case State::Failsafe:
        return "Failsafe state";
    case State::UnlimitedAutonomous:
        return "Unlimited/autonomous";
    }
    return "Unknown";
}

} // namespace module::lpc
