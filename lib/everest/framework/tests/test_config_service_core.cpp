// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>
#include <future>

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

    SECTION("Empty seeded slot: as produced by a no-config manager boot") {
        // Mirror init_database_bootstrap without a config file: an existing slot with zero modules
        everest::config::SqliteConfigSlotManager slot_mgr(db);
        const auto boot_slot_id = slot_mgr.get_next_boot_slot_id();
        REQUIRE(slot_mgr.write_config_slot(boot_slot_id, "{}", std::nullopt, std::nullopt) ==
                everest::config::GenericResponseStatus::OK);
        everest::config::SqliteStorage storage(db, boot_slot_id);
        REQUIRE(storage.write_module_configs({}) == everest::config::GenericResponseStatus::OK);

        config_service.reinitialize_from_db(true);

        auto active_configs = config_service.get_active_module_configurations();
        REQUIRE(active_configs != nullptr);
        CHECK(active_configs->empty());
        CHECK(config_service.get_active_slot_id() == boot_slot_id);

        // Runtime config arrival after a no-config boot: load a real config into a new slot
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
        auto result = config_service.load_from_yaml(valid_yaml, "Loaded after no-config boot", std::nullopt);
        INFO(result.error_message);
        REQUIRE(result.success == true);
        REQUIRE(result.slot_id.has_value());
        CHECK(result.slot_id.value() != boot_slot_id);
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
        ConfigParameterUpdate update{param_id, "accept_me"};
        Origin origin{true, std::nullopt};

        // Capture the published config update event to verify its contents
        std::optional<ConfigurationUpdate> captured_event;
        config_service.register_config_update_handler(
            [&captured_event](const ConfigurationUpdate& event) { captured_event = event; });

        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);

        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        INFO(result.parameter_results->front().status_info);
        REQUIRE(result.parameter_results->front().status == SetConfigParameterResultEnum::Applied);

        // An applied change must publish a config update event carrying the correct data
        REQUIRE(captured_event.has_value());
        CHECK(captured_event->slot_id == 0); // ACTIVE_SLOT resolved to the active slot (0)
        CHECK(captured_event->origin.external == true);
        CHECK_FALSE(captured_event->origin.module_id.has_value());
        REQUIRE(captured_event->updates.size() == 1);
        CHECK(captured_event->updates.front().identifier.module_id == "dummy_module");
        CHECK(captured_event->updates.front().identifier.configuration_parameter_name == "valid_config_entry");
        CHECK(captured_event->updates.front().value == "accept_me");
        CHECK(captured_event->updates.front().result == SetConfigParameterResultEnum::Applied);
    }

    SECTION("Set Parameters: RequiresRestart/Rejected module replies keep the persisted change") {
        std::string valid_yaml = R"(
active_modules:
  dummy_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "initial_in_memory_value"
    config_implementation:
      main:
        valid_config_entry: "initial_in_memory_value"
)";

        auto lfy_result = config_service.load_from_yaml(valid_yaml, "Test description", std::nullopt);
        INFO(lfy_result.error_message);
        REQUIRE(lfy_result.success == true);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        int callback_calls = 0;
        config_service.register_set_runtime_parameter_handler(
            [&callback_calls](const everest::config::ConfigurationParameterIdentifier&, const std::string& val) {
                callback_calls++;
                if (val == "reboot_me") {
                    return SetParameterResponse::ModuleReplied_RequiresRestart;
                }
                return SetParameterResponse::ModuleReplied_Rejected;
            });

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_config_entry", "!module"};
        Origin origin{false, "manager"};

        std::optional<ConfigurationUpdate> captured_event;
        config_service.register_config_update_handler(
            [&captured_event](const ConfigurationUpdate& event) { captured_event = event; });

        // Persist-first contract: in both cases below the value is already persisted when the
        // module replies, so the result stays WillApplyOnRestart - a runtime veto by the module
        // does NOT undo persistence. Only an Applied reply mutates the in-memory configuration.
        const auto check_persist_first_outcome = [&](const std::string& new_value,
                                                     const std::string& expected_status_info) {
            ConfigParameterUpdate update{param_id, new_value};

            auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);

            CHECK(callback_calls == 1); // the change went through the module round-trip
            REQUIRE(result.status == SetConfigParameterStatus::Ok);
            REQUIRE(result.parameter_results.has_value());
            CHECK(result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);
            CHECK(result.parameter_results->front().status_info == expected_status_info);

            // The value is persisted and applies on the next boot
            everest::config::SqliteStorage storage(db, 0);
            auto persisted = storage.get_configuration_parameter(param_id);
            REQUIRE(persisted.status == everest::config::GetSetResponseStatus::OK);
            CHECK(std::get<std::string>(persisted.configuration_parameter.value().value) == new_value);

            // The persisted change is published as a ConfigurationUpdate
            REQUIRE(captured_event.has_value());
            REQUIRE(captured_event->updates.size() == 1);
            CHECK(captured_event->updates.front().value == new_value);
            CHECK(captured_event->updates.front().result == SetConfigParameterResultEnum::WillApplyOnRestart);

            // The in-memory (runtime) value is untouched - only an Applied reply mutates it
            auto active_configs = config_service.get_active_module_configurations();
            const auto& params = active_configs->at("dummy_module").configuration_parameters.at("!module");
            const auto param_it = std::find_if(params.begin(), params.end(),
                                               [](const auto& p) { return p.name == "valid_config_entry"; });
            REQUIRE(param_it != params.end());
            CHECK(std::get<std::string>(param_it->value) == "initial_in_memory_value");
        };

        SECTION("module replies RequiresRestart") {
            check_persist_first_outcome("reboot_me", "");
        }

        SECTION("module rejects the runtime change") {
            check_persist_first_outcome("reject_me", "Runtime change rejected by module");
        }
    }

    SECTION("Set Parameters: for a stopped module") {
        std::string empty_yaml = R"({})";
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

        // Make sure slot 0 exists
        auto empty_lfy_result = config_service.load_from_yaml(empty_yaml, "Test description", 0);
        INFO(empty_lfy_result.error_message);
        REQUIRE(empty_lfy_result.success == true);

        // Ensure active_slot is 0
        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);

        // Load from YAML
        auto lfy_result = config_service.load_from_yaml(valid_yaml, "Test description", 0);
        INFO(lfy_result.error_message);
        REQUIRE(lfy_result.success == true);

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_config_entry", "!module"};
        ConfigParameterUpdate update{param_id, "to_be_applied_later"};
        Origin origin{true, std::nullopt};

        auto result = config_service.set_config_parameters(0, {update}, origin);

        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        INFO(result.parameter_results->front().status_info);

        // Since modules are not running, the configuration update should be accepted
        // but it will only be applied after a restart.
        REQUIRE(result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);
    }

    SECTION("State Tracking: run-state transitions publish events with correct data") {
        std::vector<ActiveSlotUpdate> events;
        config_service.register_active_slot_update_handler(
            [&events](const ActiveSlotUpdate& update) { events.push_back(update); });

        // Drive the full run-state lifecycle; every call must publish exactly one event
        config_service.set_modules_starting();
        config_service.set_modules_running();
        config_service.set_modules_stopping();
        config_service.set_modules_stopped();
        config_service.notice_cfg_validation_failed();
        config_service.notice_module_restart_triggered();

        const std::vector<ActiveSlotStatus> expected_statuses{
            ActiveSlotStatus::Starting, ActiveSlotStatus::Running,       ActiveSlotStatus::Stopping,
            ActiveSlotStatus::Stopped,  ActiveSlotStatus::FailedToStart, ActiveSlotStatus::RestartTriggered};

        REQUIRE(events.size() == expected_statuses.size());
        for (size_t i = 0; i < expected_statuses.size(); ++i) {
            INFO("event index " << i);
            CHECK(events[i].status == expected_statuses[i]);
            // Run-state changes never move the slot: every event reports the default active slot 0
            CHECK(events[i].active_slot_id == 0);
            REQUIRE(events[i].next_boot_slot_id.has_value());
            CHECK(events[i].next_boot_slot_id.value() == 0);
        }
    }

    SECTION("Set Parameters: ReadOnly parameters are persisted for next boot, never forwarded live") {
        // ConfigServiceCore does not evaluate the allow_set_read_only access flag: that flag only
        // changes how parameters are *presented* to modules by the module-facing config service
        // (see update_mutability in mqtt_config_service.cpp). Here a write to a ReadOnly parameter
        // is always persisted and reported as WillApplyOnRestart, and is never forwarded to a
        // running module. Two modules with opposite flag values pin down that the flag makes no
        // difference in this code path.

        // 1. Manually craft the module configurations to inject into the database
        everest::config::ModuleConfigurations mock_configs;

        // Module A: has allow_set_read_only set
        everest::config::ModuleConfig flexible_module;
        flexible_module.module_name = "TESTCSTarget";
        flexible_module.module_id = "flexible_module";

        everest::config::ConfigAccess flex_access;
        flexible_module.access.config = flex_access;
        flexible_module.access.config->allow_set_read_only = true;

        everest::config::ConfigurationParameter flex_ro_param;
        flex_ro_param.name = "ro_param";
        flex_ro_param.value = "initial_value";
        flex_ro_param.characteristics.datatype = everest::config::Datatype::String;
        flex_ro_param.characteristics.mutability = everest::config::Mutability::ReadOnly;
        flexible_module.configuration_parameters["!module"].push_back(flex_ro_param);

        mock_configs["flexible_module"] = flexible_module;

        // Module B: allow_set_read_only not set (default)
        everest::config::ModuleConfig strict_module;
        strict_module.module_name = "TESTCSTarget";
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
        ConfigParameterUpdate flex_update{flex_id, "new_value"};

        auto flex_result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {flex_update}, origin);

        REQUIRE(flex_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(flex_result.parameter_results.has_value());
        // Persisted to the DB, takes effect on next boot; not forwarded to the running module
        CHECK(flex_result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);

        // --- Verify strict module behavior ---
        everest::config::ConfigurationParameterIdentifier strict_id{"strict_module", "ro_param", "!module"};
        ConfigParameterUpdate strict_update{strict_id, "new_value"};

        auto strict_result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {strict_update}, origin);

        REQUIRE(strict_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(strict_result.parameter_results.has_value());
        // Same result without the flag: persisted for the next boot, not rejected
        CHECK(strict_result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);
        CHECK(strict_result.parameter_results->front().status_info == "");
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

        config_service.set_modules_running();

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
        ConfigParameterUpdate update{param_id, "new_value"};
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

    SECTION("Retrieval: get_config_parameters after modification with running modules") {
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
        config_service.mark_active_slot(1);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_bool_config_entry",
                                                                   "!module"};

        ConfigParameterUpdate update{param_id, "false"};
        Origin origin{false, "manager"};

        // Set a config parameter which is not runtime changeable and check that it will apply on restart (== written to
        // the DB)
        auto set_result = config_service.set_config_parameters(1, {update}, origin);

        REQUIRE(set_result.status == SetConfigParameterStatus::Ok);
        REQUIRE(set_result.parameter_results.has_value());
        CHECK(set_result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);

        // get the runtime config parameter -> old value
        auto get_result = config_service.get_config_parameters(1, {param_id});

        REQUIRE(get_result.status == GetConfigurationStatus::Success);
        REQUIRE(get_result.parameters.size() == 1);
        CHECK(get_result.parameters[0].has_value());
        CHECK(std::get<bool>(get_result.parameters[0]->value) == true);

        // get the config parameter again, but force reading from db -> new value
        auto get_force_result = config_service.get_config_parameters(1, {param_id}, true);

        REQUIRE(get_force_result.status == GetConfigurationStatus::Success);
        REQUIRE(get_force_result.parameters.size() == 1);
        CHECK(get_force_result.parameters[0].has_value());
        CHECK(std::get<bool>(get_force_result.parameters[0]->value) == false);
    }

    SECTION("Edge Cases: reinitialize_from_db safety guard") {
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
        // Populate both slots so that staging slot 1 can actually succeed; without this,
        // mark_active_slot(1) fails with DoesNotExist and the checks below pass regardless
        // of whether the guard exists.
        auto result0 = config_service.load_from_yaml(valid_yaml, "Slot 0", 0);
        REQUIRE(result0.success);
        auto result1 = config_service.load_from_yaml(valid_yaml, "Slot 1", 1);
        REQUIRE(result1.success);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        REQUIRE(config_service.get_active_slot_id() == 0);

        // Lock the state by setting it to running
        config_service.set_modules_running();

        // Stage a new slot; staging itself must succeed while modules are running
        REQUIRE(config_service.mark_active_slot(1) == SetActiveSlotStatus::Success);
        config_service.reinitialize_from_db(true); // Should return early

        // Verify active slot was NOT changed because modules are not stopped
        CHECK(config_service.get_active_slot_id() == 0);

        // Positive control: once the modules are stopped, the same reinitialize applies the
        // staged slot - proving the guard (and not a failed mark_active_slot) is what kept
        // the active slot unchanged above.
        config_service.set_modules_stopped();
        config_service.reinitialize_from_db(true);
        CHECK(config_service.get_active_slot_id() == 1);
    }

    SECTION("Edge Cases: set_config_parameters without registered callback") {
        everest::config::ModuleConfigurations mock_configs;

        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "TESTCSTarget";
        dummy_module.module_id = "dummy_module";

        everest::config::ConfigurationParameter rw_param;
        rw_param.name = "rw_param";
        rw_param.value = "initial_value";
        rw_param.characteristics.datatype = everest::config::Datatype::String;
        rw_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(rw_param);

        everest::config::ConfigurationParameter rw_fail_param;
        rw_fail_param.name = "rw_fail_param";
        rw_fail_param.value = "initial_value_f";
        rw_fail_param.characteristics.datatype = everest::config::Datatype::String;
        rw_fail_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(rw_fail_param);

        mock_configs["dummy_module"] = dummy_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        // Intentionally omit: config_service.register_set_runtime_parameter_handler(...)

        std::optional<ConfigurationUpdate> captured_event;
        config_service.register_config_update_handler(
            [&captured_event](const ConfigurationUpdate& event) { captured_event = event; });

        everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "rw_param", "!module"};
        everest::config::ConfigurationParameterIdentifier param_id2{"dummy_module", "rw_fail_param", "!module"};
        ConfigParameterUpdate update{param_id, "new_value"};
        ConfigParameterUpdate update2{param_id2, "new_value_f"};
        Origin origin{false, "manager"};

        auto result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update, update2}, origin);

        // Without a runtime-change callback the values are persisted and apply on the next
        // restart; the whole batch is processed and a ConfigurationUpdate event is published.
        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        REQUIRE(result.parameter_results->size() == 2);
        CHECK(result.parameter_results->at(0).status == SetConfigParameterResultEnum::WillApplyOnRestart);
        CHECK(result.parameter_results->at(1).status == SetConfigParameterResultEnum::WillApplyOnRestart);

        // Both changes are persisted in the active slot's storage.
        auto persisted = storage->get_configuration_parameter(param_id);
        REQUIRE(persisted.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<std::string>(persisted.configuration_parameter.value().value) == "new_value");
        auto persisted2 = storage->get_configuration_parameter(param_id2);
        REQUIRE(persisted2.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<std::string>(persisted2.configuration_parameter.value().value) == "new_value_f");

        // The published event contains both updates.
        REQUIRE(captured_event.has_value());
        REQUIRE(captured_event->updates.size() == 2);
        CHECK(captured_event->updates.at(0).result == SetConfigParameterResultEnum::WillApplyOnRestart);
        CHECK(captured_event->updates.at(1).result == SetConfigParameterResultEnum::WillApplyOnRestart);
    }

    SECTION("Set Parameters: values are validated against the datatype before persisting") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "TESTCSTarget";
        dummy_module.module_id = "dummy_module";

        everest::config::ConfigurationParameter int_param;
        int_param.name = "int_param";
        int_param.value = 5;
        int_param.characteristics.datatype = everest::config::Datatype::Integer;
        int_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(int_param);

        everest::config::ConfigurationParameter rw_param;
        rw_param.name = "rw_param";
        rw_param.value = "initial_value";
        rw_param.characteristics.datatype = everest::config::Datatype::String;
        rw_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(rw_param);

        mock_configs["dummy_module"] = dummy_module;

        everest::config::SqliteConfigSlotManager slot_manager(db);

        everest::config::ConfigurationParameterIdentifier int_param_id{"dummy_module", "int_param", "!module"};
        everest::config::ConfigurationParameterIdentifier rw_param_id{"dummy_module", "rw_param", "!module"};
        ConfigParameterUpdate bad_update{int_param_id, "abc"};
        ConfigParameterUpdate good_update{rw_param_id, "new_value"};
        Origin origin{false, "manager"};

        std::optional<ConfigurationUpdate> captured_event;
        config_service.register_config_update_handler(
            [&captured_event](const ConfigurationUpdate& event) { captured_event = event; });

        SECTION("on the active slot") {
            auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
            slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
            storage->write_module_configs(mock_configs);
            config_service.mark_active_slot(0);
            config_service.reinitialize_from_db(true);

            auto result =
                config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {bad_update, good_update},
                                                     origin);

            REQUIRE(result.status == SetConfigParameterStatus::Ok);
            REQUIRE(result.parameter_results.has_value());
            REQUIRE(result.parameter_results->size() == 2);
            // The malformed value is rejected with an informative reason and nothing is persisted;
            // the rest of the batch is still processed.
            CHECK(result.parameter_results->at(0).status == SetConfigParameterResultEnum::Rejected);
            CHECK_FALSE(result.parameter_results->at(0).status_info.empty());
            CHECK(result.parameter_results->at(1).status == SetConfigParameterResultEnum::WillApplyOnRestart);

            auto persisted_int = storage->get_configuration_parameter(int_param_id);
            REQUIRE(persisted_int.status == everest::config::GetSetResponseStatus::OK);
            CHECK(std::get<int>(persisted_int.configuration_parameter.value().value) == 5);
            auto persisted_str = storage->get_configuration_parameter(rw_param_id);
            REQUIRE(persisted_str.status == everest::config::GetSetResponseStatus::OK);
            CHECK(std::get<std::string>(persisted_str.configuration_parameter.value().value) == "new_value");

            // Only the valid update appears in the published event.
            REQUIRE(captured_event.has_value());
            REQUIRE(captured_event->updates.size() == 1);
            CHECK(captured_event->updates.front().identifier.configuration_parameter_name == "rw_param");
        }

        SECTION("on an inactive slot") {
            auto storage = std::make_unique<everest::config::SqliteStorage>(db, 1);
            slot_manager.write_config_slot(1, "{}", std::nullopt, "Inactive Slot");
            storage->write_module_configs(mock_configs);

            auto result = config_service.set_config_parameters(1, {bad_update, good_update}, origin);

            REQUIRE(result.status == SetConfigParameterStatus::Ok);
            REQUIRE(result.parameter_results.has_value());
            REQUIRE(result.parameter_results->size() == 2);
            CHECK(result.parameter_results->at(0).status == SetConfigParameterResultEnum::Rejected);
            CHECK_FALSE(result.parameter_results->at(0).status_info.empty());
            CHECK(result.parameter_results->at(1).status == SetConfigParameterResultEnum::WillApplyOnRestart);

            auto persisted_int = storage->get_configuration_parameter(int_param_id);
            REQUIRE(persisted_int.status == everest::config::GetSetResponseStatus::OK);
            CHECK(std::get<int>(persisted_int.configuration_parameter.value().value) == 5);
            auto persisted_str = storage->get_configuration_parameter(rw_param_id);
            REQUIRE(persisted_str.status == everest::config::GetSetResponseStatus::OK);
            CHECK(std::get<std::string>(persisted_str.configuration_parameter.value().value) == "new_value");

            REQUIRE(captured_event.has_value());
            CHECK(captured_event->slot_id == 1);
            REQUIRE(captured_event->updates.size() == 1);
            CHECK(captured_event->updates.front().identifier.configuration_parameter_name == "rw_param");
        }
    }

    SECTION("Set Parameters: WriteOnly handling and SetCallFailed") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig test_module;
        test_module.module_name = "TESTCSTarget";
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
        ConfigParameterUpdate wo_update{{"test_module", "wo_param", "!module"}, "new"};
        ConfigParameterUpdate rw_update{{"test_module", "rw_fail_param", "!module"}, "new"};

        auto result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {wo_update, rw_update}, origin);
        REQUIRE(result.parameter_results.has_value());
        INFO(result.status_info);
        INFO(result.parameter_results->at(0).status_info);
        CHECK(result.parameter_results->at(0).status == SetConfigParameterResultEnum::WillApplyOnRestart);
        INFO(result.parameter_results->at(1).status_info);
        CHECK(result.parameter_results->at(1).status == SetConfigParameterResultEnum::WillApplyOnRestart);
    }

    SECTION("Update handlers may call back into the config service without deadlocking") {
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
        auto lfy_result = config_service.load_from_yaml(valid_yaml, "Test", 0);
        INFO(lfy_result.error_message);
        REQUIRE(lfy_result.success);
        config_service.mark_active_slot(0);

        // Handlers run on the actor thread. Calling back into a public method used to deadlock:
        // the actor would wait on a queued task that only the actor itself could run.
        std::optional<int> slot_id_seen_by_config_handler;
        config_service.register_config_update_handler([&](const ConfigurationUpdate&) {
            slot_id_seen_by_config_handler = config_service.get_active_slot_id();
        });

        std::optional<std::size_t> slots_seen_by_slot_handler;
        config_service.register_active_slot_update_handler([&](const ActiveSlotUpdate&) {
            slots_seen_by_slot_handler = config_service.list_all_slots().size();
        });

        // Publishes an ActiveSlotUpdate -> slot handler re-enters via list_all_slots()
        config_service.reinitialize_from_db(true);
        REQUIRE(slots_seen_by_slot_handler.has_value());
        CHECK(slots_seen_by_slot_handler.value() == 1);

        // Publishes a ConfigurationUpdate -> config handler re-enters via get_active_slot_id()
        ConfigParameterUpdate update{{"dummy_module", "valid_config_entry", "!module"}, "new_value"};
        Origin origin{false, "manager"};
        auto result = config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);
        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(slot_id_seen_by_config_handler.has_value());
        CHECK(slot_id_seen_by_config_handler.value() == 0);
    }

    SECTION("Set Parameters: a persist failure rejects only that parameter, batch continues") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "TESTCSTarget";
        dummy_module.module_id = "dummy_module";

        // rw_param is the parameter whose persistence gets sabotaged below; int_param persists fine
        everest::config::ConfigurationParameter rw_param;
        rw_param.name = "rw_param";
        rw_param.value = "initial_value";
        rw_param.characteristics.datatype = everest::config::Datatype::String;
        rw_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(rw_param);

        everest::config::ConfigurationParameter int_param;
        int_param.name = "int_param";
        int_param.value = 5;
        int_param.characteristics.datatype = everest::config::Datatype::Integer;
        int_param.characteristics.mutability = everest::config::Mutability::ReadWrite;
        dummy_module.configuration_parameters["!module"].push_back(int_param);

        mock_configs["dummy_module"] = dummy_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);

        std::optional<ConfigurationUpdate> captured_event;
        config_service.register_config_update_handler(
            [&captured_event](const ConfigurationUpdate& event) { captured_event = event; });

        // Selectively sabotage persisting rw_param: any attempt to write it to the CONFIGURATION
        // table fails with a status error (not an exception), while other parameters still persist.
        REQUIRE(db->execute_statement("CREATE TRIGGER sabotage_rw_param BEFORE INSERT ON CONFIGURATION "
                                      "WHEN NEW.PARAMETER_NAME = 'rw_param' "
                                      "BEGIN SELECT RAISE(ABORT, 'sabotaged'); END;"));

        everest::config::ConfigurationParameterIdentifier failing_id{"dummy_module", "rw_param", "!module"};
        everest::config::ConfigurationParameterIdentifier good_id{"dummy_module", "int_param", "!module"};
        ConfigParameterUpdate failing_update{failing_id, "new_value"};
        ConfigParameterUpdate good_update{good_id, "7"};
        Origin origin{false, "manager"};

        auto result =
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {failing_update, good_update},
                                                 origin);

        REQUIRE(db->execute_statement("DROP TRIGGER sabotage_rw_param;"));

        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        REQUIRE(result.parameter_results->size() == 2);
        // The unpersistable parameter is rejected - it must not be reported as applying on
        // restart when the database does not actually hold the new value.
        CHECK(result.parameter_results->at(0).status == SetConfigParameterResultEnum::Rejected);
        CHECK(result.parameter_results->at(0).status_info == "Failed to persist change");
        // The rest of the batch is still processed
        CHECK(result.parameter_results->at(1).status == SetConfigParameterResultEnum::WillApplyOnRestart);

        // Database: old value for the failed parameter, new value for the good one
        auto persisted_failed = storage->get_configuration_parameter(failing_id);
        REQUIRE(persisted_failed.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<std::string>(persisted_failed.configuration_parameter.value().value) == "initial_value");
        auto persisted_good = storage->get_configuration_parameter(good_id);
        REQUIRE(persisted_good.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<int>(persisted_good.configuration_parameter.value().value) == 7);

        // Only the persisted update is published
        REQUIRE(captured_event.has_value());
        REQUIRE(captured_event->updates.size() == 1);
        CHECK(captured_event->updates.front().identifier.configuration_parameter_name == "int_param");
        CHECK(captured_event->updates.front().result == SetConfigParameterResultEnum::WillApplyOnRestart);
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
        ConfigParameterUpdate update{param_id, "new_value"};
        Origin origin{false, "manager"};

        // Capture the published config update event to verify its contents
        std::optional<ConfigurationUpdate> captured_event;
        config_service.register_config_update_handler(
            [&captured_event](const ConfigurationUpdate& event) { captured_event = event; });

        auto result = config_service.set_config_parameters(target_slot, {update}, origin);

        // 4. Verify the result and database persistence
        REQUIRE(result.status == SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        CHECK(result.parameter_results->front().status == SetConfigParameterResultEnum::WillApplyOnRestart);

        // The event must target the inactive slot and carry the originating identity
        REQUIRE(captured_event.has_value());
        CHECK(captured_event->slot_id == target_slot);
        CHECK(captured_event->origin.external == false);
        REQUIRE(captured_event->origin.module_id.has_value());
        CHECK(captured_event->origin.module_id.value() == "manager");
        REQUIRE(captured_event->updates.size() == 1);
        CHECK(captured_event->updates.front().identifier.module_id == "inactive_module");
        CHECK(captured_event->updates.front().value == "new_value");
        CHECK(captured_event->updates.front().result == SetConfigParameterResultEnum::WillApplyOnRestart);

        auto get_res = config_service.get_config_parameters(target_slot, {param_id});
        REQUIRE(get_res.status == GetConfigurationStatus::Success);
        REQUIRE(get_res.parameters.size() == 1);
        REQUIRE(get_res.parameters[0].has_value());
        CHECK(std::get<std::string>(get_res.parameters[0]->value) == "new_value");
    }

    SECTION("Edge Cases: set_config_parameters with non-existent parameter") {
        everest::config::ModuleConfigurations mock_configs;
        everest::config::ModuleConfig dummy_module;
        dummy_module.module_name = "TESTCSTarget";
        dummy_module.module_id = "dummy_module";
        mock_configs["dummy_module"] = dummy_module;

        auto storage = std::make_unique<everest::config::SqliteStorage>(db, 0);
        everest::config::SqliteConfigSlotManager slot_manager(db);
        slot_manager.write_config_slot(0, "{}", std::nullopt, "Test Slot");
        storage->write_module_configs(mock_configs);

        config_service.mark_active_slot(0);
        config_service.reinitialize_from_db(true);
        config_service.set_modules_running();

        bool config_event_fired = false;
        config_service.register_config_update_handler(
            [&config_event_fired](const ConfigurationUpdate&) { config_event_fired = true; });

        everest::config::ConfigurationParameterIdentifier unknown_id{"dummy_module", "ghost_param", "!module"};
        ConfigParameterUpdate update{unknown_id, "ghost_value"};
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
        // With no effective updates, publish_config_update() must not be called
        CHECK_FALSE(config_event_fired);
    }
}

