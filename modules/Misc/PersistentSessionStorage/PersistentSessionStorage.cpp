// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "PersistentSessionStorage.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <everest/exceptions.hpp>
#include <everest/logging.hpp>
#include <utils/exceptions.hpp>

namespace module {

void PersistentSessionStorage::init() {
    m_store =
        std::make_unique<storage::SessionStore>(std::make_unique<everest::db::sqlite::Connection>(config.database_path),
                                                std::filesystem::path(info.paths.share) / "migrations",
                                                SESSION_STORAGE_MIGRATION_FILE_VERSION, config.max_sessions);

    if (!m_store->open()) {
        // Deliberately fatal, records would silently be lost otherwise
        EVLOG_AND_THROW(
            Everest::EverestConfigError("Could not open the session storage database at " + config.database_path));
    }
    m_store_initialized.store(true, std::memory_order_release);

    subscribe_all();
    invoke_init(*p_main);
}

void PersistentSessionStorage::ready() {
    invoke_ready(*p_main);
    resolve_evse_info();
    drain_event_queue();
}

storage::SessionStoreInterface& PersistentSessionStorage::store() {
    // Also the release/acquire pair publishing m_store to the handler threads
    if (not m_store_initialized.load(std::memory_order_acquire)) {
        throw Everest::NotReady("The session storage is not initialized yet");
    }
    return *m_store;
}

void PersistentSessionStorage::subscribe_all() {
    m_evse_recorders.reserve(r_evse_manager.size());
    for (const auto& evse_manager : r_evse_manager) {
        m_evse_recorders.push_back(std::make_unique<storage::EvseSessionRecorder>(
            *m_store, storage::RecorderConfig{config.store_signed_meter_values}));
        evse_manager->subscribe_session_event(
            [this, recorder = m_evse_recorders.back().get()](types::evse_manager::SessionEvent event) {
                enqueue_or_dispatch([recorder, event = std::move(event)] { recorder->on_session_event(event); });
            });
    }
    for (const auto& ocpp : r_ocpp) {
        ocpp->subscribe_ocpp_transaction_event([this](types::ocpp::OcppTransactionEvent event) {
            enqueue_or_dispatch(
                [this, event = std::move(event)] { storage::forward_ocpp_transaction_event(*m_store, event); });
        });
    }
    for (const auto& session_cost : r_session_cost) {
        session_cost->subscribe_session_cost([this](types::session_cost::SessionCost cost) {
            enqueue_or_dispatch([this, cost = std::move(cost)] { storage::forward_session_cost(*m_store, cost); });
        });
    }
}

void PersistentSessionStorage::resolve_evse_info() {
    std::vector<std::int32_t> evse_ids{};
    evse_ids.reserve(r_evse_manager.size());
    for (std::size_t index = 0; index < r_evse_manager.size(); ++index) {
        const auto evse = r_evse_manager.at(index)->call_get_evse();
        // Marking older records of an EVSE Stale relies on distinct EVSE ids
        if (std::find(evse_ids.begin(), evse_ids.end(), evse.id) != evse_ids.end()) {
            EVLOG_AND_THROW(Everest::EverestConfigError("Connected EvseManagers must have distinct evse ids, id " +
                                                        std::to_string(evse.id) + " occurs more than once"));
        }
        evse_ids.push_back(evse.id);
        m_evse_recorders.at(index)->set_evse(evse.id, evse.evse_id.value_or(""));
    }
}

void PersistentSessionStorage::enqueue_or_dispatch(std::function<void()> call) {
    auto pending_events = m_pending_events.handle();
    if (not pending_events->started) {
        pending_events->queue.push(std::move(call));
        return;
    }
    call();
}

void PersistentSessionStorage::drain_event_queue() {
    auto pending_events = m_pending_events.handle();
    while (not pending_events->queue.empty()) {
        pending_events->queue.front()();
        pending_events->queue.pop();
    }
    pending_events->started = true;
}

} // namespace module
