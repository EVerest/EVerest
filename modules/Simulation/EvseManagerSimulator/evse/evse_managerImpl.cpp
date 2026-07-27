// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "evse_managerImpl.hpp"

namespace module {
namespace evse {

void evse_managerImpl::init() {
}

void evse_managerImpl::ready() {
    types::evse_manager::SessionEvent se;
    se.event = types::evse_manager::SessionEventEnum::Enabled;
    se.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    publish_session_event(se);
    publish_waiting_for_external_ready(true);
}

types::evse_manager::Evse evse_managerImpl::handle_get_evse() {
    types::evse_manager::Evse evse;
    evse.id = 1;

    types::evse_manager::Connector connector;
    connector.id = 1;

    if (const auto& mapping = get_mapping(); mapping.has_value()) {
        evse.id = mapping->evse;
        if (mapping->connector.has_value()) {
            connector.id = mapping->connector.value();
        }
    }

    evse.connectors = {connector};
    return evse;
}

bool evse_managerImpl::handle_enable_disable(int& connector_id, types::evse_manager::EnableDisableSource& cmd_source) {
    // your code for cmd enable_disable goes here
    return true;
}

void evse_managerImpl::handle_authorize_response(types::authorization::ProvidedIdToken& provided_token,
                                                 types::authorization::ValidationResult& validation_result) {
    // your code for cmd authorize_response goes here
}

void evse_managerImpl::handle_withdraw_authorization() {
    // your code for cmd withdraw_authorization goes here
}

bool evse_managerImpl::handle_reserve(int& reservation_id) {
    // your code for cmd reserve goes here
    return true;
}

void evse_managerImpl::handle_cancel_reservation() {
    // your code for cmd cancel_reservation goes here
}

bool evse_managerImpl::handle_pause_charging() {
    // your code for cmd pause_charging goes here
    return true;
}

bool evse_managerImpl::handle_resume_charging() {
    // your code for cmd resume_charging goes here
    return true;
}

bool evse_managerImpl::handle_stop_transaction(types::evse_manager::StopTransactionRequest& request) {
    // your code for cmd stop_transaction goes here
    return true;
}

bool evse_managerImpl::handle_force_unlock(int& connector_id) {
    // your code for cmd force_unlock goes here
    return true;
}

bool evse_managerImpl::handle_external_ready_to_start_charging() {
    publish_ready(true);
    return true;
}

void evse_managerImpl::handle_set_plug_and_charge_configuration(
    types::evse_manager::PlugAndChargeConfiguration& plug_and_charge_configuration) {
    // your code for cmd set_plug_and_charge_configuration goes here
}

types::evse_manager::UpdateAllowedEnergyTransferModesResult
evse_managerImpl::handle_update_allowed_energy_transfer_modes(
    std::vector<types::iso15118::EnergyTransferMode>& allowed_energy_transfer_modes) {
    // your code for cmd update_allowed_energy_transfer_modes goes here
    return types::evse_manager::UpdateAllowedEnergyTransferModesResult::NoHlc;
}

types::evse_manager::SetDerAvailableResult evse_managerImpl::handle_set_der_available(bool& available) {
    // your code for cmd set_der_available goes here
    return types::evse_manager::SetDerAvailableResult::Accepted;
}

} // namespace evse
} // namespace module
