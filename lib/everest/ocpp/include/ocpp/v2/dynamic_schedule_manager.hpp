// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <date/date.h>
#include <everest/timer.hpp>
#include <everest/util/async/monitor.hpp>

#include <ocpp/common/cistring.hpp>
#include <ocpp/v2/message_handler.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocpp/v2/types.hpp>

namespace ocpp::v2 {

struct FunctionalBlockContext;

/// \brief Per-profile adaptive-pull and expiry deadline tracking for K28 Dynamic profiles.
/// Both fields are tracked under a single map entry so a refresh that touches both deadlines
/// is observed atomically by the timer thread (no partial-state window).
struct ProfileDeadlines {
    /// \brief K28.FR.10 pull deadline: absolute UTC time the next PullDynamicScheduleUpdateRequest
    /// should be dispatched. Set from \c dynUpdateTime + \c dynUpdateInterval, or "now" on bootstrap
    /// (missing \c dynUpdateTime); \c std::nullopt when the profile is not pull-tracked.
    std::optional<date::utc_clock::time_point> pull;

    /// \brief K28.FR.13 expiry deadline: \c dynUpdateTime + \c chargingSchedule[0].duration.
    /// \c std::nullopt when the profile has no schedule duration or no \c dynUpdateTime.
    std::optional<date::utc_clock::time_point> expire;
};

/// \brief Owns adaptive-pull and expiry deadline tracking for K28 Dynamic charging profiles plus
/// the worker futures that consume CSMS pull responses. Composed inside \ref SmartCharging.
///
/// Public methods are safe to call from the smart-charging message-handler threads; the internal
/// timer fires on its own io_context thread and all state is guarded by monitors.
class DynamicScheduleManager {
public:
    /// \param context                         Functional-block context (device model, database,
    ///                                        message dispatcher) shared with \ref SmartCharging.
    /// \param set_charging_profiles_callback  Fired whenever the set of valid Dynamic profiles
    ///                                        changes (K28.FR.06 push apply, K28.FR.13 expiry).
    DynamicScheduleManager(const FunctionalBlockContext& context, std::function<void()> set_charging_profiles_callback);

    /// Signals teardown so any in-flight pull-response worker blocked on a CSMS response
    /// returns promptly instead of stalling for the full worker wait bound, then joins the
    /// workers (each captures \c this, so it must not outlive this object).
    ~DynamicScheduleManager();

    DynamicScheduleManager(const DynamicScheduleManager&) = delete;
    DynamicScheduleManager& operator=(const DynamicScheduleManager&) = delete;
    DynamicScheduleManager(DynamicScheduleManager&&) = delete;
    DynamicScheduleManager& operator=(DynamicScheduleManager&&) = delete;

    /// \brief K28.FR.10: rebuild pull- and expiry-deadline tracking by walking all persisted
    /// profiles. Called once from \ref SmartCharging's constructor; also exposed so tests that
    /// pre-populate the database can exercise the rebuild path.
    void rebuild_from_db();

    /// \brief Refresh adaptive-pull and expiry deadlines for \p profile, then rearm the timer.
    /// K28.FR.14 reactivation is implicit: a refreshed \c dynUpdateTime moves both deadlines forward.
    void update_tracking(const ChargingProfile& profile);

    /// \brief Erase pull- and expiry-deadline tracking for \p charging_profile_id and rearm the
    /// timer. Called from ClearChargingProfile and SetChargingProfile replace paths so a removed
    /// or replaced profile cannot leave a stale deadline behind.
    void erase_tracking(std::int32_t charging_profile_id);

    /// \brief Best-effort timer re-arm decoupled from deadline mutation: recovery shim for a
    /// failed \ref update_tracking refresh on an already-persisted profile.
    void reschedule();

