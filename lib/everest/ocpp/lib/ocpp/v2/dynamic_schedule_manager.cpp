// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/dynamic_schedule_manager.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <everest/database/exceptions.hpp>

#include <ocpp/common/constants.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/database_handler.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/message_dispatcher.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocpp/v2/profile.hpp>

#include <ocpp/v21/messages/PullDynamicScheduleUpdate.hpp>
#include <ocpp/v21/messages/UpdateDynamicSchedule.hpp>

namespace ocpp::v2 {

DynamicScheduleManager::DynamicScheduleManager(const FunctionalBlockContext& functional_block_context,
                                               std::function<void()> set_charging_profiles_callback) :
    context(functional_block_context),
    set_charging_profiles_callback(std::move(set_charging_profiles_callback)),
    timer_state(TimerState{std::make_unique<Everest::SteadyTimer>()}) {
    this->reaper_thread = std::thread(&DynamicScheduleManager::reaper_loop, this);
}

DynamicScheduleManager::~DynamicScheduleManager() {
    // Signal teardown before anything else so the reaper, possibly mid-wait on a silent CSMS,
    // wakes within one poll slice instead of holding the join for the full per-pull bound.
    this->teardown_requested.store(true, std::memory_order_relaxed);
    // Wake the reaper if it is blocked on the monitor's condition variable, then join it (in the
    // destructor body, before the implicit std::thread destructor would std::terminate on a
    // still-joinable thread). On observing teardown the reaper drops all in-flight futures.
    this->pending_pulls.notify_all();
    if (this->reaper_thread.joinable()) {
        this->reaper_thread.join();
    }
    // Retire the timer so a concurrent reschedule_timer bails instead of re-arming during teardown.
    std::unique_ptr<Everest::SteadyTimer> retired_timer;
    {
        auto state = this->timer_state.handle();
        state->shutting_down = true;
        retired_timer = std::move(state->timer);
    }
    retired_timer.reset();
}

void DynamicScheduleManager::rebuild_from_db() {
    // K28.FR.10: on construction, walk persisted profiles and seed deadline state.
    std::vector<ChargingProfile> all;
    try {
        all = this->context.database_handler.get_all_charging_profiles();
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_error << "Could not load charging profiles during deadline-state rebuild: " << e.what();
        return;
    }
    for (const auto& profile : all) {
        this->write_deadlines(profile);
    }
    // One arm after seeding every profile instead of O(N) rearms.
    this->reschedule_timer();
}

bool DynamicScheduleManager::write_deadlines(const ChargingProfile& profile) {
    auto handle = this->deadlines.handle();

    // K28.FR.10 pull deadline and K28.FR.13/.14/.15 schedule-duration expiry deadline.
    // Reactivation (FR.14) is implicit: a refreshed dynUpdateTime flows through both helpers
    // and re-arms the deadlines.
    const auto pull = dynamic_pull_deadline(profile);
    const auto expire = dynamic_expiry_deadline(profile);

    // Atomic cross-deadline write: both fields land in one entry under one handle. When neither
    // deadline applies the entry is dropped entirely.
    const auto it = handle->find(profile.id);
    if (pull.has_value() || expire.has_value()) {
        if (it == handle->end() || it->second.pull != pull || it->second.expire != expire) {
            auto& entry = (*handle)[profile.id];
            entry.pull = pull;
            entry.expire = expire;
            return true;
        }
        return false;
    }
    if (it != handle->end()) {
        handle->erase(it);
        return true;
    }
    return false;
}

void DynamicScheduleManager::update_tracking(const ChargingProfile& profile) {
    // Rearm only when a deadline actually changed. A non-Dynamic profile (the common SetChargingProfile
    // case) writes nothing and needs no timer churn. Handle released before arming (lock order:
    // timer_state then deadlines).
    if (this->write_deadlines(profile)) {
        this->reschedule_timer();
    }
}

void DynamicScheduleManager::erase_tracking(std::int32_t charging_profile_id) {
    bool changed = false;
    {
        auto handle = this->deadlines.handle();
        changed = handle->erase(charging_profile_id) > 0;
    }
    if (changed) {
        this->reschedule_timer();
    }
}

void DynamicScheduleManager::handle_update_dynamic_schedule_request(const ocpp::EnhancedMessage<MessageType>& message) {
    const Call<v21::UpdateDynamicScheduleRequest> call = message.message;

    v21::UpdateDynamicScheduleResponse response;
    response.status = ChargingProfileStatusEnum::Rejected;

    ChargingProfileCriterion criteria;
    criteria.chargingProfileId = std::vector<std::int32_t>{call.msg.chargingProfileId};
    std::vector<ReportedChargingProfile> matches;
    try {
        matches = this->context.database_handler.get_charging_profiles_matching_criteria(std::nullopt, criteria);
    } catch (const everest::db::QueryExecutionException& e) {
        const std::string detail = "Could not look up profile " + std::to_string(call.msg.chargingProfileId) +
                                   " for UpdateDynamicSchedule: " + e.what();
        EVLOG_error << detail;
        response.statusInfo = StatusInfo();
        response.statusInfo->reasonCode = "InternalError";
        response.statusInfo->additionalInfo = CiString<1024>(detail, StringTooLarge::Truncate);
        const ocpp::CallResult<v21::UpdateDynamicScheduleResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    bool fire_callback = false;

    // K28.FR.11: reject when the target profile is missing or non-Dynamic. additionalInfo
    // discriminates the two cases so a CSMS can tell "id unknown" from "id exists, wrong kind".
    if (matches.empty() || matches.front().profile.chargingProfileKind != ChargingProfileKindEnum::Dynamic) {
        response.statusInfo = StatusInfo();
        response.statusInfo->reasonCode = "InvalidProfile";
        response.statusInfo->additionalInfo = matches.empty() ? "ProfileNotFound" : "ProfileNotDynamic";
    } else {
        auto& matched = matches.front();
        const auto apply_result =
            this->apply_update(matched.profile, call.msg.scheduleUpdate, matched.evse_id, matched.source);
        switch (apply_result) {
        case ApplyResult::AppliedFieldsCallbackPending:
            fire_callback = true;
            [[fallthrough]];
        case ApplyResult::AppliedNoFieldsCallbackSkipped:
            // K28.FR.10: refresh the pull deadline from the updated dynUpdateTime (an empty
            // heartbeat-style update still refreshed it).
            response.status = ChargingProfileStatusEnum::Accepted;
            this->update_tracking(matched.profile);
            break;
        case ApplyResult::PersistFailed:
            response.statusInfo = StatusInfo();
            response.statusInfo->reasonCode = "InternalError";
            response.statusInfo->additionalInfo = "ProfilePersistFailed";
            break;
        }
    }

    const ocpp::CallResult<v21::UpdateDynamicScheduleResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);

    // Fire the smart-charging callback only when the apply succeeded AND fields were updated;
    // never fire on persist failure or empty (heartbeat-style) updates.
    if (fire_callback) {
        this->set_charging_profiles_callback();
    }
}

DynamicScheduleManager::ApplyResult DynamicScheduleManager::apply_update(ChargingProfile& profile,
                                                                         const ChargingScheduleUpdate& update,
                                                                         std::int32_t evse_id,
                                                                         const CiString<20>& charging_limit_source) {
    static constexpr std::pair<std::optional<double> ChargingScheduleUpdate::*,
                               std::optional<double> ChargingSchedulePeriod::*>
        K28_UPDATE_FIELDS[] = {
            {&ChargingScheduleUpdate::limit, &ChargingSchedulePeriod::limit},
            {&ChargingScheduleUpdate::limit_L2, &ChargingSchedulePeriod::limit_L2},
            {&ChargingScheduleUpdate::limit_L3, &ChargingSchedulePeriod::limit_L3},
            {&ChargingScheduleUpdate::dischargeLimit, &ChargingSchedulePeriod::dischargeLimit},
            {&ChargingScheduleUpdate::dischargeLimit_L2, &ChargingSchedulePeriod::dischargeLimit_L2},
            {&ChargingScheduleUpdate::dischargeLimit_L3, &ChargingSchedulePeriod::dischargeLimit_L3},
            {&ChargingScheduleUpdate::setpoint, &ChargingSchedulePeriod::setpoint},
            {&ChargingScheduleUpdate::setpoint_L2, &ChargingSchedulePeriod::setpoint_L2},
            {&ChargingScheduleUpdate::setpoint_L3, &ChargingSchedulePeriod::setpoint_L3},
            {&ChargingScheduleUpdate::setpointReactive, &ChargingSchedulePeriod::setpointReactive},
            {&ChargingScheduleUpdate::setpointReactive_L2, &ChargingSchedulePeriod::setpointReactive_L2},
            {&ChargingScheduleUpdate::setpointReactive_L3, &ChargingSchedulePeriod::setpointReactive_L3},
        };
    const bool no_fields_set = std::none_of(std::begin(K28_UPDATE_FIELDS), std::end(K28_UPDATE_FIELDS),
                                            [&](const auto& f) { return (update.*f.first).has_value(); });

    // Defensive invariant guard, unreachable by valid CSMS traffic: K28.FR.01 is enforced at
    // SetChargingProfile ingress and only validated profiles ever reach here, so an empty schedule
    // or period vector means the stored row is corrupt (schema drift / hand-edited DB) - an
    // InternalError, not user input. Reject before mutating dynUpdateTime so the pull-deadline
    // state stays consistent with what's persisted.
    if (!no_fields_set &&
        (profile.chargingSchedule.empty() || profile.chargingSchedule.at(0).chargingSchedulePeriod.empty())) {
        EVLOG_error << "Cannot apply ChargingScheduleUpdate to profile " << profile.id << ": malformed schedule shape.";
        return ApplyResult::PersistFailed;
    }

    // K28.FR.09: refresh dynUpdateTime even when the update is empty (heartbeat-style ping).
    profile.dynUpdateTime = ocpp::DateTime();

    if (no_fields_set) {
        EVLOG_debug << "UpdateDynamicSchedule for profile " << profile.id
                    << ": empty update (FR.09 heartbeat refresh).";
    } else {
        auto& period = profile.chargingSchedule.at(0).chargingSchedulePeriod.at(0);
        for (const auto& [u, p] : K28_UPDATE_FIELDS) {
            if ((update.*u).has_value()) {
                period.*p = (update.*u).value();
            }
        }
    }

    try {
        this->context.database_handler.insert_or_update_charging_profile(evse_id, profile, charging_limit_source);
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_error << "Could not persist updated Dynamic ChargingProfile in the database: " << e.what();
        return ApplyResult::PersistFailed;
    }

    return no_fields_set ? ApplyResult::AppliedNoFieldsCallbackSkipped : ApplyResult::AppliedFieldsCallbackPending;
}

void DynamicScheduleManager::reschedule_timer() {
    auto state = this->timer_state.handle();
    if (state->shutting_down || !state->timer) {
        return;
    }

    std::optional<date::utc_clock::time_point> earliest_deadline;
    {
        auto handle = this->deadlines.handle();
        for (const auto& [id, entry] : *handle) {
            if (entry.pull.has_value() &&
                (!earliest_deadline.has_value() || entry.pull.value() < earliest_deadline.value())) {
                earliest_deadline = entry.pull;
            }
            if (entry.expire.has_value() &&
                (!earliest_deadline.has_value() || entry.expire.value() < earliest_deadline.value())) {
                earliest_deadline = entry.expire;
            }
        }
    }

    if (earliest_deadline.has_value()) {
        state->timer->at([this]() { this->on_deadline(); }, earliest_deadline.value());
    } else {
        state->timer->stop();
    }
}

DynamicScheduleManager::DynamicProfileLookup
DynamicScheduleManager::lookup_dynamic_profile(std::int32_t charging_profile_id) const {
    ChargingProfileCriterion criteria;
    criteria.chargingProfileId = std::vector<std::int32_t>{charging_profile_id};
    std::vector<ReportedChargingProfile> matches;
    try {
        matches = this->context.database_handler.get_charging_profiles_matching_criteria(std::nullopt, criteria);
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_warning << "Could not look up profile " << charging_profile_id << ": " << e.what();
        return {std::nullopt, true};
    }
    if (matches.empty() || matches.front().profile.chargingProfileKind != ChargingProfileKindEnum::Dynamic) {
        return {std::nullopt, false};
    }
    return {matches.front(), false};
}

void DynamicScheduleManager::dispatch_due_pulls(date::utc_clock::time_point now) {
    // Snapshot ids whose pull deadline has passed; release the handle before doing any work.
    std::vector<std::int32_t> due_pull_ids;
    {
        auto handle = this->deadlines.handle();
        for (const auto& [id, entry] : *handle) {
            if (entry.pull.has_value() && entry.pull.value() <= now) {
                due_pull_ids.push_back(id);
            }
        }
    }

    for (const auto id : due_pull_ids) {
        // Re-read profile to get fresh dynUpdateInterval (may have been mutated since deadline was set).
        const auto match = this->lookup_dynamic_profile(id);
        if (match.db_error) {
            // Database error, not a deletion: the profile may still exist. Keep tracking and back off
            // 1s (same as the dispatch-failure path below) so the next tick retries.
            auto handle = this->deadlines.handle();
            auto it = handle->find(id);
            if (it != handle->end() && it->second.pull.has_value() && it->second.pull.value() <= now) {
                it->second.pull = now + std::chrono::seconds(1);
            }
            continue;
        }
        if (!match.profile.has_value() || !match.profile->profile.dynUpdateInterval.has_value() ||
            match.profile->profile.dynUpdateInterval.value() <= 0) {
            // Profile gone / non-Dynamic / no interval: drop tracking. Inlined (not
            // erase_tracking) to avoid reschedule_timer re-arming mid-tick; on_deadline
            // reschedules once at the end.
            {
                auto handle = this->deadlines.handle();
                handle->erase(id);
            }
            continue;
        }

        const auto interval = std::chrono::seconds(match.profile->profile.dynUpdateInterval.value());
        const bool dispatch_succeeded = this->dispatch_pull_request(id);
        if (!dispatch_succeeded) {
            // Dispatch failed: 1s backoff so a past-due deadline doesn't busy-loop the timer.
            auto handle = this->deadlines.handle();
            auto it = handle->find(id);
            if (it != handle->end() && it->second.pull.has_value() && it->second.pull.value() <= now) {
                it->second.pull = now + std::chrono::seconds(1);
            }
            continue;
        }
        {
            // Re-check before overwriting: a concurrent update_tracking may have written a
            // fresh deadline we must not stomp.
            auto handle = this->deadlines.handle();
            auto it = handle->find(id);
            if (it != handle->end() && it->second.pull.has_value() && it->second.pull.value() <= now) {
                it->second.pull = now + interval;
            }
        }
    }
}

void DynamicScheduleManager::commit_due_expiries(date::utc_clock::time_point now) {
    // K28.FR.13 expiry: fire the composite recompute, then clear expire only if the callback
    // succeeds (on throw, keep entries so the next tick retries). The deferred clear re-checks
    // the deadline so a concurrent reactivation is not wiped.
    std::vector<std::int32_t> expired_ids;
    {
        auto handle = this->deadlines.handle();
        for (const auto& [id, entry] : *handle) {
            if (entry.expire.has_value() && entry.expire.value() <= now) {
                expired_ids.push_back(id);
            }
        }
    }
    if (expired_ids.empty()) {
        return;
    }
    try {
        this->set_charging_profiles_callback();
    } catch (...) {
        // Keep the expire entries so the next tick retries the recompute. Rethrow so the
        // outer catch records the exception type; the entries are intentionally NOT cleared.
        EVLOG_error << "K28-expiry: composite recompute callback threw for " << expired_ids.size()
                    << " expired profile(s); entries kept, recompute retried next timer tick";
        throw;
    }
    // Callback succeeded: clear the expire field, but only while it is still the past
    // deadline (preserve a refreshed future deadline from a concurrent FR.14 reactivation).
    // Erase the key when both deadlines become nullopt.
    auto handle = this->deadlines.handle();
    for (const auto id : expired_ids) {
        auto it = handle->find(id);
        if (it != handle->end() && it->second.expire.has_value() && it->second.expire.value() <= now) {
            it->second.expire.reset();
            if (!it->second.pull.has_value()) {
                handle->erase(it);
            }
        }
    }
}

void DynamicScheduleManager::on_deadline() {
    // Runs on the timer io_context thread; any uncaught exception would terminate the process.
    try {
        const auto now = date::utc_clock::now();
        this->dispatch_due_pulls(now);
        // commit_due_expiries rethrows on callback failure (entries already kept inside it); that
        // propagates here so the outer catch records the type and keeps the timer thread alive.
        this->commit_due_expiries(now);
    } catch (const std::exception& e) {
        EVLOG_error << "on_deadline: unhandled exception swallowed to keep timer thread alive: " << e.what();
    } catch (...) {
        EVLOG_error << "on_deadline: unknown exception swallowed to keep timer thread alive";
    }

    // Always rearm; an exception in reschedule_timer must not kill the timer thread either.
    try {
        this->reschedule_timer();
    } catch (const std::exception& e) {
        EVLOG_error << "on_deadline: reschedule_timer threw: " << e.what();
    } catch (...) {
        EVLOG_error << "on_deadline: reschedule_timer threw unknown exception";
    }
}

bool DynamicScheduleManager::dispatch_pull_request(std::int32_t charging_profile_id) {
    v21::PullDynamicScheduleUpdateRequest req;
    req.chargingProfileId = charging_profile_id;

    const ocpp::Call<v21::PullDynamicScheduleUpdateRequest> call(req);

    // Single-flight: skip if a pull for this id is already in flight. Only the timer thread
    // pushes here and the reaper only removes, so check-then-push is race-free. Return true so
    // the timer advances the deadline, treating the suppressed pull as in flight.
    {
        auto handle = this->pending_pulls.handle();
        if (std::any_of(handle->begin(), handle->end(),
                        [charging_profile_id](const PendingPull& p) { return p.id == charging_profile_id; })) {
            EVLOG_debug << "K28: PullDynamicScheduleUpdate for profile " << charging_profile_id
                        << " already in flight; suppressing duplicate dispatch";
            return true;
        }
    }

    // dispatch_call_async may sync-throw on queue-full / websocket-down / serialization failure.
    // Catch locally and report failure so the timer-thread caller skips advancing the pull deadline:
    // we want the next tick to retry promptly rather than wait a full interval.
    std::future<ocpp::EnhancedMessage<MessageType>> future;
    try {
        future = this->context.message_dispatcher.dispatch_call_async(call);
    } catch (const std::exception& e) {
        EVLOG_warning << "PullDynamicScheduleUpdate dispatch_call_async threw for profile " << charging_profile_id
                      << ": " << e.what();
        return false;
    }

    // Bound the reaper's per-pull wait (MessageTimeout + 10s slack) so the destructor join
    // can't hang on a silent CSMS; fall back to the default if the device-model read throws.
    std::chrono::seconds pull_wait{DEFAULT_WAIT_FOR_FUTURE_TIMEOUT};
    try {
        const auto msg_timeout =
            this->context.device_model.get_value<int>(ControllerComponentVariables::MessageTimeout);
        pull_wait = std::chrono::seconds(msg_timeout) + std::chrono::seconds(10);
    } catch (const std::exception& e) {
        EVLOG_warning << "K28: MessageTimeout read failed, using " << DEFAULT_WAIT_FOR_FUTURE_TIMEOUT.count()
                      << "s worker bound: " << e.what();
    }

    // Hand the future to the reaper (not the timer thread) so a slow CSMS can't block the timer.
    // Notify so the reaper wakes immediately instead of waiting a poll slice.
    {
        auto handle = this->pending_pulls.handle();
        handle->push_back(
            PendingPull{charging_profile_id, std::move(future), std::chrono::steady_clock::now() + pull_wait});
    }
    this->pending_pulls.notify_one();
    return true;
}

void DynamicScheduleManager::reaper_loop() {
    constexpr auto poll_slice = std::chrono::milliseconds(200);
    while (!this->teardown_requested.load(std::memory_order_relaxed)) {
        std::vector<PendingPull> ready;
        {
            auto handle = this->pending_pulls.handle();
            // Wait on teardown, else time out after poll_slice and re-scan. A ready future does
            // NOT notify the monitor, so the timeout is load-bearing: it drives the periodic
            // re-scan for ready/expired entries. A dispatch's notify only trims wake latency.
            handle.wait_for([&] { return this->teardown_requested.load(std::memory_order_relaxed); }, poll_slice);
            if (this->teardown_requested.load(std::memory_order_relaxed)) {
                // Drop everything: let handle (and the futures it owns) destruct.
                break;
            }
            // Still holding the lock: collect entries that are ready or past their deadline, moving
            // their futures out, and erase them from the live vector.
            const auto now = std::chrono::steady_clock::now();
            auto& pulls = *handle;
            const auto split = std::remove_if(pulls.begin(), pulls.end(), [&](PendingPull& p) {
                const bool is_ready =
                    p.future.valid() && p.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                if (is_ready || now >= p.deadline) {
                    ready.push_back(std::move(p));
                    return true;
                }
                return false;
            });
            pulls.erase(split, pulls.end());
        }

        // Lock released: validate and apply each collected entry.
        for (auto& entry : ready) {
            if (this->teardown_requested.load(std::memory_order_relaxed)) {
                EVLOG_debug << "K28-reaper: teardown requested, abandoning PullDynamicScheduleUpdate wait for profile "
                            << entry.id;
                break;
            }
            if (std::chrono::steady_clock::now() >= entry.deadline) {
                EVLOG_warning << "K28: PullDynamicScheduleUpdate response timed out for profile " << entry.id
                              << "; pull skipped, retry next dynUpdateInterval";
                continue;
            }
            ocpp::EnhancedMessage<MessageType> enhanced_message;
            try {
                enhanced_message = entry.future.get();
            } catch (const std::exception& e) {
                // Transient failure (e.g. websocket drop): no deadline feedback, the profile stays
                // one dynUpdateInterval stale until the next attempt.
                EVLOG_error << "K28-reaper: PullDynamicScheduleUpdate future threw for profile " << entry.id
                            << "; pull skipped, next attempt in one dynUpdateInterval: " << e.what();
                continue;
            }
            if (enhanced_message.offline) {
                // CSMS unreachable on response: no deadline feedback, profile stays stale.
                EVLOG_error << "K28-reaper: PullDynamicScheduleUpdate response offline for profile " << entry.id
                            << "; pull skipped, next attempt in one dynUpdateInterval";
                continue;
            }
            if (enhanced_message.messageType != MessageType::PullDynamicScheduleUpdateResponse) {
                EVLOG_warning << "Unexpected response type for PullDynamicScheduleUpdate: "
                              << enhanced_message.messageType;
                continue;
            }
            try {
                this->apply_pull_response(entry.id, enhanced_message);
            } catch (const std::exception& e) {
                EVLOG_error << "K28-reaper: pull-response apply for profile " << entry.id
                            << " threw; result dropped, next attempt in one dynUpdateInterval: " << e.what();
            } catch (...) {
                EVLOG_error << "K28-reaper: pull-response apply for profile " << entry.id
                            << " threw unknown exception; result dropped, next attempt in one dynUpdateInterval";
            }
        }
    }
}

void DynamicScheduleManager::apply_pull_response(std::int32_t charging_profile_id,
                                                 const ocpp::EnhancedMessage<MessageType>& enhanced_message) {
    v21::PullDynamicScheduleUpdateResponse response;
    try {
        const ocpp::CallResult<v21::PullDynamicScheduleUpdateResponse> call_result = enhanced_message.message;
        response = call_result.msg;
    } catch (const std::exception& e) {
        EVLOG_warning << "Could not parse PullDynamicScheduleUpdateResponse: " << e.what();
        return;
    }

    if (response.status != ChargingProfileStatusEnum::Accepted || !response.scheduleUpdate.has_value()) {
        EVLOG_debug << "PullDynamicScheduleUpdate not Accepted or scheduleUpdate missing for profile "
                    << charging_profile_id;
        return;
    }

    // Look up the profile fresh; it may have been cleared or replaced while in flight.
    auto match = this->lookup_dynamic_profile(charging_profile_id);
    if (!match.profile.has_value()) {
        if (match.db_error) {
            EVLOG_warning << "Could not look up profile " << charging_profile_id
                          << " for pull response (database error); ignoring, retry next dynUpdateInterval.";
        } else {
            EVLOG_debug << "Profile " << charging_profile_id
                        << " missing or no longer Dynamic; ignoring pull response.";
        }
        return;
    }

    auto& matched = match.profile.value();
    const auto apply_result =
        this->apply_update(matched.profile, response.scheduleUpdate.value(), matched.evse_id, matched.source);
    switch (apply_result) {
    case ApplyResult::AppliedFieldsCallbackPending:
        // Fire-then-commit: publish the recompute BEFORE advancing pull tracking. If the
        // callback throws, do NOT advance tracking; the pull deadline is left unchanged
        // so the next tick retries.
        try {
            this->set_charging_profiles_callback();
        } catch (const std::exception& e) {
            EVLOG_error << "K28-reaper: composite recompute callback threw for profile " << charging_profile_id
                        << "; pull tracking NOT advanced, next attempt in one dynUpdateInterval: " << e.what();
            break;
        }
        this->update_tracking(matched.profile);
        break;
    case ApplyResult::AppliedNoFieldsCallbackSkipped:
        this->update_tracking(matched.profile);
        break;
    case ApplyResult::PersistFailed:
        EVLOG_error << "Could not persist Dynamic profile " << charging_profile_id << " from pull response.";
        break;
    }
}

} // namespace ocpp::v2
