// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <sqlite3.h>

#include <ocpp/common/constants.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelStorageSQLiteTest : public ::testing::Test {
protected:
    const std::string DATABASE_PATH = "file::memory:?cache=shared";
    const std::string MIGRATION_FILES_PATH = "./resources/v2/device_model_migration_files";
    const std::string CONFIGS_PATH = "./resources/config/v2/component_config";
    DeviceModelTestHelper device_model_test_helper;

public:
    DeviceModelStorageSQLiteTest() : device_model_test_helper(DATABASE_PATH, MIGRATION_FILES_PATH, CONFIGS_PATH) {
    }
};

/// \brief Tests check_integrity does not raise error for valid database
TEST_F(DeviceModelStorageSQLiteTest, test_check_integrity_valid) {
    DeviceModelStorageSqlite dm(DATABASE_PATH);

    EXPECT_NO_THROW(dm.check_integrity());
}

/// \brief Tests check_integrity raises exception for invalid database
TEST_F(DeviceModelStorageSQLiteTest, test_check_integrity_invalid) {

    device_model_test_helper.remove_variable_from_db("DisplayMessageCtrlr", std::nullopt, std::nullopt, std::nullopt,
                                                     "NumberOfDisplayMessages", std::nullopt);

    DeviceModelStorageSqlite dm(DATABASE_PATH);

    EXPECT_NO_THROW(dm.check_integrity());
}

/// \brief A variable is looked up by name and instance, both matched case-insensitively
TEST_F(DeviceModelStorageSQLiteTest, test_set_variable_attribute_value_variable_case_insensitive) {
    DeviceModelStorageSqlite dm(DATABASE_PATH);

    EVSE evse;
    evse.id = 2;
    evse.connectorId = 3;

    Component component;
    component.name = "UnitTestCtrlr";
    component.instance = "UnitTestInstance";
    component.evse = evse;

    Variable miscased_variable;
    miscased_variable.name = "UNITTESTPROPERTYBNAME";
    miscased_variable.instance = "UNITTESTINSTANCE";

    EXPECT_EQ(dm.set_variable_attribute_value(component, miscased_variable, AttributeEnum::Actual, "false", "test"),
              SetVariableStatusEnum::Accepted);

    Variable configured_variable;
    configured_variable.name = "UnitTestPropertyBName";
    configured_variable.instance = "UnitTestInstance";

    const auto attribute = dm.get_variable_attribute(component, configured_variable, AttributeEnum::Actual);
    ASSERT_TRUE(attribute.has_value());
    ASSERT_TRUE(attribute->value.has_value());
    EXPECT_EQ(attribute->value.value().get(), "false");

    Variable other_instance_variable;
    other_instance_variable.name = "UnitTestPropertyBName";
    other_instance_variable.instance = "OtherInstance";
    EXPECT_FALSE(dm.get_variable_attribute(component, other_instance_variable, AttributeEnum::Actual).has_value());

    Variable other_name_variable;
    other_name_variable.name = "UnitTestPropertyAName";
    other_name_variable.instance = "UnitTestInstance";
    EXPECT_FALSE(dm.get_variable_attribute(component, other_name_variable, AttributeEnum::Actual).has_value());
}

/// \brief A component is looked up by name and instance, both matched case-insensitively
TEST_F(DeviceModelStorageSQLiteTest, test_set_variable_attribute_value_component_case_insensitive) {
    DeviceModelStorageSqlite dm(DATABASE_PATH);

    EVSE evse;
    evse.id = 2;
    evse.connectorId = 3;

    Variable variable;
    variable.name = "UnitTestPropertyAName";

    Component miscased_component;
    miscased_component.name = "unittestCTRLR";
    miscased_component.instance = "UNITTESTINSTANCE";
    miscased_component.evse = evse;

    EXPECT_EQ(dm.set_variable_attribute_value(miscased_component, variable, AttributeEnum::Actual, "false", "test"),
              SetVariableStatusEnum::Accepted);

    Component configured_component;
    configured_component.name = "UnitTestCtrlr";
    configured_component.instance = "UnitTestInstance";
    configured_component.evse = evse;

    const auto attribute = dm.get_variable_attribute(configured_component, variable, AttributeEnum::Actual);
    ASSERT_TRUE(attribute.has_value());
    ASSERT_TRUE(attribute->value.has_value());
    EXPECT_EQ(attribute->value.value().get(), "false");

    Component no_instance_component;
    no_instance_component.name = "UnitTestCtrlr";
    no_instance_component.evse = evse;

    const auto untouched_attribute = dm.get_variable_attribute(no_instance_component, variable, AttributeEnum::Actual);
    ASSERT_TRUE(untouched_attribute.has_value());
    ASSERT_TRUE(untouched_attribute->value.has_value());
    EXPECT_EQ(untouched_attribute->value.value().get(), "true");

    Component other_name_component;
    other_name_component.name = "OtherCtrlr";
    other_name_component.instance = "UnitTestInstance";
    other_name_component.evse = evse;
    EXPECT_FALSE(dm.get_variable_attribute(other_name_component, variable, AttributeEnum::Actual).has_value());
}

