// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_all.hpp>
#include <everest/compile_time_settings.hpp>
#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/schema_updater.hpp>

#include <tests/helpers.hpp>
#include <utils/config/config_service_core.hpp>
#include <utils/config/settings.hpp>

using namespace Everest;
using namespace Everest::config;
using namespace everest::db::sqlite;

// Helper to set up an in-memory DB and apply migrations
std::shared_ptr<ConnectionInterface> setup_in_memory_db() {
    const auto bin_dir = tests::get_bin_dir().string() + "/";
    const auto migrations_dir = bin_dir + "migrations";

    auto db = std::make_shared<Connection>("file::memory:?cache=shared");
    REQUIRE(db->open_connection());
    SchemaUpdater updater{db.get()};
    REQUIRE(updater.apply_migration_files(migrations_dir, TARGET_MIGRATION_FILE_VERSION));
    return db;
}

// Helper to set up dummy parse settings
ConfigParseSettings setup_parse_settings() {
    const auto bin_dir = tests::get_bin_dir().string() + "/";

    // Leverage ManagerSettings to automatically resolve the correct CMake test-directory paths
    ManagerSettings ms(bin_dir + "config_service_test/", bin_dir + "config_service_test/config.yaml");
    ms.validate_schema = false;
    return ms; // Safely slices to ConfigParseSettings
}

