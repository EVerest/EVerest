// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef PERSISTENT_SESSION_STORAGE_HPP
#define PERSISTENT_SESSION_STORAGE_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/session_storage/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/evse_manager/Interface.hpp>
#include <generated/interfaces/ocpp/Interface.hpp>
#include <generated/interfaces/session_cost/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <vector>

#include <everest/util/async/monitor.hpp>

#include "storage/session_recorder.hpp"
#include "storage/session_store.hpp"
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string database_path;
    int max_sessions;
    bool store_signed_meter_values;
};

class PersistentSessionStorage : public Everest::ModuleBase {
public:
    PersistentSessionStorage() = delete;
    PersistentSessionStorage(const ModuleInfo& info, std::unique_ptr<session_storageImplBase> p_main,
                             std::vector<std::unique_ptr<evse_managerIntf>> r_evse_manager,
                             std::vector<std::unique_ptr<ocppIntf>> r_ocpp,
                             std::vector<std::unique_ptr<session_costIntf>> r_session_cost, Conf& config) :
        ModuleBase(info),
        p_main(std::move(p_main)),
        r_evse_manager(std::move(r_evse_manager)),
        r_ocpp(std::move(r_ocpp)),
        r_session_cost(std::move(r_session_cost)),
        config(config){};

    const std::unique_ptr<session_storageImplBase> p_main;
    const std::vector<std::unique_ptr<evse_managerIntf>> r_evse_manager;
    const std::vector<std::unique_ptr<ocppIntf>> r_ocpp;
    const std::vector<std::unique_ptr<session_costIntf>> r_session_cost;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    /// \brief Access to the stored session records for the provided implementation
    storage::SessionStoreInterface& store();
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    /// \brief Calls of events received before ready(), in arrival order across all sources
    struct PendingEvents {
        bool started{false};
        std::queue<std::function<void()>> queue{};
    };

    /// \brief Creates one recorder per connected EvseManager and subscribes to all requirements
    void subscribe_all();
    /// \brief Reads the EVSE ids of every recorder via get_evse, they must be distinct
    void resolve_evse_info();
    /// \brief Runs the queued calls in order, then lets new events dispatch directly
    void drain_event_queue();
    /// \brief Queues \p call before ready(), dispatches it directly afterwards
    /// \param[in] call - the bound event handling call
    void enqueue_or_dispatch(std::function<void()> call);

    std::unique_ptr<storage::SessionStore> m_store{};
    /// \brief Publishes m_store to the command handlers, which are registered before init() runs
    std::atomic<bool> m_store_initialized{false};
    std::vector<std::unique_ptr<storage::EvseSessionRecorder>> m_evse_recorders{};
    everest::lib::util::monitor<PendingEvents> m_pending_events{};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // PERSISTENT_SESSION_STORAGE_HPP
