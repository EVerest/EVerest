// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest

#include "DataStore.hpp"
#include "GenericInfoStore.hpp"
#include "SessionInfo.hpp"
#include <everest/logging.hpp>
#include <sstream>
#include <string_view>

namespace data {

static bool almost_equal(float a, float b, float epsilon = std::numeric_limits<float>::epsilon() * 100) {
    return std::fabs(a - b) <= epsilon * std::fmax(1.0f, std::fmax(std::fabs(a), std::fabs(b)));
}

namespace {

constexpr std::array<std::string_view, NUMBER_OF_EVSE_STATUS_FIELDS> evse_status_field_names{
    "active_connector_index", "charging_allowed",     "state",
    "error_present",          "charge_protocol",      "charging_duration_s",
    "charged_energy_wh",      "discharged_energy_wh", "available"};

static_assert(evse_status_field_names.size() == NUMBER_OF_EVSE_STATUS_FIELDS,
              "evse_status_field_names size should be in sync with EVSEStatusField enum definition");

} // namespace

// we currently don't get this info from the system yet, so allow setting to unknown
void ChargerInfoStore::set_unknown() {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->dataobj.vendor = "unknown";
    this->dataobj.model = "unknown";
    this->dataobj.serial = "unknown";
    this->dataobj.firmware_version = "unknown";
    // pretend we got something
    this->data_is_valid = true;
}

void ChargerErrorsStore::add_error(const types::json_rpc_api::ErrorObj& error) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // Check if the error already exists in the vector
    for (const auto& existing_error : this->dataobj) {
        if (existing_error.uuid == error.uuid) {
            // Error already exists, no need to add it again
            throw std::runtime_error("Error with UUID " + error.uuid + " already exists in the store.");
        }
    }
    this->dataobj.push_back(error);
    this->data_is_valid = true; // set the data as valid, since we have a valid error now
    data_lock.unlock();
    // Notify that data has changed
    this->notify_data_changed();
}

void ChargerErrorsStore::clear_error(const types::json_rpc_api::ErrorObj& error) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // Find and remove the error from the vector
    for (auto it = this->dataobj.begin(); it != this->dataobj.end(); ++it) {
        // String comparison for uuid
        if (it->uuid == error.uuid) {
            this->dataobj.erase(it);
            data_lock.unlock();
            // Notify that data has changed
            this->notify_data_changed();
            return; // Exit after removing the first matching error
        }
    }
}

void EVSEInfoStore::set_supported_energy_transfer_modes(
    const std::vector<types::json_rpc_api::EnergyTransferModeEnum>& supported_energy_transfer_modes) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->dataobj.supported_energy_transfer_modes = supported_energy_transfer_modes;
}
void EVSEInfoStore::set_index(int32_t index) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->dataobj.index = index;
}
void EVSEInfoStore::set_id(const std::string& id) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->dataobj.id = id;
    this->data_is_valid = true; // set the data as valid, since we have a valid id now
}
void EVSEInfoStore::set_available_connectors(const std::vector<RPCDataTypes::ConnectorInfoObj>& connectors) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->dataobj.available_connectors = connectors;
}

void EVSEInfoStore::set_available_connector(types::json_rpc_api::ConnectorInfoObj& available_connector) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // Iterate through the vector and set the connector with the given index
    for (auto& connector : this->dataobj.available_connectors) {
        if (connector.index == available_connector.index) {
            connector = available_connector; // Update the existing connector
            return;
        }
    }
    // If the connector with the given index is not found, add it to the vector
    this->dataobj.available_connectors.push_back(available_connector);
}

bool EVSEInfoStore::get_is_ac_transfer_mode() const {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    return this->is_ac_transfer_mode;
}

int32_t EVSEInfoStore::get_index() {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    return this->dataobj.index;
}

void EVSEInfoStore::set_is_ac_transfer_mode(bool is_ac) {
    this->is_ac_transfer_mode = is_ac;
}

std::vector<types::json_rpc_api::ConnectorInfoObj> EVSEInfoStore::get_available_connectors() const {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    return this->dataobj.available_connectors;
}