    /// \brief Handle a K28 UpdateDynamicScheduleRequest from the CSMS.
    ///
    /// Resolves the target profile by id and rejects with reasonCode \c "InvalidProfile" if missing
    /// or non-Dynamic (K28.FR.11). Otherwise applies the update, refreshes pull-deadline tracking
    /// (K28.FR.10), and fires the smart-charging callback only when fields actually changed. The
    /// response is dispatched before the callback so the CSMS sees Accepted before downstream
    /// effects.
    void handle_update_dynamic_schedule_request(const ocpp::EnhancedMessage<MessageType>& message);

private:
    /// \brief Adaptive timer plus its shutdown flag, guarded as one unit by a monitor.
    /// The timer drives K28.FR.10 PullDynamicScheduleUpdate dispatch and K28.FR.13 expiry
    /// recompute. (Re)armed by \ref reschedule_timer at the earliest pull or expiry deadline
    /// across all tracked profiles; fires \ref on_deadline on its own io_context thread.
    /// \c shutting_down is set by the destructor so a concurrent \ref reschedule_timer bails
    /// instead of re-arming a timer that is being torn down.
    struct TimerState {
        std::unique_ptr<Everest::SteadyTimer> timer;
        bool shutting_down{false};
    };

    /// \brief Outcome of \ref apply_update.
    enum class ApplyResult {
        AppliedFieldsCallbackPending,   ///< Persisted; caller fires set_charging_profiles_callback.
        AppliedNoFieldsCallbackSkipped, ///< Persisted (heartbeat-style ping); no callback to fire.
        PersistFailed,                  ///< Database write failed; caller should respond Rejected/InternalError.
    };

    /// \brief Apply a K28.FR.06 \c ChargingScheduleUpdate to the target Dynamic profile.
    /// \param profile               Profile to mutate (also persisted to the database).
    /// \param update                Fields to overwrite on the profile's single
    ///                                \c chargingSchedulePeriod (K28.FR.01 guarantees one period).
    /// \param evse_id               EVSE id the profile is bound to (used by the persistence layer).
    /// \param charging_limit_source Source string (CSO / EMS / etc.) preserved on persist.
    ApplyResult apply_update(ChargingProfile& profile, const ChargingScheduleUpdate& update, std::int32_t evse_id,
                             const CiString<20>& charging_limit_source);

    /// \brief Adaptive timer fire callback: dispatch due pulls, drop expired entries, rearm.
    ///
    /// Runs on the timer's io_context thread. All state mutations go through monitor handles:
    /// - Pull side: snapshot due ids, dispatch each via \ref dispatch_pull_request, then re-check
    ///   the entry under a fresh handle before overwriting the deadline so a concurrent
    ///   \ref update_tracking cannot be clobbered.
    /// - Expire side: erase-while-iterating under a single handle so a concurrent refresh of
    ///   \c dynUpdateTime is not silently wiped between snapshot and erase (K28.FR.14
    ///   reactivation). A single \c set_charging_profiles_callback is fired after release.
    void on_deadline();

    /// \brief Pull pass of \ref on_deadline: snapshot the ids whose pull deadline is due as of
    /// \p now, then for each re-read the profile, drop tracking for ones that are gone / no longer
    /// Dynamic / have no positive interval, dispatch a \c PullDynamicScheduleUpdateRequest, and
    /// advance (or back off) the pull deadline. Runs on the timer's io_context thread.
    void dispatch_due_pulls(date::utc_clock::time_point now);

    /// \brief Expiry pass of \ref on_deadline: snapshot the ids whose expiry deadline is due as of
    /// \p now, fire a single composite recompute, and clear the expire field only after the
    /// callback succeeds (rethrowing on failure so the entries are kept for the next tick). Runs on
    /// the timer's io_context thread.
    void commit_due_expiries(date::utc_clock::time_point now);

    /// \brief Look up a single Dynamic charging profile by id. Returns the matched profile only when
    /// it exists AND its \c chargingProfileKind is \c Dynamic; returns \c std::nullopt when the id
    /// is unknown, the profile is not Dynamic, or the database query throws (logged as a warning).
    std::optional<ReportedChargingProfile> lookup_dynamic_profile(std::int32_t charging_profile_id) const;

    /// \brief Map mutation for \ref update_tracking and \ref rebuild_from_db: write/erase the pull
    /// and expiry deadlines for \p profile without rearming the timer. \return true if any deadline
    /// was added, removed, or changed.
    bool write_deadlines(const ChargingProfile& profile);