/// \brief Fixture that mimics a DeviceModelConfigPath copied from a release that predates OCPP16LegacyCtrlr: a copy
///        of the example component config with OCPP16LegacyCtrlr.json removed, plus an on-disk database.
class DeviceModelStorageSQLiteLegacyCtrlrTest : public ::testing::Test {
protected:
    const std::filesystem::path work_dir =
        std::filesystem::temp_directory_path() / "libocpp_test_device_model_storage_legacy_ctrlr";
    const std::filesystem::path db_path = work_dir / "device_model.db";
    const std::filesystem::path config_without_legacy_ctrlr = work_dir / "component_config";
    const std::map<std::int32_t, std::int32_t> evse_connector_structure{{1, 1}, {2, 1}};

    void SetUp() override {
        std::filesystem::remove_all(work_dir);
        std::filesystem::create_directories(work_dir);
        std::filesystem::copy(CONFIG_PATH, config_without_legacy_ctrlr, std::filesystem::copy_options::recursive);
        ASSERT_TRUE(std::filesystem::remove(config_without_legacy_ctrlr / "standardized" / "OCPP16LegacyCtrlr.json"));
    }

    void TearDown() override {
        std::filesystem::remove_all(work_dir);
    }

    /// \brief Open the storage like OCPP201 / OCPPmulti do and build a device model on top of it.
    std::unique_ptr<DeviceModel> create_device_model(const std::filesystem::path& config_path) {
        return std::make_unique<DeviceModel>(
            std::make_unique<DeviceModelStorageSqlite>(db_path, MIGRATION_FILES_PATH, config_path));
    }

    /// \brief Write NumberOfConnectors the way the ChargePoint constructor does before check_integrity.
    static void set_number_of_connectors(DeviceModel& dm, const std::size_t number_of_connectors) {
        const auto& cv = ControllerComponentVariables::NumberOfConnectors;
        ASSERT_EQ(dm.set_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                               std::to_string(number_of_connectors), VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL, true),
                  SetVariableStatusEnum::Accepted);
    }
};

/// \brief A fresh database initialized from a config directory without OCPP16LegacyCtrlr.json still gets the component
///        (and thus the required NumberOfConnectors) via the built-in fallback.
TEST_F(DeviceModelStorageSQLiteLegacyCtrlrTest, fresh_db_without_legacy_ctrlr_config_passes_integrity_check) {
    auto dm = create_device_model(config_without_legacy_ctrlr);

    set_number_of_connectors(*dm, evse_connector_structure.size());
    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));
    EXPECT_EQ(dm->get_optional_value<int>(ControllerComponentVariables::NumberOfConnectors), 2);
}

/// \brief A database that already contains OCPP16LegacyCtrlr (e.g. created from the full component config) keeps it
///        when re-initialized from a config directory without OCPP16LegacyCtrlr.json.
TEST_F(DeviceModelStorageSQLiteLegacyCtrlrTest, existing_db_keeps_legacy_ctrlr_when_config_lacks_it) {
    {
        auto dm = create_device_model(CONFIG_PATH);
        set_number_of_connectors(*dm, evse_connector_structure.size());
        ASSERT_NO_THROW(dm->check_integrity(evse_connector_structure));
    }

    auto dm = create_device_model(config_without_legacy_ctrlr);

    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));
    EXPECT_EQ(dm->get_optional_value<int>(ControllerComponentVariables::NumberOfConnectors), 2);
}

/// \brief A database created from an older built-in OCPP16LegacyCtrlr schema picks up variables added since, even
///        though the config directory has no OCPP16LegacyCtrlr.json to merge from.
TEST_F(DeviceModelStorageSQLiteLegacyCtrlrTest, existing_db_gains_new_legacy_ctrlr_variables) {
    const auto& cv = ControllerComponentVariables::ReportClearedErrors;
    {
        auto dm = create_device_model(config_without_legacy_ctrlr);
        ASSERT_TRUE(dm->get_variable_meta_data(cv.component, cv.variable.value()).has_value());
    }
    {
        // Mimic the older schema by dropping the variable from the database.
        everest::db::sqlite::Connection db(db_path);
        db.open_connection();
        auto stmt = db.new_statement("DELETE FROM VARIABLE WHERE NAME = ? AND COMPONENT_ID = "
                                     "(SELECT ID FROM COMPONENT WHERE NAME = ?)");
        stmt->bind_text(1, cv.variable.value().name.get(), everest::db::sqlite::SQLiteString::Transient);
        stmt->bind_text(2, cv.component.name.get(), everest::db::sqlite::SQLiteString::Transient);
        ASSERT_EQ(stmt->step(), SQLITE_DONE);
        ASSERT_EQ(stmt->changes(), 1);
    }

    auto dm = create_device_model(config_without_legacy_ctrlr);

    EXPECT_TRUE(dm->get_variable_meta_data(cv.component, cv.variable.value()).has_value());
}

} // namespace v2
} // namespace ocpp