void EVSEStatusStore::update_data_is_valid() {
    // shall be called only when a lock on this->data_mutex is held
    if (this->data_is_valid) {
        return; // No need to update if data is already valid
    }

    std::ostringstream missing_fields;
    bool has_missing_fields{false};

    for (size_t i = 0; i < this->field_status.size(); ++i) {
        if (!this->field_status.test(i)) {
            if (has_missing_fields) {
                missing_fields << ',';
            }
            missing_fields << evse_status_field_names.at(i);
            has_missing_fields = true;
        }
    }

    this->data_is_valid = !has_missing_fields;
    if (has_missing_fields) {
        EVLOG_debug << "EVSEStatusStore: Missing fields [" << missing_fields.str() << "], data is invalid.";
    } else {
        EVLOG_debug << "EVSEStatusStore: All required fields are set, data is now valid.";
    }
}

void EVSEStatusStore::set_field_status(EVSEStatusField field) {
    this->field_status.set(to_underlying_value(field));
}

void EVSEStatusStore::set_ac_charge_param_evse_max_current(float current_limit) {
    std::unique_lock<std::mutex> cv_lock(mtx_current_limit_applied);

    // current_limit with 0 is not valid and means internally that no energy is
    // available. The energy available state is already notified via the EVSE state, thus it
    // is not necessary to forward current_limit=0 to the API clients
    if (current_limit == 0.0f) {
        return;
    }
    this->configured_current_limit = current_limit;

    // Check if a new current limit is requested from the API
    if (this->requested_current_limit != 0.0f) {
        // Check if the requested limit is applied
        this->cv_current_limit_applied.notify_all();
        // We are skipping applying the new current limit, as long as a new limit is requested from the API and not yet
        // applied
        return;
    }
    // Apply the new current limit
    EVLOG_debug << "Applying new current limit: " << this->configured_current_limit;
    this->set_ac_charge_param_evse_current_limit_internal(this->configured_current_limit);
}

bool EVSEStatusStore::wait_until_current_limit_applied(float requested_limit, std::chrono::milliseconds timeout_ms) {
    std::unique_lock<std::mutex> lock(mtx_current_limit_applied);
    bool is_current_limit_applied{false};
    this->requested_current_limit = requested_limit;
    if (this->cv_current_limit_applied.wait_for(lock, timeout_ms, [this] {
            return almost_equal(this->configured_current_limit, this->requested_current_limit);
        })) {
        this->requested_current_limit = 0.0f; // reset the request
        is_current_limit_applied = true;
        EVLOG_debug << "Current limit applied: " << this->configured_current_limit
                    << " (requested: " << this->requested_current_limit << ")";
    } else {
        EVLOG_debug << "timed out waiting for current limit to be applied, configured: "
                    << this->configured_current_limit << ", requested: " << this->requested_current_limit;
        // If there is already a new limit configured, notify it now
        this->requested_current_limit = 0.0f; // reset the request
        set_ac_charge_param_evse_current_limit_internal(this->configured_current_limit);
    }

    return is_current_limit_applied;
}

void EVSEStatusStore::set_ac_charge_param_evse_max_phase_count(int32_t phase_count) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    auto& ac_charge_param = this->dataobj.ac_charge_param;

    if (!ac_charge_param.has_value()) {
        ac_charge_param.emplace();
    }

    auto& evse_phase_count = ac_charge_param.value().evse_max_phase_count;

    if (evse_phase_count != phase_count) {
        evse_phase_count = phase_count;
        data_lock.unlock();
        this->notify_data_changed();
    }
}

void EVSEStatusStore::set_ac_charge_param_evse_current_limit_internal(float max_current) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    auto& ac_charge_param = this->dataobj.ac_charge_param;

    if (!ac_charge_param.has_value()) {
        ac_charge_param.emplace();
    }

    auto& evse_max_current = ac_charge_param.value().evse_max_current;

    if (!almost_equal(evse_max_current, max_current)) {
        evse_max_current = max_current;
        data_lock.unlock();
        this->notify_data_changed();
    }
}

EVSEStatusStore::EVSEStatusStore() {
    // Initialize data store with default values
    this->set_charging_duration_s(0);
    this->set_charged_energy_wh(0.0f);
    this->set_discharged_energy_wh(0.0f);
    this->set_error_present(false);
    this->set_charging_allowed(true);
    this->set_available(true);
}