TEST_CASE("ConfigServiceCore Unit Tests", "[config_service_core]") {
    auto db = setup_in_memory_db();
    auto parse_settings = setup_parse_settings();

    // Instantiate the core service
    ConfigServiceCore config_service(parse_settings, db);

    SECTION("YAML Loading: into a new slot") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";

        auto result = config_service.load_from_yaml(valid_yaml, "Test description", std::nullopt);
        INFO(result.error_message);
        REQUIRE(result.success == true);
        REQUIRE(result.slot_id.has_value());

        auto slots = config_service.list_all_slots();
        REQUIRE(slots.size() >= 1);
    }

    SECTION("Set Parameters: for a running module") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";

        auto lfy_result = config_service.load_from_yaml(valid_yaml, "Test description", std::nullopt);
        INFO(lfy_result.error_message);
        REQUIRE(lfy_result.success == true);

        // load_from_yaml above (no explicit slot_id) populated slot 0
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        auto config = config_service.get_configuration(0);

        // Modules are running, so changes go through the callback
        config_service.set_modules_running();

        // Register a mock handler that simulates the module accepting the new value immediately
        config_service.register_set_runtime_parameter_handler(
            [](const everest::config::ConfigurationParameterIdentifier& id, const std::string& val) {
                if (val == "accept_me") {
                    return SetParameterResponse::ModuleReplied_Applied;
                } else if (val == "reboot_me") {
                    return SetParameterResponse::ModuleReplied_RequiresRestart;
                }
                return SetParameterResponse::ModuleReplied_Rejected;
            });

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_config_entry", "!module"};
        ConfigParameterUpdate update{param_id, "accept_me", false};
        Origin origin{true, std::nullopt};

        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);

        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        INFO(result.parameter_results->front().status_info);
        REQUIRE(result.parameter_results->front().status == SetConfigParameterResultEnum::Applied);
    }

    SECTION("State Tracking: event listeners triggered on state changes") {
        bool listener_called = false;
        config_service.register_active_slot_update_handler([&listener_called](const ActiveSlotUpdate& update) {
            listener_called = true;
            CHECK(update.status == ActiveSlotStatus::Starting);
        });

        config_service.set_modules_starting();
        REQUIRE(listener_called == true);
    }

    SECTION("Set Parameters: ReadOnly handling based on allow_set_read_only") {
        // 1. Manually craft the module configurations to inject into the database
        everest::config::ModuleConfigurations mock_configs;

        // Module A: Allows setting ReadOnly parameters
        everest::config::ModuleConfig flexible_module;
        flexible_module.module_name = "FlexibleModule";
        flexible_module.module_id = "flexible_module";

        everest::config::ConfigAccess flex_access;
        flexible_module.access.config = flex_access;
        // Specifically allow setting ReadOnly parameters globally for this module
        flexible_module.access.config->allow_set_read_only = true;

        everest::config::ConfigurationParameter flex_ro_param;
        flex_ro_param.name = "ro_param";
        flex_ro_param.value = "initial_value";
        flex_ro_param.characteristics.datatype = everest::config::Datatype::String;
        flex_ro_param.characteristics.mutability = everest::config::Mutability::ReadOnly;
        flexible_module.configuration_parameters["!module"].push_back(flex_ro_param);

        mock_configs["flexible_module"] = flexible_module;

        // Module B: Strictly rejects setting ReadOnly parameters (default behavior)
        everest::config::ModuleConfig strict_module;
        strict_module.module_name = "StrictModule";
        strict_module.module_id = "strict_module";

        everest::config::ConfigAccess strict_access;
        strict_module.access.config = strict_access;
        strict_module.access.config->allow_set_read_only = false;

        everest::config::ConfigurationParameter strict_ro_param;
        strict_ro_param.name = "ro_param";
        strict_ro_param.value = "initial_value";
        strict_ro_param.characteristics.datatype = everest::config::Datatype::String;
        strict_ro_param.characteristics.mutability = everest::config::Mutability::ReadOnly;
        strict_module.configuration_parameters["!module"].push_back(strict_ro_param);

        mock_configs["strict_module"] = strict_module;

        // 2. Write this configuration to an active slot (Slot 0)
        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        // Create the slot metadata first
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        // Persist the crafted modules
        storage->write_module_configs(mock_configs);

        // 3. Initialize the core service and simulate running modules
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        // Register a callback that will intentionally FAIL the test if called.
        // ReadOnly updates should NEVER be forwarded to running modules.
        config_service.register_set_runtime_parameter_handler(
            [](const everest::config::ConfigurationParameterIdentifier&, const std::string&) {
                FAIL("Callback should not be invoked for ReadOnly parameters");
                return SetParameterResponse::ModuleReplied_Rejected;
            });

        Origin origin{false, "manager"};

        // --- Verify flexible module behavior ---
        everest::config::ConfigurationParameterIdentifier flex_id{"flexible_module", "ro_param", "!module"};
        ConfigParameterUpdate flex_update{flex_id, "new_value", false};

        auto flex_result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {flex_update}, origin);

        REQUIRE(flex_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(flex_result.parameter_results.has_value());
        // Since allow_set_read_only is true, it is persisted to the DB and takes effect on next boot
        CHECK(flex_result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);

        // --- Verify strict module behavior ---
        everest::config::ConfigurationParameterIdentifier strict_id{"strict_module", "ro_param", "!module"};
        ConfigParameterUpdate strict_update{strict_id, "new_value", false};

        auto strict_result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {strict_update}, origin);

        REQUIRE(strict_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(strict_result.parameter_results.has_value());
        // Since allow_set_read_only is false, it is rejected immediately
        CHECK(strict_result.parameter_results->front().status == SetConfigParameterResultEnum::Rejected);
        CHECK(strict_result.parameter_results->front().status_info == "Is a ReadOnly parameter");
    }

    SECTION("Slot Management: mark_active_slot and delete_slot") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";

        // Create slots 1 and 2 (Slot 0 is the default active slot initialized in the constructor)
        auto res1 = config_service.load_from_yaml(valid_yaml, "Slot 1", 1);
        REQUIRE(res1.success == true);
        auto res2 = config_service.load_from_yaml(valid_yaml, "Slot 2", 2);
        REQUIRE(res2.success == true);

        // Verify initial state: next boot slot is 0
        REQUIRE(config_service.get_next_boot_slot_id() == 0);

        // Test mark_active_slot
        bool listener_called = false;
        config_service.register_active_slot_update_handler([&listener_called](const ActiveSlotUpdate& update) {
            listener_called = true;
            REQUIRE(update.next_boot_slot_id.has_value());
            CHECK(update.next_boot_slot_id.value() == 1);
        });

        // 1. Success
        auto mark_res = config_service.mark_active_slot(1);
        REQUIRE(mark_res == SetActiveSlotStatus::Success);
        REQUIRE(config_service.get_next_boot_slot_id() == 1);
        REQUIRE(listener_called == true);

        // 2. NoChangeRequired
        mark_res = config_service.mark_active_slot(1);
        REQUIRE(mark_res == SetActiveSlotStatus::NoChangeRequired);

        // 3. DoesNotExist
        mark_res = config_service.mark_active_slot(99);
        REQUIRE(mark_res == SetActiveSlotStatus::DoesNotExist);

        // Test delete_slot
        // 1. CannotDeleteActiveSlot (Slot 0 is running right now)
        auto del_res = config_service.delete_slot(0);
        REQUIRE(del_res == DeleteSlotStatus::CannotDeleteActiveSlot);

        // 2. CannotDeleteActiveSlot (Slot 1 is staged for next boot)
        del_res = config_service.delete_slot(1);
        REQUIRE(del_res == DeleteSlotStatus::CannotDeleteActiveSlot);

        // 3. Success (slot 2 is neither running nor staged for next boot)
        del_res = config_service.delete_slot(2);
        REQUIRE(del_res == DeleteSlotStatus::Success);

        // 4. DoesNotExist
        del_res = config_service.delete_slot(2); // Already deleted
        REQUIRE(del_res == DeleteSlotStatus::DoesNotExist);
    }

    SECTION("Slot Management: duplicate_slot and set_description") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";

        // Create slot 1
        auto res1 = config_service.load_from_yaml(valid_yaml, "Original Slot", 1);
        REQUIRE(res1.success == true);

        // Test set_description
        // 1. Success
        bool desc_res = config_service.set_description(1, "Updated Original Slot");
        REQUIRE(desc_res == true);

        auto slots = config_service.list_all_slots();
        auto it1 = std::find_if(slots.begin(), slots.end(), [](const auto& s) { return s.id == 1; });
        REQUIRE(it1 != slots.end());
        REQUIRE(it1->description.has_value());
        CHECK(it1->description.value() == "Updated Original Slot");

        // 2. DoesNotExist
        desc_res = config_service.set_description(99, "Ghost Slot");
        REQUIRE(desc_res == false);

        // Test duplicate_slot
        // 1. Success with description
        auto dup_res = config_service.duplicate_slot(1, "Duplicated Slot");
        REQUIRE(dup_res.success == true);
        REQUIRE(dup_res.slot_id.has_value());
        int new_slot_id = dup_res.slot_id.value();

        slots = config_service.list_all_slots();
        auto it_dup =
            std::find_if(slots.begin(), slots.end(), [new_slot_id](const auto& s) { return s.id == new_slot_id; });
        REQUIRE(it_dup != slots.end());
        REQUIRE(it_dup->description.has_value());
        CHECK(it_dup->description.value() == "Duplicated Slot");

        // Verify config was actually duplicated
        auto cfg_res = config_service.get_configuration(new_slot_id);
        REQUIRE(cfg_res.status == GetConfigurationStatus::Success);
        CHECK(cfg_res.module_configurations.count("dummy_module") > 0);

        // 2. Success without new description (inherits description from original)
        auto dup_res_no_desc = config_service.duplicate_slot(1, std::nullopt);
        REQUIRE(dup_res_no_desc.success == true);
        REQUIRE(dup_res_no_desc.slot_id.has_value());
        int new_slot_id2 = dup_res_no_desc.slot_id.value();

        slots = config_service.list_all_slots();
        auto it_dup2 =
            std::find_if(slots.begin(), slots.end(), [new_slot_id2](const auto& s) { return s.id == new_slot_id2; });
        REQUIRE(it_dup2 != slots.end());
        REQUIRE(not it_dup2->description.has_value());

        // 3. DoesNotExist
        auto dup_res_fail = config_service.duplicate_slot(99, "Fail");
        REQUIRE(dup_res_fail.success == false);
        REQUIRE_FALSE(dup_res_fail.slot_id.has_value());
    }

    SECTION("Edge Cases: load_from_yaml overwriting an existing slot") {
        std::string yaml_v1 = R"(
active_modules:
  dummy_module_v1:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        auto res_v1 = config_service.load_from_yaml(yaml_v1, "Version 1", 5);
        REQUIRE(res_v1.success == true);

        // Overwrite the same slot
        std::string yaml_v2 = R"(
active_modules:
  dummy_module_v2:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        auto res_v2 = config_service.load_from_yaml(yaml_v2, "Version 2", 5);
        REQUIRE(res_v2.success == true);

        // Verify the overwrite was successful
        auto cfg_res = config_service.get_configuration(5);
        REQUIRE(cfg_res.status == GetConfigurationStatus::Success);
        CHECK(cfg_res.module_configurations.count("dummy_module_v2") > 0);
    }

    SECTION("Edge Cases: load_from_yaml") {
        std::string invalid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";

        config_service.set_modules_running();
        // Loading into active slot should fail
        auto res_active = config_service.load_from_yaml("active_modules: {}", "Active", 0);
        REQUIRE(res_active.success == false);
        CHECK(res_active.error_message == "Cannot load YAML into the active slot when modules are running");
    }

    SECTION("Edge Cases: set_config_parameters during transient states") {
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_starting(); // Transient state

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "some_param", "!module"};
        ConfigParameterUpdate update{param_id, "new_value", false};
        Origin origin{false, "manager"};

        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);

        REQUIRE(result.status == SetConfigParameterStatus::ModulesInTransientState);
        REQUIRE(result.parameter_results.has_value());
        CHECK(result.parameter_results->front().status == SetConfigParameterResultEnum::RetryLater);
    }

    SECTION("Retrieval: get_config_parameters usage") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        config_service.load_from_yaml(valid_yaml, "Slot 1", 1);

        everest::config::ConfigurationParameterIdentifier unknown_id{"dummy_module", "unknown_param", "!module"};

        auto result = config_service.get_config_parameters(1, {unknown_id});

        REQUIRE(result.status == GetConfigurationStatus::Success);
        REQUIRE(result.parameters.size() == 1);
        CHECK_FALSE(result.parameters[0].has_value()); // Parameter does not exist

        // Non-existent slot
        auto bad_result = config_service.get_config_parameters(99, {unknown_id});
        CHECK(bad_result.status == GetConfigurationStatus::SlotDoesNotExist);
    }

    SECTION("Edge Cases: reinitialize_from_db safety guard") {
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);

        // Lock the state by setting it to running
        config_service.set_modules_running();

        // Attempt to stage and load a new slot
        config_service.mark_active_slot(1);
        config_service.reinitialize_from_db(true); // Should return early

        // Verify active slot was NOT changed because modules are not stopped
        CHECK(config_service.get_active_slot_id() == 0);
    }

    SECTION("State Tracking: remaining state change events") {
        std::vector<ActiveSlotStatus> recorded_statuses;
        config_service.register_active_slot_update_handler(
            [&recorded_statuses](const ActiveSlotUpdate& update) { recorded_statuses.push_back(update.status); });

        config_service.set_modules_stopping();
        config_service.set_modules_stopped();
        config_service.notice_cfg_validation_failed();
        config_service.notice_module_restart_triggered();

        // Starting and Running are tested elsewhere, so we check the 4 triggered above
        // Note: The callback vector is appended to, so earlier tests might have added their own
        // states if the service was not re-instantiated. We check the last 4.
        REQUIRE(recorded_statuses.size() >= 4);
        auto it = recorded_statuses.end() - 4;
        CHECK(*it++ == ActiveSlotStatus::Stopping);
        CHECK(*it++ == ActiveSlotStatus::Stopped);
        CHECK(*it++ == ActiveSlotStatus::FailedToStart);
        CHECK(*it++ == ActiveSlotStatus::RestartTriggered);
    }

    SECTION("Edge Cases: set_config_parameters without registered callback") {
        everest::config::ModuleConfigurations mock_configs;

        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "DummyModule";
        dummy_module.module_id = "dummy_module";

        everest::config::ConfigurationParameter rw_param;
        rw_param.name = "rw_param";
        rw_param.value = "initial_value";
        rw_param.characteristics.datatype = everest::config::Datatype::String;
        rw_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(rw_param);

        mock_configs["dummy_module"] = dummy_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        // Intentionally omit: config_service.register_set_runtime_parameter_handler(...)

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "rw_param", "!module"};
        ConfigParameterUpdate update{param_id, "new_value", false};
        Origin origin{false, "manager"};

        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);

        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        CHECK(result.parameter_results->front().status == SetConfigParameterResultEnum::Rejected);
    }

    SECTION("Set Parameters: WriteOnly handling and SetCallFailed") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig test_module;
        test_module.module_name = "TestModule";
        test_module.module_id = "test_module";

        everest::config::ConfigurationParameter wo_param;
        wo_param.name = "wo_param";
        wo_param.value = "initial_value";
        wo_param.characteristics.datatype = everest::config::Datatype::String;
        wo_param.characteristics.mutability = everest::config::Mutability::WriteOnly;
        test_module.configuration_parameters["!module"].push_back(wo_param);

        everest::config::ConfigurationParameter rw_fail_param;
        rw_fail_param.name = "rw_fail_param";
        rw_fail_param.value = "initial_value";
        rw_fail_param.characteristics.datatype = everest::config::Datatype::String;
        rw_fail_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        test_module.configuration_parameters["!module"].push_back(rw_fail_param);

        mock_configs["test_module"] = test_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.reinitialize_from_db(true);
        auto cfg = config_service.get_configuration(0);
        INFO(cfg.module_configurations.size());
        config_service.set_modules_running();

        config_service.register_set_runtime_parameter_handler(
            [](const everest::config::ConfigurationParameterIdentifier& id, const std::string&) {
                if (id.configuration_parameter_name == "wo_param") {
                    FAIL("WriteOnly parameters should not trigger the runtime callback");
                }
                return SetParameterResponse::SetCallFailed;
            });

        Origin origin{false, "manager"};
        ConfigParameterUpdate wo_update{{"test_module", "wo_param", "!module"}, "new", false};
        ConfigParameterUpdate rw_update{{"test_module", "rw_fail_param", "!module"}, "new", false};

        auto result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {wo_update, rw_update}, origin);
        REQUIRE(result.parameter_results.has_value());
        INFO(result.status_info);
        INFO(result.parameter_results->at(0).status_info);
        CHECK(result.parameter_results->at(0).status == SetConfigParameterResultEnum::WillApplyOnRestart);
        INFO(result.parameter_results->at(1).status_info);
        CHECK(result.parameter_results->at(1).status == SetConfigParameterResultEnum::Rejected);
    }

    SECTION("Set Parameters: direct modification of an inactive slot") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig inactive_module;
        inactive_module.module_name = "InactiveModule";
        inactive_module.module_id = "inactive_module";

        everest::config::ConfigurationParameter rw_param;
        rw_param.name = "inactive_param";
        rw_param.value = "old_value";
        rw_param.characteristics.datatype = everest::config::Datatype::String;
        rw_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        inactive_module.configuration_parameters["!module"].push_back(rw_param);

        mock_configs["inactive_module"] = inactive_module;

        // 1. Write this configuration to an inactive slot (Slot 3)
        int target_slot = 3;
        auto storage = std::make_unique<everest::config::SqliteStorage>(db, target_slot);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(target_slot, "{}", std::nullopt, "Inactive Slot");
        storage->write_module_configs(mock_configs);

        // 2. Set the active slot to 0 and verify we are targeting a different slot
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        // 3. Update the parameter in the INACTIVE slot (Slot 3)
        everest::config::ConfigurationParameterIdentifier param_id{"inactive_module", "inactive_param", "!module"};
        ConfigParameterUpdate update{param_id, "new_value", false};
        Origin origin{false, "manager"};

        auto result = config_service.set_config_parameters(target_slot, {update}, origin);

        // 4. Verify the result and database persistence
        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        CHECK(result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);

        auto get_res = config_service.get_config_parameters(target_slot, {param_id});
        REQUIRE(get_res.status == GetConfigurationStatus::Success);
        REQUIRE(get_res.parameters.size() == 1);
        REQUIRE(get_res.parameters[0].has_value());
        CHECK(std::get<std::string>(get_res.parameters[0]->value) == "new_value");
    }

    SECTION("Edge Cases: set_config_parameters with non-existent parameter") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "DummyModule";
        dummy_module.module_id = "dummy_module";
        mock_configs["dummy_module"] = dummy_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        everest::config::ConfigurationParameterIdentifier unknown_id{"dummy_module", "ghost_param", "!module"};
        ConfigParameterUpdate update{unknown_id, "ghost_value", false};
        Origin origin{false, "manager"};

        // 1. Target the active slot
        auto active_result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);

        REQUIRE(active_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(active_result.parameter_results.has_value());
        CHECK(active_result.parameter_results->front().status == SetConfigParameterResultEnum::DoesNotExist);
        CHECK(active_result.parameter_results->front().status_info ==
              "Unknown parameter: ghost_param in module: dummy_module");

        // 2. Target an inactive slot directly
        int target_slot = 4;
        auto storage_inactive = std::make_unique<everest::config::SqliteStorage>(db, target_slot);
        slot_manager.write_config_slot(target_slot, "{}", std::nullopt, "Inactive Test Slot");
        storage_inactive->write_module_configs(mock_configs);

        auto inactive_result = config_service.set_config_parameters(target_slot, {update}, origin);
        REQUIRE(inactive_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(inactive_result.parameter_results.has_value());
        CHECK(inactive_result.parameter_results->front().status == SetConfigParameterResultEnum::DoesNotExist);
        CHECK(inactive_result.parameter_results->front().status_info ==
              "Unknown parameter: ghost_param in module: dummy_module");
    }
}

