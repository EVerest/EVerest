// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "session_recorder.hpp"

#include <utility>

#include <everest/logging.hpp>

#include "record_conversions.hpp"

namespace module::storage {

namespace {

constexpr std::int32_t DEFAULT_CONNECTOR_ID = 1;

} // namespace

EvseSessionRecorder::EvseSessionRecorder(SessionStoreInterface& store, RecorderConfig config) :
    m_store(store), m_config(config) {
}

void EvseSessionRecorder::set_evse(std::int32_t evse_id, const std::string& evse_id_string) {
    auto state = m_state.handle();
    state->evse_id = evse_id;
    state->evse_id_string = evse_id_string;
}

void EvseSessionRecorder::on_session_event(const types::evse_manager::SessionEvent& event) {
    auto state = m_state.handle();

    if (state->evse_id == 0) {
        EVLOG_error << "Dropping session event of session " << event.uuid << " received before the EVSE is known";
        return;
    }

    switch (event.event) {
    case types::evse_manager::SessionEventEnum::SessionStarted:
        handle_session_started(*state, event);
        break;
    case types::evse_manager::SessionEventEnum::TransactionStarted:
        handle_transaction_started(event);
        break;
    case types::evse_manager::SessionEventEnum::TransactionFinished:
        handle_transaction_finished(*state, event);
        break;
    case types::evse_manager::SessionEventEnum::SessionFinished:
        m_store.store_session_finished(event.uuid, event.timestamp);
        break;
    case types::evse_manager::SessionEventEnum::SessionResumed:
        state->resumed_session_id = event.uuid;
        break;
    default:
        break;
    }
}

void EvseSessionRecorder::handle_session_started(State& state, const types::evse_manager::SessionEvent& event) {
    if (not event.session_started.has_value()) {
        EVLOG_error << "Dropping SessionStarted event of session " << event.uuid << " without payload";
        return;
    }

    SessionStart session{};
    session.session_id = event.uuid;
    session.evse_id = state.evse_id;
    session.evse_id_string = state.evse_id_string;
    session.connector_id = event.connector_id.value_or(DEFAULT_CONNECTOR_ID);
    session.timestamp_start = event.timestamp;
    session.start_reason = event.session_started->reason;

    m_store.store_session_started(session);
}

void EvseSessionRecorder::handle_transaction_started(const types::evse_manager::SessionEvent& event) {
    if (not event.transaction_started.has_value()) {
        EVLOG_error << "Dropping TransactionStarted event of session " << event.uuid << " without payload";
        return;
    }
    const auto& payload = event.transaction_started.value();

    TransactionStart transaction{};
    transaction.session_id = event.uuid;
    transaction.timestamp_start = event.timestamp;
    transaction.energy_Wh_import_start = payload.meter_value.energy_Wh_import.total;
    transaction.id_token_hash = compute_id_token_hash(payload.id_tag);
    transaction.id_token_type = payload.id_tag.id_token.type;
    transaction.authorization_type = payload.id_tag.authorization_type;
    if (m_config.store_signed_meter_values) {
        transaction.signed_meter_value_start = payload.signed_meter_value;
    }

    m_store.store_transaction_started(transaction);
}

void EvseSessionRecorder::handle_transaction_finished(State& state, const types::evse_manager::SessionEvent& event) {
    if (not event.transaction_finished.has_value()) {
        EVLOG_error << "Dropping TransactionFinished event of session " << event.uuid << " without payload";
        return;
    }
    const auto& payload = event.transaction_finished.value();

    TransactionFinish finish{};
    finish.session_id = event.uuid;
    if (finish.session_id.empty()) {
        // The recovery finish after a power loss carries no uuid, only the preceding
        // SessionResumed identifies the session, and no SessionFinished follows it
        if (not state.resumed_session_id.has_value()) {
            EVLOG_warning << "Dropping TransactionFinished event without session id on EVSE " << state.evse_id
                          << ", no resumed session is pending";
            return;
        }
        finish.session_id = std::move(state.resumed_session_id.value());
        state.resumed_session_id.reset();
        finish.closes_session = true;
    }

    finish.timestamp_stop = event.timestamp;
    finish.energy_Wh_import_stop = payload.meter_value.energy_Wh_import.total;
    finish.stop_reason = payload.reason;
    if (m_config.store_signed_meter_values) {
        finish.signed_meter_value_stop = payload.signed_meter_value;
        finish.signed_meter_value_start = payload.start_signed_meter_value;
    }

    m_store.store_transaction_finished(finish);
}

void forward_ocpp_transaction_event(SessionStoreInterface& store, const types::ocpp::OcppTransactionEvent& event) {
    OcppTransactionEvent transaction_event{};
    transaction_event.session_id = event.session_id;
    transaction_event.transaction_event = event.transaction_event;
    transaction_event.transaction_id = event.transaction_id;
    transaction_event.timestamp = event.timestamp;

    store.store_ocpp_transaction_event(transaction_event);
}

void forward_session_cost(SessionStoreInterface& store, const types::session_cost::SessionCost& cost) {
    store.update_cost(cost.session_id, strip_id_tag(cost));
}

} // namespace module::storage
