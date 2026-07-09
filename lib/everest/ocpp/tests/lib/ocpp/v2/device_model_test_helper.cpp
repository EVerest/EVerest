// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "device_model_test_helper.hpp"

#include <everest/database/sqlite/connection.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

using namespace everest::db;
using namespace everest::db::sqlite;

namespace ocpp::v2 {
DeviceModelTestHelper::DeviceModelTestHelper(const std::string& database_path, const std::string& migration_files_path,
                                             const std::string& config_path, bool seed_der_ctrlr_enabled) :
    database_path(database_path),
    migration_files_path(migration_files_path),
    config_path(config_path),
    seed_der_ctrlr_enabled(seed_der_ctrlr_enabled),
    database_connection(std::make_unique<everest::db::sqlite::Connection>(database_path)) {
    this->database_connection->open_connection();
    this->device_model = create_device_model();
}

DeviceModel* DeviceModelTestHelper::get_device_model() {
    if (this->device_model == nullptr) {
        return nullptr;
    }

    return this->device_model.get();
}

bool DeviceModelTestHelper::remove_variable_from_db(const std::string& component_name,
                                                    const std::optional<std::string>& component_instance,
                                                    const std::optional<std::uint32_t>& evse_id,
                                                    const std::optional<std::uint32_t>& connector_id,
                                                    const std::string& variable_name,
                                                    const std::optional<std::string>& variable_instance) {
    const std::string delete_query = "DELETE FROM VARIABLE WHERE ID = "
                                     "(SELECT ID FROM VARIABLE WHERE COMPONENT_ID = "
                                     "(SELECT ID FROM COMPONENT WHERE NAME = ? AND INSTANCE IS ? AND "
                                     "EVSE_ID IS ? AND CONNECTOR_ID IS ?) "
                                     "AND NAME = ? AND INSTANCE IS ?)";

    auto delete_stmt = this->database_connection->new_statement(delete_query);
    delete_stmt->bind_text(1, component_name, SQLiteString::Transient);
    if (component_instance.has_value()) {
        delete_stmt->bind_text(2, component_instance.value(), SQLiteString::Transient);
    } else {
        delete_stmt->bind_null(2);
    }
    if (evse_id.has_value()) {
        delete_stmt->bind_int(3, evse_id.value());
        if (connector_id.has_value()) {
            delete_stmt->bind_int(4, connector_id.value());
        } else {
            delete_stmt->bind_null(4);
        }
    } else {
        delete_stmt->bind_null(3);
        delete_stmt->bind_null(4);
    }

    delete_stmt->bind_text(5, variable_name, SQLiteString::Transient);
    if (variable_instance.has_value()) {
        delete_stmt->bind_text(6, variable_instance.value(), SQLiteString::Transient);
    } else {
        delete_stmt->bind_null(6);
    }

    if (delete_stmt->step() != SQLITE_DONE) {
        EVLOG_error << this->database_connection->get_error_message();
        return false;
    }

    this->device_model = create_device_model(false);
    return true;
}

bool DeviceModelTestHelper::update_variable_characteristics(const VariableCharacteristics& characteristics,
                                                            const std::string& component_name,
                                                            const std::optional<std::string>& component_instance,
                                                            const std::optional<std::uint32_t>& evse_id,
                                                            const std::optional<std::uint32_t>& connector_id,
                                                            const std::string& variable_name,
                                                            const std::optional<std::string>& variable_instance) {
    const std::string update_query =
        "UPDATE VARIABLE_CHARACTERISTICS SET DATATYPE_ID=@datatype_id, MAX_LIMIT=@max_limit, "
        "MIN_LIMIT=@min_limit, SUPPORTS_MONITORING=@supports_monitoring, UNIT=@unit, VALUES_LIST=@values_list WHERE "
        "VARIABLE_ID=(SELECT ID FROM VARIABLE WHERE COMPONENT_ID = "
        "(SELECT ID FROM COMPONENT WHERE NAME = @component_name AND INSTANCE IS @component_instance AND "
        "EVSE_ID IS @evse_id AND CONNECTOR_ID IS @connector_id) "
        "AND NAME = @variable_name AND INSTANCE IS @variable_instance)";

    std::unique_ptr<StatementInterface> update_statement;
    try {
        update_statement = this->database_connection->new_statement(update_query);
    } catch (const QueryExecutionException&) {
        throw InitDeviceModelDbError("Could not create statement " + update_query);
    }

    update_statement->bind_int("@datatype_id", static_cast<int>(characteristics.dataType));

    const uint8_t supports_monitoring = (characteristics.supportsMonitoring ? 1 : 0);
    update_statement->bind_int("@supports_monitoring", supports_monitoring);

    if (characteristics.unit.has_value()) {
        update_statement->bind_text("@unit", characteristics.unit.value(), SQLiteString::Transient);
    } else {
        update_statement->bind_null("@unit");
    }

    if (characteristics.valuesList.has_value()) {
        update_statement->bind_text("@values_list", characteristics.valuesList.value(), SQLiteString::Transient);
    } else {
        update_statement->bind_null("@values_list");
    }

    if (characteristics.maxLimit.has_value()) {
        update_statement->bind_double("@max_limit", static_cast<double>(characteristics.maxLimit.value()));
    } else {
        update_statement->bind_null("@max_limit");
    }

    if (characteristics.minLimit.has_value()) {
        update_statement->bind_double("@min_limit", static_cast<double>(characteristics.minLimit.value()));
    } else {
        update_statement->bind_null("@min_limit");
    }

    update_statement->bind_text("@component_name", component_name, SQLiteString::Transient);
    if (component_instance.has_value()) {
        update_statement->bind_text("@component_instance", component_instance.value(), SQLiteString::Transient);
    } else {
        update_statement->bind_null("@component_instance");
    }
    if (evse_id.has_value()) {
        update_statement->bind_int("@evse_id", evse_id.value());
        if (connector_id.has_value()) {
            update_statement->bind_int("@connector_id", connector_id.value());
        } else {
            update_statement->bind_null("@connector_id");
        }
    } else {
        update_statement->bind_null("@evse_id");
        update_statement->bind_null("@connector_id");
    }

    update_statement->bind_text("@variable_name", variable_name, SQLiteString::Transient);
    if (variable_instance.has_value()) {
        update_statement->bind_text("@variable_instance", variable_instance.value(), SQLiteString::Transient);
    } else {
        update_statement->bind_null("@variable_instance");
    }

    if (update_statement->step() != SQLITE_DONE) {
        return false;
    }

    this->device_model = create_device_model(false);
    return true;
}

bool DeviceModelTestHelper::set_variable_attribute_value_null(const std::string& component_name,
                                                              const std::optional<std::string>& component_instance,
                                                              const std::optional<std::uint32_t>& evse_id,
                                                              const std::optional<std::uint32_t>& connector_id,
                                                              const std::string& variable_name,
                                                              const std::optional<std::string>& variable_instance,
                                                              const AttributeEnum& attribute_enum) {
    std::string update_query =
        "UPDATE VARIABLE_ATTRIBUTE SET VALUE=NULL WHERE VARIABLE_ID="
        "(SELECT ID FROM VARIABLE WHERE COMPONENT_ID = "
        "(SELECT ID FROM COMPONENT WHERE NAME = @component_name AND INSTANCE IS @component_instance AND "
        "EVSE_ID IS @evse_id AND CONNECTOR_ID IS @connector_id) "
        "AND NAME = @variable_name AND INSTANCE IS @variable_instance) "
        "AND TYPE_ID=@type_id";
    auto update_statement = this->database_connection->new_statement(update_query);

    update_statement->bind_text("@component_name", component_name, SQLiteString::Transient);
    if (component_instance.has_value()) {
        update_statement->bind_text("@component_instance", component_instance.value(), SQLiteString::Transient);
    } else {
        update_statement->bind_null("@component_instance");
    }
    if (evse_id.has_value()) {
        update_statement->bind_int("@evse_id", evse_id.value());
        if (connector_id.has_value()) {
            update_statement->bind_int("@connector_id", connector_id.value());
        } else {
            update_statement->bind_null("@connector_id");
        }
    } else {
        update_statement->bind_null("@evse_id");
        update_statement->bind_null("@connector_id");
    }

    update_statement->bind_text("@variable_name", variable_name, SQLiteString::Transient);
    if (variable_instance.has_value()) {
        update_statement->bind_text("@variable_instance", variable_instance.value(), SQLiteString::Transient);
    } else {
        update_statement->bind_null("@variable_instance");
    }

    update_statement->bind_int("@type_id", static_cast<int>(attribute_enum));

    if (update_statement->step() != SQLITE_DONE) {
        return false;
    }

    return true;
}

void DeviceModelTestHelper::create_device_model_db() {
    InitDeviceModelDb db(this->database_path, this->migration_files_path);
    auto component_configs = get_all_component_configs(this->config_path);

    DeviceModelVariable supported_operation_modes;
    supported_operation_modes.name = "SupportedOperationModes";
    VariableCharacteristics characteristics;
    characteristics.dataType = DataEnum::MemberList;
    characteristics.supportsMonitoring = false;
    supported_operation_modes.characteristics = characteristics;
    DbVariableAttribute supported_operation_modes_attribute;
    supported_operation_modes_attribute.variable_attribute.mutability = MutabilityEnum::ReadWrite;
    supported_operation_modes_attribute.variable_attribute.value =
        "ChargingOnly,CentralSetpoint,ExternalSetpoint,ExternalLimits,CentralFrequency,LocalFrequency,"
        "LocalLoadBalancing,Idle";
    supported_operation_modes_attribute.variable_attribute.type = AttributeEnum::Actual;
    supported_operation_modes.attributes = std::vector<DbVariableAttribute>{supported_operation_modes_attribute};
    const std::vector<DeviceModelVariable> v2x_variables{supported_operation_modes};
    for (const std::int32_t evse_id : {1, 2}) {
        ComponentKey v2x_ctrl;
        v2x_ctrl.name = "V2XChargingCtrlr";
        v2x_ctrl.evse_id = evse_id;
        component_configs.insert(std::make_pair(v2x_ctrl, v2x_variables));
    }

    if (this->seed_der_ctrlr_enabled) {
        DeviceModelVariable der_enabled;
        der_enabled.name = "Enabled";
        VariableCharacteristics der_enabled_characteristics;
        der_enabled_characteristics.dataType = DataEnum::boolean;
        der_enabled_characteristics.supportsMonitoring = false;
        der_enabled.characteristics = der_enabled_characteristics;
        DbVariableAttribute der_enabled_attribute;
        der_enabled_attribute.variable_attribute.mutability = MutabilityEnum::ReadWrite;
        der_enabled_attribute.variable_attribute.value = "true";
        der_enabled_attribute.variable_attribute.type = AttributeEnum::Actual;
        der_enabled.attributes = std::vector<DbVariableAttribute>{der_enabled_attribute};
        for (auto& [component_key, variables] : component_configs) {
            if ((component_key.name == "DCDERCtrlr" || component_key.name == "ACDERCtrlr") &&
                component_key.evse_id == 1) {
                variables.push_back(der_enabled);
            }
        }
    }

    db.initialize_database(component_configs, true);
}

std::unique_ptr<DeviceModel> DeviceModelTestHelper::create_device_model(const bool init) {
    if (init) {
        create_device_model_db();
    }
    auto device_model_storage = std::make_unique<DeviceModelStorageSqlite>(this->database_path);
    auto dm = std::make_unique<DeviceModel>(std::move(device_model_storage));

    return dm;
}
} // namespace ocpp::v2