TEST_CASE("ConfigServiceCore Concurrency Tests", "[config_service_core][concurrency]") {
    auto db = setup_in_memory_db();
    auto parse_settings = setup_parse_settings();

    // Instantiate with spawn_threads = true (the default) to enable concurrency features
    ConfigServiceCore config_service(parse_settings, db);

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

        // Event-based coordination instead of wall-clock guesses: the callback signals when the
        // actor thread is provably inside it, and blocks until the test releases it. No Catch2
        // assertions in the callback - it runs on the actor thread.
        std::promise<void> callback_entered;
        auto entered = callback_entered.get_future();
        std::promise<void> release;
        auto release_future = release.get_future().share();

        config_service.register_set_runtime_parameter_handler(
            [&callback_entered, release_future](const everest::config::ConfigurationParameterIdentifier&,
                                                const std::string&) {
                callback_entered.set_value();
                // Defensive timeout so a failing test run cannot deadlock the whole suite.
                release_future.wait_for(std::chrono::seconds(10));
                return SetParameterResponse::ModuleReplied_Applied;
            });

        auto slow_call = std::async(std::launch::async, [&]() {
            everest::config::ConfigurationParameterIdentifier param_id{"dummy_module", "valid_config_entry", "!module"};
            ConfigParameterUpdate update{param_id, "slow_val"};
            Origin origin{false, "manager"};
            config_service.set_config_parameters(ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);
        });

        // Deterministic happens-before: after this the actor thread is inside the slow callback.
        const bool actor_is_busy = entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        if (not actor_is_busy) {
            release.set_value(); // unblock whatever can still be unblocked before failing
        }
        REQUIRE(actor_is_busy);

        // This call MUST queue behind the slow set_config_parameters call.
        auto blocked_call = std::async(std::launch::async, [&]() { config_service.set_modules_stopped(); });

        // A correctly serializing actor CANNOT complete this call regardless of machine speed,
        // because completion requires our release below - so this check cannot flake on loaded
        // CI. A broken actor (bypassing the queue) completes the no-op state flip immediately.
        CHECK(blocked_call.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout);

        release.set_value();
        slow_call.get();
        blocked_call.get();
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

TEST_CASE("ConfigServiceCore constructor propagates DB errors instead of aborting", "[config_service_core]") {
    auto db = setup_in_memory_db();
    auto parse_settings = setup_parse_settings();

    // Sabotage the schema so that the initial reload the constructor posts to the actor thread
    // throws (slot_manager_.exists() fails to prepare its statement). The constructor must stop
    // and join the worker thread and rethrow; before that fix, stack unwinding destroyed a
    // joinable std::thread and aborted the whole process via std::terminate.
    REQUIRE(db->execute_statement("DROP TABLE CONFIG_META;"));

    CHECK_THROWS(ConfigServiceCore(parse_settings, db));
}