    /// \brief Recompute the earliest deadline across all pull and expiry deadlines and (re)arm
    /// the timer held in \ref timer_state.
    /// \note Lock order is the \ref timer_state monitor, then the \ref deadlines monitor. Callers
    /// must release any \ref deadlines handle before calling.
    void reschedule_timer();

    /// \brief Dispatch a \c PullDynamicScheduleUpdateRequest for \p charging_profile_id.
    ///
    /// The response handler runs on a worker thread (not the timer's io_context) so a slow CSMS
    /// cannot block the timer or deadlock by re-entering \ref update_tracking on the timer thread.
    /// Each handler's future is stored in \ref pending_pull_response_futures (compacted
    /// opportunistically) and waited on in the destructor.
    ///
    /// \return \c true on successful dispatch (worker queued); \c false when
    /// \c dispatch_call_async or \c std::async throws synchronously (queue full, websocket down,
    /// serialization failure, thread/resource exhaustion). On \c false, the timer-thread caller
    /// must skip the deadline overwrite so the next tick retries promptly.
    bool dispatch_pull_request(std::int32_t charging_profile_id);

    /// \brief Worker-thread step 1: block (bounded by \p worker_wait) on the
    /// PullDynamicScheduleUpdate response future and validate the envelope. Consumes
    /// \p fut. Returns the enhanced message only when actionable; logs and returns
    /// \c std::nullopt on timeout, future-throw, offline response, or unexpected
    /// message type.
    std::optional<ocpp::EnhancedMessage<MessageType>>
    wait_for_pull_response(std::future<ocpp::EnhancedMessage<MessageType>> fut, std::chrono::seconds worker_wait,
                           std::int32_t charging_profile_id);

    /// \brief Worker-thread step 2: parse the response, re-look-up the profile
    /// (it may have been cleared/replaced in flight), and apply the schedule update.
    /// On success the recompute callback is fired first and the pull deadline is
    /// advanced only afterwards, so a callback failure leaves the deadline unchanged
    /// and the next tick retries. Logs and returns on any non-actionable state (parse
    /// fail, not Accepted, profile gone/non-Dynamic, persist fail).
    void apply_pull_response(std::int32_t charging_profile_id,
                             const ocpp::EnhancedMessage<MessageType>& enhanced_message);

    const FunctionalBlockContext& context;
    std::function<void()> set_charging_profiles_callback;

    /// \brief K28.FR.10/.13 deadline tracking. Keyed by chargingProfileId; value carries both the
    /// pull and expiry deadlines for that profile. Tracking both deadlines under one map entry
    /// makes a cross-deadline refresh (\ref update_tracking) atomic from the timer thread's view.
    /// Mutated from \ref on_deadline (timer thread) and from \ref update_tracking /
    /// \ref erase_tracking (message-handler threads); all access goes through the monitor's scoped
    /// handle.
    everest::lib::util::monitor<std::map<std::int32_t, ProfileDeadlines>> deadlines;

    /// \brief Futures from in-flight async pull-response handlers. Each
    /// \ref dispatch_pull_request launches a worker task that captures \c this and consumes the
    /// CSMS response; its future is stored here and waited on in the destructor so no in-flight
    /// worker dereferences a destroyed object. Compacted opportunistically on dispatch to keep the
    /// vector bounded.
    everest::lib::util::monitor<std::vector<std::future<void>>> pending_pull_response_futures;

    /// \brief Set by the destructor before joining the pull-response workers. A worker polls this
    /// while waiting (in slices) on the CSMS response future and bails immediately once set, so
    /// teardown is not stalled for the full worker wait bound on a silent CSMS.
    std::atomic<bool> teardown_requested{false};

    /// \brief Adaptive timer and its shutdown flag, mutated from \ref reschedule_timer
    /// (message-handler and timer threads) and from the destructor. Guarded as one unit by the
    /// monitor so the timer pointer and \c shutting_down move and read together.
    everest::lib::util::monitor<TimerState> timer_state;
};

} // namespace ocpp::v2
