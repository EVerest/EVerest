// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/dynamic_schedule_manager.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <string>
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
}

DynamicScheduleManager::~DynamicScheduleManager() {
    // Signal teardown before anything else so a worker already blocked on a CSMS response
    // wakes within one wait slice instead of holding the join for the full worker bound.
    this->teardown_requested.store(true, std::memory_order_relaxed);
    // Stop the timer next so no new pull workers enter pending_pull_response_futures, then
    // flush in-flight workers.
    std::unique_ptr<Everest::SteadyTimer> retired_timer;
    {
        auto state = this->timer_state.handle();
        state->shutting_down = true;
        retired_timer = std::move(state->timer);
    }
    retired_timer.reset();
    std::vector<std::future<void>> flush;
    {
        auto handle = this->pending_pull_response_futures.handle();
        flush = std::move(*handle);
    }
    for (auto& fut : flush) {
        if (fut.valid()) {
            fut.wait();
        }
    }
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
    this->write_deadlines(profile);
    // Handle released before arming the timer (lock order: timer_state then deadlines).
    this->reschedule_timer();
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

void DynamicScheduleManager::reschedule() {
    this->reschedule_timer();
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
    const bool no_fields_set = !update.limit && !update.limit_L2 && !update.limit_L3 && !update.dischargeLimit &&
                               !update.dischargeLimit_L2 && !update.dischargeLimit_L3 && !update.setpoint &&
                               !update.setpoint_L2 && !update.setpoint_L3 && !update.setpointReactive &&
                               !update.setpointReactive_L2 && !update.setpointReactive_L3;

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
        if (update.limit) {
            period.limit = update.limit;
        }
        if (update.limit_L2) {
            period.limit_L2 = update.limit_L2;
        }
        if (update.limit_L3) {
            period.limit_L3 = update.limit_L3;
        }
        if (update.dischargeLimit) {
            period.dischargeLimit = update.dischargeLimit;
        }
        if (update.dischargeLimit_L2) {
            period.dischargeLimit_L2 = update.dischargeLimit_L2;
        }
        if (update.dischargeLimit_L3) {
            period.dischargeLimit_L3 = update.dischargeLimit_L3;
        }
        if (update.setpoint) {
            period.setpoint = update.setpoint;
        }
        if (update.setpoint_L2) {
            period.setpoint_L2 = update.setpoint_L2;
        }
        if (update.setpoint_L3) {
            period.setpoint_L3 = update.setpoint_L3;
        }
        if (update.setpointReactive) {
            period.setpointReactive = update.setpointReactive;
        }
        if (update.setpointReactive_L2) {
            period.setpointReactive_L2 = update.setpointReactive_L2;
        }
        if (update.setpointReactive_L3) {
            period.setpointReactive_L3 = update.setpointReactive_L3;
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

std::optional<ReportedChargingProfile>
DynamicScheduleManager::lookup_dynamic_profile(std::int32_t charging_profile_id) const {
    ChargingProfileCriterion criteria;
    criteria.chargingProfileId = std::vector<std::int32_t>{charging_profile_id};
    std::vector<ReportedChargingProfile> matches;
    try {
        matches = this->context.database_handler.get_charging_profiles_matching_criteria(std::nullopt, criteria);
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_warning << "Could not look up profile " << charging_profile_id << ": " << e.what();
        return std::nullopt;
    }
    if (matches.empty() || matches.front().profile.chargingProfileKind != ChargingProfileKindEnum::Dynamic) {
        return std::nullopt;
    }
    return matches.front();
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
        if (!match.has_value() || !match->profile.dynUpdateInterval.has_value() ||
            match->profile.dynUpdateInterval.value() <= 0) {
            // Profile gone / non-Dynamic / no interval: drop tracking. Inlined (not
            // erase_tracking) to avoid reschedule_timer re-arming mid-tick; on_deadline
            // reschedules once at the end.
            {
                auto handle = this->deadlines.handle();
                handle->erase(id);
            }
            continue;
        }

        const auto interval = std::chrono::seconds(match->profile.dynUpdateInterval.value());
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

    // Bound the worker's wait so the destructor join cannot hang on a silent CSMS. MessageTimeout
    // (device-model, may exceed the 60s default) + 10s slack; fall back to the default backstop if
    // the device-model read throws. Resolved on the timer thread; the io_context thread never
    // blocks here.
    std::chrono::seconds worker_wait{DEFAULT_WAIT_FOR_FUTURE_TIMEOUT};
    try {
        const auto msg_timeout =
            this->context.device_model.get_value<int>(ControllerComponentVariables::MessageTimeout);
        worker_wait = std::chrono::seconds(msg_timeout) + std::chrono::seconds(10);
    } catch (const std::exception& e) {
        EVLOG_warning << "K28: MessageTimeout read failed, using " << DEFAULT_WAIT_FOR_FUTURE_TIMEOUT.count()
                      << "s worker bound: " << e.what();
    }

    // Run the response handler on a worker thread so a slow CSMS response cannot block the timer's
    // io_context (and so the response cannot deadlock by re-entering update_tracking on the timer
    // thread). Each handler's future is tracked in pending_pull_response_futures and waited on in
    // the destructor so no in-flight worker dereferences a destroyed object. std::async itself may
    // sync-throw std::system_error on thread/resource exhaustion; same retry-promptly policy.
    std::future<void> response_handler;
    try {
        response_handler =
            std::async(std::launch::async, [this, charging_profile_id, worker_wait, fut = std::move(future)]() mutable {
                try {
                    auto msg = this->wait_for_pull_response(std::move(fut), worker_wait, charging_profile_id);
                    if (!msg.has_value()) {
                        return;
                    }
                    this->apply_pull_response(charging_profile_id, msg.value());
                } catch (const std::exception& e) {
                    EVLOG_error << "K28-worker: pull-response worker for profile " << charging_profile_id
                                << " threw; result dropped, next attempt in one dynUpdateInterval: " << e.what();
                } catch (...) {
                    EVLOG_error << "K28-worker: pull-response worker for profile " << charging_profile_id
                                << " threw unknown exception; result dropped, next attempt in one "
                                   "dynUpdateInterval";
                }
            });
    } catch (const std::exception& e) {
        EVLOG_warning << "PullDynamicScheduleUpdate std::async threw for profile " << charging_profile_id << ": "
                      << e.what();
        return false;
    }

    auto handle = this->pending_pull_response_futures.handle();
    // Compact: drop already-completed futures so the vector does not grow unbounded.
    handle->erase(std::remove_if(handle->begin(), handle->end(),
                                 [](std::future<void>& f) {
                                     return f.valid() &&
                                            f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                 }),
                  handle->end());
    handle->push_back(std::move(response_handler));
    return true;
}

std::optional<ocpp::EnhancedMessage<MessageType>>
DynamicScheduleManager::wait_for_pull_response(std::future<ocpp::EnhancedMessage<MessageType>> fut,
                                               std::chrono::seconds worker_wait, std::int32_t charging_profile_id) {
    ocpp::EnhancedMessage<MessageType> enhanced_message;
    // Wait in slices instead of one blocking wait_for(worker_wait) so the destructor can
    // interrupt a worker stuck on a silent CSMS: teardown_requested is polled each slice.
    constexpr auto wait_slice = std::chrono::milliseconds(200);
    const auto wait_deadline = std::chrono::steady_clock::now() + worker_wait;
    while (true) {
        if (this->teardown_requested.load(std::memory_order_relaxed)) {
            EVLOG_debug << "K28-worker: teardown requested, abandoning PullDynamicScheduleUpdate wait for profile "
                        << charging_profile_id;
            return std::nullopt;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= wait_deadline) {
            EVLOG_warning << "K28: PullDynamicScheduleUpdate response timed out for profile " << charging_profile_id
                          << "; pull skipped, retry next dynUpdateInterval";
            return std::nullopt;
        }
        const auto slice = std::min<std::chrono::steady_clock::duration>(wait_slice, wait_deadline - now);
        if (fut.wait_for(slice) != std::future_status::timeout) {
            break;
        }
    }
    try {
        enhanced_message = fut.get();
    } catch (const std::exception& e) {
        // Transient failure (e.g. websocket drop): no deadline feedback, the profile stays
        // one dynUpdateInterval stale until the next attempt.
        EVLOG_error << "K28-worker: PullDynamicScheduleUpdate future threw for profile " << charging_profile_id
                    << "; pull skipped, next attempt in one dynUpdateInterval: " << e.what();
        return std::nullopt;
    }

    if (enhanced_message.offline) {
        // CSMS unreachable on response: no deadline feedback, profile stays stale.
        EVLOG_error << "K28-worker: PullDynamicScheduleUpdate response offline for profile " << charging_profile_id
                    << "; pull skipped, next attempt in one dynUpdateInterval";
        return std::nullopt;
    }

    if (enhanced_message.messageType != MessageType::PullDynamicScheduleUpdateResponse) {
        EVLOG_warning << "Unexpected response type for PullDynamicScheduleUpdate: " << enhanced_message.messageType;
        return std::nullopt;
    }

    return enhanced_message;
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
    if (!match.has_value()) {
        EVLOG_debug << "Profile " << charging_profile_id << " missing or no longer Dynamic; ignoring pull response.";
        return;
    }

    auto& matched = match.value();
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
            EVLOG_error << "K28-worker: composite recompute callback threw for profile " << charging_profile_id
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