TEST_CASE("ConfigServiceCore Concurrency Tests", "[config_service_core][concurrency]") {
    auto db = setup_in_memory_db();
    auto parse_settings = setup_parse_settings();

    // Instantiate with spawn_threads = true (the default) to enable concurrency features
    ConfigServiceCore config_service(parse_settings, db, true);

    SECTION("Async module callback timeout logic") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        auto lfy_result = config_service.load_from_yaml(valid_yaml, "Test description", std::nullopt);
        REQUIRE(lfy_result.success == true);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        // Register a callback that blocks for 5 seconds (longer than the 3-second timeout)
        config_service.register_set_runtime_parameter_handler(
            [](const everest::config::ConfigurationParameterIdentifier&, const std::string&) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                return SetParameterResponse::ModuleReplied_Applied;
            });

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_config_entry", "!module"};
        ConfigParameterUpdate update{param_id, "timeout_me", false};
        Origin origin{true, std::nullopt};

        auto start = std::chrono::steady_clock::now();
        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);
        auto end = std::chrono::steady_clock::now();

        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        CHECK(result.parameter_results->front().status == SetConfigParameterResultEnum::RetryLater);
        CHECK(result.parameter_results->front().status_info == "Timeout waiting for module to respond");

        // Should take ~3 seconds due to the timeout, not 5
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        CHECK(elapsed >= 3);
        CHECK(elapsed < 5);
    }

    SECTION("Parallel execution of network callbacks") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "DummyModule";
        dummy_module.module_id = "dummy_module";

        for (int i = 0; i < 5; ++i) {
            everest::config::ConfigurationParameter param;
            param.name = "param_" + std::to_string(i);
            param.value = "val";
            param.characteristics.datatype = everest::config::Datatype::String;
            param.characteristics.mutability = everest::config::Mutability::ReadWrite;
            dummy_module.configuration_parameters["!module"].push_back(param);
        }
        mock_configs["dummy_module"] = dummy_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        std::atomic<int> active_calls{0};
        int max_active_calls = 0;
        std::mutex mtx;

        config_service.register_set_runtime_parameter_handler(
            [&](const everest::config::ConfigurationParameterIdentifier&, const std::string&) {
                int current = ++active_calls;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (current > max_active_calls) {
                        max_active_calls = current;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                active_calls--;
                return SetParameterResponse::ModuleReplied_Applied;
            });

        std::vector<ConfigParameterUpdate> updates;
        for (int i = 0; i < 5; ++i) {
            everest::config::ConfigurationParameterIdentifier id{"dummy_module", "param_" + std::to_string(i),
                                                                 "!module"};
            updates.push_back({id, "new_val", false});
        }
        Origin origin{false, "manager"};

        auto start = std::chrono::steady_clock::now();
        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, updates, origin);
        auto end = std::chrono::steady_clock::now();

        REQUIRE(result.status == SetConfigParameterStatus::Ok);

        // Since we process 5 updates in parallel and each takes 500ms, the total time should be close to 500ms.
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        CHECK(elapsed_ms >= 500);
        CHECK(elapsed_ms < 1500);    // Allow some overhead
        CHECK(max_active_calls > 1); // Prove they ran in parallel
    }

    SECTION("Concurrent calls to public API serialize safely via Actor Model") {
        std::string yaml_v1 = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        auto res_v1 = config_service.load_from_yaml(yaml_v1, "Version 1", 0);
        REQUIRE(res_v1.success == true);

        const int num_threads = 20;
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&config_service, i]() {
                // Interleave reads and writes to ensure actor model handles it safely
                (void)config_service.get_configuration(0);
                (void)config_service.list_all_slots();

                if (i % 2 == 0) {
                    config_service.set_description(0, "Updated Description " + std::to_string(i));
                } else {
                    (void)config_service.get_active_module_configurations();
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        SUCCEED("Concurrent reads and actor tasks completed without data races");
    }

    SECTION("Actor strictly serializes overlapping mutating calls") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        config_service.load_from_yaml(valid_yaml, "Test", 0);
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        // Register a callback that simulates a slow module (500ms delay)
        config_service.register_set_runtime_parameter_handler(
            [](const everest::config::ConfigurationParameterIdentifier&, const std::string&) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return SetParameterResponse::ModuleReplied_Applied;
            });

        std::thread slow_thread([&]() {
            everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_config_entry", "!module"};
            ConfigParameterUpdate update{param_id, "slow_val", false};
            Origin origin{false, "manager"};
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);
        });

        // Give the slow thread a tiny head start to acquire the Actor queue
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto b_start = std::chrono::steady_clock::now();

        // This call MUST queue behind the slow set_config_parameters call.
        // If the Actor Model is broken, this returns instantly. If it works, it blocks.
        config_service.set_modules_stopped();

        auto b_end = std::chrono::steady_clock::now();

        slow_thread.join();

        auto b_duration = std::chrono::duration_cast<std::chrono::milliseconds>(b_end - b_start).count();

        // set_modules_stopped should have been blocked for the remaining ~450ms
        CHECK(b_duration >= 400);
    }

    SECTION("Copy-on-Write (RCU) read safety during rapid writes") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello there"
    config_implementation:
      main:
        valid_config_entry: "hello there"
)";
        config_service.load_from_yaml(valid_yaml, "Test", 0);
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);

        std::atomic<bool> stop_readers{false};
        std::atomic<int> read_count{0};

        auto reader_fn = [&]() {
            while (!stop_readers) {
                // Atomically pull the config. Keep it alive in the shared_ptr while inspecting it.
                auto ptr = config_service.get_active_module_configurations();
                if (ptr && ptr->count("dummy_module") > 0) {
                    read_count++;
                }
            }
        };

        std::thread r1(reader_fn);
        std::thread r2(reader_fn);

        // Perform rapid writes to force multiple pointer allocations and swaps
        for (int i = 0; i < 50; ++i) {
            config_service.set_description(0, "Rapid Description " + std::to_string(i));
        }

        stop_readers = true;
        r1.join();
        r2.join();

        CHECK(read_count > 0);
        SUCCEED("Readers successfully traversed swapped pointers without crashing");
    }
}