// Example set method using the enum
void EVSEStatusStore::set_active_connector_index(int32_t active_connector_index) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex); // check if data has changed
    this->set_field_status(EVSEStatusField::ActiveConnectorIndex);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.active_connector_index != active_connector_index) {
        this->dataobj.active_connector_index = active_connector_index;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the charging allowed flag
void EVSEStatusStore::set_charging_allowed(bool charging_allowed) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::ChargingAllowed);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.charging_allowed != charging_allowed) {
        this->dataobj.charging_allowed = charging_allowed;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the EVSE state
void EVSEStatusStore::set_state(types::json_rpc_api::EVSEStateEnum state) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::State);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.state != state) {
        this->dataobj.state = state;
        data_lock.unlock();
        this->notify_data_changed();
    }
}

// set EVSE errors
void EVSEStatusStore::set_error_present(const bool error_present) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::ErrorPresent);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.error_present != error_present) {
        this->dataobj.error_present = error_present;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the charge protocol
void EVSEStatusStore::set_charge_protocol(types::json_rpc_api::ChargeProtocolEnum charge_protocol) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::ChargeProtocol);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.charge_protocol != charge_protocol) {
        this->dataobj.charge_protocol = charge_protocol;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the charging duration in seconds
void EVSEStatusStore::set_charging_duration_s(int32_t charging_duration_s) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::ChargingDurationS);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.charging_duration_s != charging_duration_s) {
        this->dataobj.charging_duration_s = charging_duration_s;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the charged energy in Wh
void EVSEStatusStore::set_charged_energy_wh(float charged_energy_wh) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::ChargedEnergyWh);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.charged_energy_wh != charged_energy_wh) {
        this->dataobj.charged_energy_wh = charged_energy_wh;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the discharged energy in Wh
void EVSEStatusStore::set_discharged_energy_wh(float discharged_energy_wh) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::DischargedEnergyWh);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.discharged_energy_wh != discharged_energy_wh) {
        this->dataobj.discharged_energy_wh = discharged_energy_wh;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the available flag
void EVSEStatusStore::set_available(bool available) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    this->set_field_status(EVSEStatusField::Available);
    this->update_data_is_valid();
    // check if data has changed
    if (this->dataobj.available != available) {
        this->dataobj.available = available;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the AC charge parameters
void EVSEStatusStore::set_ac_charge_param(const std::optional<RPCDataTypes::ACChargeParametersObj>& ac_charge_param) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // check if data has changed
    if (this->dataobj.ac_charge_param != ac_charge_param) {
        this->dataobj.ac_charge_param = ac_charge_param;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the DC charge parameters
void EVSEStatusStore::set_dc_charge_param(const std::optional<RPCDataTypes::DCChargeParametersObj>& dc_charge_param) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // check if data has changed
    if (this->dataobj.dc_charge_param != dc_charge_param) {
        this->dataobj.dc_charge_param = dc_charge_param;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the AC charge status
void EVSEStatusStore::set_ac_charge_status(const std::optional<RPCDataTypes::ACChargeStatusObj>& ac_charge_status) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // check if data has changed
    if (this->dataobj.ac_charge_status != ac_charge_status) {
        this->dataobj.ac_charge_status = ac_charge_status;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the DC charge status
void EVSEStatusStore::set_dc_charge_status(const std::optional<RPCDataTypes::DCChargeStatusObj>& dc_charge_status) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // check if data has changed
    if (this->dataobj.dc_charge_status != dc_charge_status) {
        this->dataobj.dc_charge_status = dc_charge_status;
        data_lock.unlock();
        this->notify_data_changed();
    }
}
// set the display parameters
void EVSEStatusStore::set_display_parameters(
    const std::optional<RPCDataTypes::DisplayParametersObj>& display_parameters) {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    // check if data has changed
    if (this->dataobj.display_parameters != display_parameters) {
        this->dataobj.display_parameters = display_parameters;
        data_lock.unlock();
        this->notify_data_changed();
    }
}

types::json_rpc_api::EVSEStateEnum EVSEStatusStore::get_state() const {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    return this->dataobj.state;
}

std::optional<RPCDataTypes::ACChargeParametersObj> EVSEStatusStore::get_ac_charge_param() {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    return this->dataobj.ac_charge_param;
}

std::optional<RPCDataTypes::DCChargeParametersObj> EVSEStatusStore::get_dc_charge_param() {
    std::unique_lock<std::mutex> data_lock(this->data_mutex);
    return this->dataobj.dc_charge_param;
}

} // namespace data
