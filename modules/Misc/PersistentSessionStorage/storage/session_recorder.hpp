// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Correlation of EVerest events into stored session records

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <everest/util/async/monitor.hpp>

#include <generated/types/evse_manager.hpp>
#include <generated/types/ocpp.hpp>
#include <generated/types/session_cost.hpp>

#include "session_store.hpp"

namespace module::storage {

/// \brief Configuration of the recorder
struct RecorderConfig {
    /// \brief Store the signed meter values of transaction start and stop
    bool store_signed_meter_values{false};
};

/// \brief Correlates the session events of one EvseManager into calls on a SessionStoreInterface
class EvseSessionRecorder {
public:
    EvseSessionRecorder() = delete;
    EvseSessionRecorder(SessionStoreInterface& store, RecorderConfig config);

    /// \brief Sets the EVSE the recorded events belong to, once before the first event
    /// \param[in] evse_id - numeric one based EVSE id as reported by get_evse
    /// \param[in] evse_id_string - EVSEID string as reported by get_evse, empty if the EVSE has none
    void set_evse(std::int32_t evse_id, const std::string& evse_id_string);

    /// \brief Processes one session event of the EVSE
    /// \param[in] event - the session event
    void on_session_event(const types::evse_manager::SessionEvent& event);

private:
    /// \brief Correlation state of the EVSE
    struct State {
        std::int32_t evse_id{0};
        std::string evse_id_string{};
        /// \brief Session uuid latched from SessionResumed, consumed by the following
        ///        recovery TransactionFinished that carries an empty uuid
        std::optional<std::string> resumed_session_id{};
    };

    void handle_session_started(State& state, const types::evse_manager::SessionEvent& event);
    void handle_transaction_started(const types::evse_manager::SessionEvent& event);
    void handle_transaction_finished(State& state, const types::evse_manager::SessionEvent& event);

    SessionStoreInterface& m_store;
    const RecorderConfig m_config{};
    everest::lib::util::monitor<State> m_state{};
};

/// \brief Attaches the CSMS assigned transaction id and the OCPP transaction start and
///        stop timestamps to the matching record
/// \param[in] store - the store to update
/// \param[in] event - the OCPP transaction event
void forward_ocpp_transaction_event(SessionStoreInterface& store, const types::ocpp::OcppTransactionEvent& event);

/// \brief Updates the cost information of the matching record, latest wins
/// \param[in] store - the store to update
/// \param[in] cost - the reported session cost
void forward_session_cost(SessionStoreInterface& store, const types::session_cost::SessionCost& cost);

} // namespace module::storage
