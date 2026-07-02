// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Limitation of Power Consumption (LPC) use case state machine

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <optional>

#include <control_service/control_service.grpc.pb.h>
#include <usecases/cs/lpc/service.grpc-ext.pb.h>

#include <EebusCallbacks.hpp>
#include <generated/types/energy.hpp>

namespace module::lpc {

/// \brief Translate a gRPC LoadLimit into an EVerest ExternalLimits schedule.
///        An active limit produces a capped schedule entry (followed by an uncapped
///        entry when the limit carries a duration); an inactive limit produces a
///        single uncapped entry.
/// \param[in] load_limit - the limit received from the EEBUS service
/// \returns the equivalent external limits schedule
types::energy::ExternalLimits translate_to_external_limits(const common_types::LoadLimit& load_limit);

/// \brief Implements the LPC use case state machine: tracks the connected Energy
///        Guard (EG) via heartbeats, applies received consumption limits, and falls
///        back to the failsafe limit when the EG goes silent.
class LpcUseCaseHandler {
public:
    /// \brief LPC states per the use case specification's state diagram.
    enum class State : uint8_t {
        Init,
        UnlimitedControlled,
        Limited,
        Failsafe,
        UnlimitedAutonomous
    };

    /// Injectable clock — production code uses steady_clock::now(); unit tests
    /// supply a fake clock to control time without real delays.
    using ClockFn = std::function<std::chrono::time_point<std::chrono::steady_clock>()>;

    /// \brief construct the handler with its vendor-configured limits
    /// \param[in] failsafe_control_limit - failsafe active power limit in W
    /// \param[in] max_nominal_power - maximum nominal power consumption in W
    /// \param[in] callbacks - callbacks used to publish calculated limits
    /// \param[in] clock_fn - clock source; overridable for unit tests
    LpcUseCaseHandler(
        double failsafe_control_limit, double max_nominal_power, EebusCallbacks callbacks,
        ClockFn clock_fn = [] { return std::chrono::steady_clock::now(); });
    ~LpcUseCaseHandler() = default;
    LpcUseCaseHandler(const LpcUseCaseHandler&) = delete;
    LpcUseCaseHandler& operator=(const LpcUseCaseHandler&) = delete;
    LpcUseCaseHandler(LpcUseCaseHandler&&) = delete;
    LpcUseCaseHandler& operator=(LpcUseCaseHandler&&) = delete;

    /// \brief start heartbeat monitoring and apply the initial (failsafe) limit
    void start();

    /// \brief set the stub for the LPC service endpoint
    void set_stub(std::shared_ptr<cs_lpc::ControllableSystemLPCControl::Stub> stub);
    /// \brief dispatch a use case event received from the control service
    void handle_event(const control_service::SubscribeUseCaseEventsResponse& response);
    /// \brief evaluate timeouts and state transitions; apply limit changes
    void run_state_machine();
    /// \brief describes the LPC use case for registration with the control service
    static control_service::UseCase get_use_case_info();
    /// \brief push the vendor configuration (nominal max, failsafe values) to the service
    void configure_use_case();

    /// \brief process a limit received from outside (production: via gRPC; tests: directly)
    void process_received_limit(const common_types::LoadLimit& limit);

    /// \brief current state of the LPC state machine
    State get_state() const {
        return m_state;
    }

    /// [LPC-001] Active Power Consumption Limit value must be >= 0 W.
    /// Pure predicate; exposed publicly for unit-test access without adding friendships.
    static bool limit_value_is_valid(const common_types::LoadLimit& limit);

    /// [LPC-022/1, LPC-022/4] Accepts an EG-written Failsafe Duration Minimum iff it sits in
    /// [LPC_VENDOR_CONFIG_MIN, LPC_CS_MAXIMUM] = [2h, 24h], the range the spec prescribes for both
    /// the vendor pre-configured value and the CS's maximum accepted write.
    static bool failsafe_duration_is_valid(std::chrono::nanoseconds duration);

    /// [LPC-001] Failsafe Consumption Active Power Limit must be >= 0 W.
    /// Public so unit tests can call it without friendships.
    static bool failsafe_limit_is_valid(double watts);

private:
    using event_handler_t = void (LpcUseCaseHandler::*)();
    using TP = std::chrono::time_point<std::chrono::steady_clock>;

    void initialize_event_handlers();
    void set_state(State new_state);
    static std::string state_to_string(State state);
    bool is_data_event(const std::string& event_name) const;

    void approve_pending_writes();
    void update_limit_from_event();
    void apply_limit_for_current_state();
    void start_heartbeat();

    void handle_data_update_heartbeat();
    void handle_data_update_limit();
    void handle_data_update_failsafe_duration_minimum();
    void handle_data_update_failsafe_consumption_active_power_limit();
    void handle_write_approval_required();
    void handle_use_case_support_update();

    // Per-state handlers called by run_state_machine(). Each receives only the
    // pre-computed booleans it needs; all state mutations go through set_state().
    void handle_init_state(TP now, bool limit_is_active, bool limit_is_deactivated);
    void handle_limited_state(bool heartbeat_has_timeout, bool limit_is_deactivated, bool limit_expired);
    void handle_unlimited_controlled_state(bool heartbeat_has_timeout, bool limit_is_active);
    void handle_unlimited_autonomous_state(bool heartbeat_has_timeout, bool limit_is_active, bool limit_is_deactivated);
    void handle_failsafe_state(TP now, bool heartbeat_has_timeout, bool limit_is_active);

    EebusCallbacks m_callbacks;
    std::shared_ptr<cs_lpc::ControllableSystemLPCControl::Stub> m_stub;

    ClockFn m_clock_fn;

    State m_state{State::Init};
    bool m_state_changed{false};

    bool m_limit_value_changed{false};
    std::optional<common_types::LoadLimit> m_current_limit;

    // SKI of the currently-connected Energy Guard (EG). Set on the first data-carrying
    // event and cleared on entry to Failsafe or UnlimitedAutonomous. While connected,
    // events from any other SKI are ignored.
    std::optional<std::string> m_active_ems_ski;

    std::chrono::time_point<std::chrono::steady_clock> m_last_heartbeat_timestamp{
        std::chrono::time_point<std::chrono::steady_clock>::min()};

    std::chrono::seconds m_heartbeat_timeout{0};
    std::chrono::time_point<std::chrono::steady_clock> m_init_timestamp{
        std::chrono::time_point<std::chrono::steady_clock>::min()};
    // Timestamps that are set/reset atomically on every Failsafe entry.
    struct FailsafeContext {
        using TP = std::chrono::time_point<std::chrono::steady_clock>;
        TP entry{TP::min()};           // When we entered Failsafe [LPC-922 clock]
        TP first_heartbeat{TP::min()}; // First post-Failsafe heartbeat [LPC-921 clock]

        void reset(TP entry_time) {
            entry = entry_time;
            first_heartbeat = TP::min();
        }
    } m_failsafe_ctx;
    std::chrono::time_point<std::chrono::steady_clock> m_last_limit_received_timestamp{
        std::chrono::time_point<std::chrono::steady_clock>::min()};
    std::chrono::nanoseconds m_failsafe_duration_timeout{0};
    double m_failsafe_control_limit{0.0};
    double m_max_nominal_power{0.0};

    std::map<std::string, event_handler_t> m_event_handlers;
};

} // namespace module::lpc
