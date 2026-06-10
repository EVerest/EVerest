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
#include <thread>
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

/// \brief A Dynamic profile's two K28 deadlines: when to pull the next schedule update and when the
/// current schedule expires.
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
/// the in-flight pull-response futures, multiplexed onto a single reaper thread. Composed inside
/// \ref SmartCharging.
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

    /// Signals teardown so the reaper thread, possibly waiting on an in-flight CSMS response,
    /// wakes within one poll slice instead of stalling for the full per-pull wait bound, then
    /// joins the reaper (it captures \c this, so it must not outlive this object).
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

    /// \brief Handle a K28 UpdateDynamicScheduleRequest from the CSMS.
    ///
    /// Resolves the target profile by id and rejects with reasonCode \c "InvalidProfile" if missing
    /// or non-Dynamic (K28.FR.11). Otherwise applies the update, refreshes pull-deadline tracking
    /// (K28.FR.10), and fires the smart-charging callback only when fields actually changed. The
    /// response is dispatched before the callback so the CSMS sees Accepted before downstream
    /// effects.
    void handle_update_dynamic_schedule_request(const ocpp::EnhancedMessage<MessageType>& message);

private:
    /// \brief Adaptive timer plus its shutdown flag, guarded as one unit so the two move and read
    /// together. \c shutting_down lets a concurrent \ref reschedule_timer bail instead of re-arming
    /// a timer the destructor is tearing down.
    struct TimerState {
        std::unique_ptr<Everest::SteadyTimer> timer;
        bool shutting_down{false};
    };

    /// \brief One in-flight PullDynamicScheduleUpdate: the profile id, the response future the
    /// reaper polls, and the absolute steady-clock deadline past which the wait is abandoned.
    struct PendingPull {
        std::int32_t id;
        std::future<ocpp::EnhancedMessage<MessageType>> future;
        std::chrono::steady_clock::time_point deadline;
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

    /// \brief Adaptive timer fire callback (timer io_context thread): run the pull and expiry passes,
    /// then rearm. See \ref dispatch_due_pulls and \ref commit_due_expiries.
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

    /// \brief Result of \ref lookup_dynamic_profile, separating a confirmed-absent profile from a
    /// database error so a transient failure does not look like a deletion.
    struct DynamicProfileLookup {
        /// \brief The match, set only when the id exists and its kind is Dynamic.
        std::optional<ReportedChargingProfile> profile;
        /// \brief True when the query threw: \c profile is empty but absence is unknown, so callers
        /// must keep tracking and retry rather than erase.
        bool db_error{false};
    };

    /// \brief Look up a single Dynamic charging profile by id. \c profile is set only when the id
    /// exists AND its \c chargingProfileKind is \c Dynamic. A failed query sets \c db_error (logged
    /// as a warning) so callers can distinguish a deleted profile from an unreachable database.
    DynamicProfileLookup lookup_dynamic_profile(std::int32_t charging_profile_id) const;

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
    /// Single-flight: if a pull for this id is already in flight (present in \ref pending_pulls),
    /// the dispatch is skipped without emitting a wire request and \c true is returned, so the
    /// timer advances the deadline normally. Otherwise the response future is stored in
    /// \ref pending_pulls and consumed by the single \ref reaper_loop thread (not the timer's
    /// io_context) so a slow CSMS cannot block the timer or deadlock by re-entering
    /// \ref update_tracking on the timer thread.
    ///
    /// \return \c true on successful dispatch (or single-flight skip); \c false when
    /// \c dispatch_call_async throws synchronously (queue full, websocket down, serialization
    /// failure). On \c false, the timer-thread caller must skip the deadline overwrite so the next
    /// tick retries promptly.
    bool dispatch_pull_request(std::int32_t charging_profile_id);

    /// \brief Single reaper thread: poll every in-flight pull future in slices, applying each
    /// response as it lands. One thread services all \ref pending_pulls regardless of count; the
    /// per-pull deadline bounds each wait, and \ref teardown_requested is observed every slice so
    /// the destructor join cannot hang on a silent CSMS. Validates each response (timeout,
    /// future-throw, offline, unexpected type) before calling \ref apply_pull_response.
    void reaper_loop();

    /// \brief Parse the response, re-look-up the profile
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

    /// \brief Set by the destructor before joining the reaper. The reaper observes this each poll
    /// slice and on its monitor wait predicate, abandoning and dropping all in-flight futures so
    /// teardown is not stalled for the full per-pull wait bound on a silent CSMS. Declared before
    /// \ref pending_pulls and \ref reaper_thread so destruction tears down in the reverse order.
    std::atomic<bool> teardown_requested{false};

    /// \brief In-flight PullDynamicScheduleUpdate responses, multiplexed onto \ref reaper_thread.
    /// A vector (not a thread_safe_queue) because the reaper must scan all entries each tick and
    /// erase arbitrary entries on completion/timeout, and \ref dispatch_pull_request scans it for
    /// the single-flight check. The monitor's own condition variable wakes the reaper on a fresh
    /// dispatch or on teardown. Pushed by the timer thread (the sole producer); entries are only
    /// removed by the reaper, so the single-flight check-then-push is race-free.
    everest::lib::util::monitor<std::vector<PendingPull>> pending_pulls;

    /// \brief The single thread running \ref reaper_loop. Started in the constructor, joined in the
    /// destructor body before its implicit std::thread destructor (which would std::terminate on a
    /// still-joinable thread). Captures \c this, so it must not outlive this object.
    std::thread reaper_thread;

    /// \brief Mutated from \ref reschedule_timer (message-handler and timer threads) and from the
    /// destructor.
    everest::lib::util::monitor<TimerState> timer_state;
};

} // namespace ocpp::v2
