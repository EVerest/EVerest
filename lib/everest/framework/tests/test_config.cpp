// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <framework/runtime.hpp>
#include <tests/helpers.hpp>
#include <utils/config.hpp>
#include <utils/config/config_service_core.hpp>
#include <utils/config/slot_manager.hpp>
#include <utils/config/storage_sqlite.hpp>

#include <algorithm>

namespace fs = std::filesystem;

SCENARIO("Check ManagerSettings Constructor", "[!throws]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    GIVEN("An invalid prefix, but a valid config file") {
        THEN("It should throw BootException") {
            CHECK_THROWS_AS(
                Everest::ManagerSettings(bin_dir + "non-valid-prefix/", bin_dir + "valid_config/config.yaml"),
                Everest::BootException);
        }
    }
    GIVEN("A valid prefix, but a non existing config file") {
        THEN("It should throw BootException") {
            CHECK_THROWS_AS(Everest::ManagerSettings(bin_dir + "valid_config/", bin_dir + "non-existing-config.yaml"),
                            Everest::BootException);
        }
    }
    GIVEN("A valid prefix and a valid config file") {
        THEN("It should not throw") {
            CHECK_NOTHROW(Everest::ManagerSettings(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml"));
        }
    }
    GIVEN("A valid prefix and a valid config file with a custom prefix") {
        THEN("It should not throw") {
            auto ms = Everest::ManagerSettings(bin_dir + "valid_config_custom_prefix/usr",
                                               bin_dir + "valid_config_custom_prefix/usr/config.yaml");
            CHECK(ms.runtime_settings.etc_dir == bin_dir + "valid_config_custom_prefix/etc/everest");
        }
    }
    GIVEN("A broken yaml file") {
        // FIXME (aw): this also throws, if the folder doesn't even exists or some other things fail
        THEN("It should throw") {
            CHECK_THROWS(Everest::ManagerSettings(bin_dir + "broken_yaml/", bin_dir + "broken_yaml/config.yaml"));
        }
    }
    GIVEN("A empty yaml file") {
        THEN("It shouldn't throw") {
            CHECK_NOTHROW(Everest::ManagerSettings(bin_dir + "empty_yaml/", bin_dir + "empty_yaml/config.yaml"));
        }
    }
    GIVEN("A empty yaml object file") {
        THEN("It shouldn't throw") {
            CHECK_NOTHROW(
                Everest::ManagerSettings(bin_dir + "empty_yaml_object/", bin_dir + "empty_yaml_object/config.yaml"));
        }
    }
    GIVEN("A null yaml file") {
        THEN("It shouldn't throw") {
            CHECK_NOTHROW(Everest::ManagerSettings(bin_dir + "null_yaml/", bin_dir + "null_yaml/config.yaml"));
        }
    }
    GIVEN("A string yaml file") {
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerSettings(bin_dir + "string_yaml/", bin_dir + "string_yaml/config.yaml"),
                            Everest::BootException);
        }
    }
    GIVEN("A non-exsiting database file") {
        THEN("It should not throw and create the file") {
            Everest::ManagerSettings ms(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml",
                                        bin_dir + "valid_config/non_existing.db");
            CHECK_NOTHROW(Everest::init_database_bootstrap(ms));
        }
    }
}

SCENARIO("Check ManagerSettings without a config file", "[!throws]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    // The empty_yaml fixture uses the filesystem hierarchy standard layout (share/everest/...,
    // etc/everest/default_logging.cfg, libexec/everest/modules) but contains no etc/everest/default.yaml.
    auto prefix = bin_dir + "empty_yaml/";

    GIVEN("A valid prefix without any config file and without a default.yaml") {
        THEN("Construction should not throw (proves there is no default.yaml fallback) and use built-in defaults") {
            auto ms = Everest::ManagerSettings(Everest::ManagerSettings::WithoutConfig{}, prefix, "");
            CHECK(ms.config_file.empty());
            CHECK(ms.config.is_object());
            CHECK(ms.config.empty());
            CHECK(ms.db_dir == fs::path(Everest::defaults::IN_MEMORY_DB_URI));
        }
    }
    GIVEN("A valid prefix without a config file and an explicit database path") {
        auto db_path = bin_dir + "empty_yaml/no_config.db";
        if (fs::exists(db_path)) {
            fs::remove(db_path);
        }
        Everest::ManagerSettings ms(Everest::ManagerSettings::WithoutConfig{}, prefix, db_path);
        CHECK(ms.db_dir == fs::path(db_path));

        THEN("Bootstrap on a fresh database should seed an empty config slot") {
            auto bs = Everest::init_database_bootstrap(ms);
            CHECK(bs.module_configs_initialized == true);

            everest::config::SqliteConfigSlotManager slot_mgr(bs.db_connection);
            const auto boot_slot_id = slot_mgr.get_next_boot_slot_id();
            CHECK(slot_mgr.exists(boot_slot_id));

            auto storage = std::make_unique<everest::config::SqliteStorage>(bs.db_connection, boot_slot_id);
            auto get_mod_cfg_response = storage->get_module_configs();
            CHECK(get_mod_cfg_response.status == everest::config::GenericResponseStatus::OK);
            CHECK(get_mod_cfg_response.module_configs.empty());

            const auto slots = slot_mgr.list_slots();
            REQUIRE(slots.size() == 1);
            CHECK(slots.front().id == boot_slot_id);
            CHECK_FALSE(slots.front().config_file_path.has_value());

            THEN("A second bootstrap (restart) should boot from the now-existing database slot") {
                auto bs2 = Everest::init_database_bootstrap(ms);
                CHECK(bs2.module_configs_initialized == true);
            }
        }
        THEN("Bootstrap with reset-from-yaml should throw, since there is no YAML to re-seed from") {
            CHECK_THROWS_AS(Everest::init_database_bootstrap(ms, true), Everest::BootException);
        }
    }
}

SCENARIO("Check resolve_boot_source", "[!throws]") {
    using Everest::BootMode;
    GIVEN("Neither --config nor --db") {
        THEN("The YAML mode with an in-memory database is selected (default config lookup)") {
            const auto src = Everest::resolve_boot_source("", "", false, false);
            CHECK(src.mode == BootMode::YamlWithInMemoryDb);
            CHECK(src.config_path.empty());
            CHECK(src.db_path.empty());
        }
    }
    GIVEN("Only --config") {
        THEN("The YAML mode with an in-memory database is selected") {
            const auto src = Everest::resolve_boot_source("config.yaml", "", false, false);
            CHECK(src.mode == BootMode::YamlWithInMemoryDb);
            CHECK(src.config_path == "config.yaml");
        }
    }
    GIVEN("Only --db") {
        THEN("The database is the only configuration source") {
            const auto src = Everest::resolve_boot_source("", "everest.db", false, false);
            CHECK(src.mode == BootMode::DatabaseOnly);
            CHECK(src.db_path == "everest.db");
        }
    }
    GIVEN("--config and --db") {
        THEN("The database wins when valid, otherwise it is seeded from YAML") {
            const auto src = Everest::resolve_boot_source("config.yaml", "everest.db", false, false);
            CHECK(src.mode == BootMode::DatabaseWithYamlSeed);
        }
    }
    GIVEN("The deprecated --db-init flag") {
        THEN("With both --config and --db it is a no-op (only warns)") {
            const auto src = Everest::resolve_boot_source("config.yaml", "everest.db", false, true);
            CHECK(src.mode == BootMode::DatabaseWithYamlSeed);
        }
        THEN("Without both options it is ignored (warns), not an error") {
            CHECK(Everest::resolve_boot_source("config.yaml", "", false, true).mode == BootMode::YamlWithInMemoryDb);
            CHECK(Everest::resolve_boot_source("", "everest.db", false, true).mode == BootMode::DatabaseOnly);
            CHECK(Everest::resolve_boot_source("", "", false, true).mode == BootMode::YamlWithInMemoryDb);
        }
    }
    GIVEN("--reset-from-yaml") {
        THEN("Without --config it throws BootException") {
            CHECK_THROWS_AS(Everest::resolve_boot_source("", "everest.db", true, false), Everest::BootException);
            CHECK_THROWS_AS(Everest::resolve_boot_source("", "", true, false), Everest::BootException);
        }
        THEN("With --config it is passed through") {
            CHECK(Everest::resolve_boot_source("config.yaml", "", true, false).reset_from_yaml);
            CHECK(Everest::resolve_boot_source("config.yaml", "everest.db", true, false).reset_from_yaml);
        }
    }
}

SCENARIO("Check database bootstrap with an in-memory database", "[!throws]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";

    GIVEN("A valid config and no database path") {
        Everest::ManagerSettings ms(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml", "");
        // A shared-cache in-memory database persists within this test process for as long as one
        // connection stays open, so give each scenario its own database instead of the default URI.
        ms.db_dir = fs::path("file:test_config_in_memory_bootstrap?mode=memory&cache=shared");

        THEN("Bootstrap seeds the database from YAML and the data survives the bootstrap scopes") {
            const auto bs = Everest::init_database_bootstrap(ms);
            CHECK(bs.module_configs_initialized == true);

            // Reading through fresh storage/slot-manager instances proves the connection keepalive:
            // without it the in-memory database would have been dropped when the bootstrap-internal
            // consumers closed their connection.
            everest::config::SqliteConfigSlotManager slot_mgr(bs.db_connection);
            const auto boot_slot_id = slot_mgr.get_next_boot_slot_id();
            CHECK(slot_mgr.exists(boot_slot_id));

            auto storage = std::make_unique<everest::config::SqliteStorage>(bs.db_connection, boot_slot_id);
            const auto get_mod_cfg_response = storage->get_module_configs();
            CHECK(get_mod_cfg_response.status == everest::config::GenericResponseStatus::OK);

            const auto slots = slot_mgr.list_slots();
            REQUIRE(slots.size() == 1);
            CHECK(slots.front().config_file_path == ms.config_file.string());

            THEN("A second bootstrap while the connection is held boots from the existing slot") {
                const auto bs2 = Everest::init_database_bootstrap(ms);
                CHECK(bs2.module_configs_initialized == true);
            }
        }
    }
}

SCENARIO("Check ManagerConfig Constructor", "[!throws]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    GIVEN("A config without modules") {
        auto ms = Everest::ManagerSettings(bin_dir + "empty_config/", bin_dir + "empty_config/config.yaml");
        auto config = Everest::ManagerConfig(ms);
        THEN("It should not contain the module some_module") {
            CHECK(!config.contains("some_module"));
        }
    }
    GIVEN("A config file referencing a non existent module") {
        auto ms = Everest::ManagerSettings(bin_dir + "missing_module/", bin_dir + "missing_module/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (missing meta data)") {
        auto ms = Everest::ManagerSettings(bin_dir + "broken_manifest_1/", bin_dir + "broken_manifest_1/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (empty file)") {
        auto ms = Everest::ManagerSettings(bin_dir + "broken_manifest_2/", bin_dir + "broken_manifest_2/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            // FIXME: an empty manifest breaks the test?
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (broken module config)") {
        auto ms = Everest::ManagerSettings(bin_dir + "broken_manifest_3/", bin_dir + "broken_manifest_3/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (broken implementation config)") {
        auto ms = Everest::ManagerSettings(bin_dir + "broken_manifest_4/", bin_dir + "broken_manifest_4/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an unknown implementation config") {
        auto ms = Everest::ManagerSettings(bin_dir + "unknown_impls/", bin_dir + "unknown_impls/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an missing config entry") {
        auto ms =
            Everest::ManagerSettings(bin_dir + "missing_config_entry/", bin_dir + "missing_config_entry/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an missing implementation config entry") {
        auto ms = Everest::ManagerSettings(bin_dir + "missing_impl_config_entry/",
                                           bin_dir + "missing_impl_config_entry/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an invalid type of an implementation config entry") {
        auto ms = Everest::ManagerSettings(bin_dir + "invalid_config_entry_type/",
                                           bin_dir + "invalid_config_entry_type/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with an invalid interface (missing "
          "interface)") {
        auto ms = Everest::ManagerSettings(bin_dir + "missing_interface/", bin_dir + "missing_interface/config.yaml");
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(ms), Everest::EverestConfigError);
        }
    }
    GIVEN("A valid config") {
        auto ms = Everest::ManagerSettings(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml");
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(ms));
        }
    }
    GIVEN("A valid config with a valid module") {
        auto ms =
            Everest::ManagerSettings(bin_dir + "valid_module_config/", bin_dir + "valid_module_config/config.yaml");
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(ms));
        }
    }
    GIVEN("A valid config with a valid module and a user-config applied") {
        auto ms = Everest::ManagerSettings(bin_dir + "valid_module_config_userconfig/",
                                           bin_dir + "valid_module_config_userconfig/config.yaml");
        THEN("It should not throw at all") {
            CHECK_NOTHROW([&]() {
                auto mc = Everest::ManagerConfig(ms);
                auto module_configs = mc.get_module_configurations();

                bool found = false;
                const auto config_params = module_configs.at("valid_module").configuration_parameters;
                for (const auto& param : config_params.at("!module")) {
                    if (param.name == "valid_config_entry") {
                        found = true;
                        CHECK(std::get<std::string>(param.value) == "hi");
                    }
                }

                if (!found) {
                    FAIL("Expected configuration parameter 'valid_config_entry' not found.");
                }
            }());
        }
    }
    GIVEN("A valid config with a valid module and enabled schema validation") {
        auto ms = Everest::ManagerSettings(bin_dir + "valid_module_config_validate/",
                                           bin_dir + "valid_module_config_validate/config.yaml");
        THEN("It should not throw at all") {
            CHECK_NOTHROW([&]() {
                auto mc = Everest::ManagerConfig(ms);
                auto interfaces = mc.get_interfaces();
                CHECK(interfaces.size() == 1);
                CHECK(interfaces.contains("TESTValidManifestCmdVar"));
                CHECK(interfaces.at("TESTValidManifestCmdVar").at("main") == "test_interface_cmd_var");
                auto types = mc.get_types();
                CHECK(types.size() == 1);
                CHECK(types.contains("/test_type"));
            }());
        }
    }
    GIVEN("A valid config in legacy json format with a valid module") {
        auto ms = Everest::ManagerSettings(bin_dir + "valid_module_config_json/",
                                           bin_dir + "valid_module_config_json/config.json");
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(ms));
        }
    }
    GIVEN("A config file that does not exist") {
        THEN("It should throw Everest::BootException") {
            CHECK_THROWS_AS(Everest::ManagerSettings(bin_dir + "valid_module_config_json/",
                                                     bin_dir + "valid_module_config_json/config.yaml"),
                            Everest::BootException);
        }
    }
    GIVEN("A valid config in legacy json format with multiple connected valid modules") {
        auto ms =
            Everest::ManagerSettings(bin_dir + "valid_complete_config/", bin_dir + "valid_complete_config/config.json");
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(ms));
        }
    }
    GIVEN("Bootstrap is called two times - first with fallback to init from config file, second with database") {
        auto db_path = bin_dir + "valid_config/everest.db";

        // Clean up before test
        if (fs::exists(db_path)) {
            fs::remove(db_path);
        }
        Everest::ManagerSettings ms(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml", db_path);
        auto bs = Everest::init_database_bootstrap(ms);
        CHECK(bs.module_configs_initialized == true);
        THEN("In the first instantiation the database is not initialized — ManagerConfig parses YAML") {
            auto config = Everest::ManagerConfig(ms);

            THEN("In the second instantiation the database is initialized and valid") {
                auto bs2 = Everest::init_database_bootstrap(ms);
                auto storage = std::make_unique<everest::config::SqliteStorage>(bs2.db_connection);
                auto get_mod_cfg_response = storage->get_module_configs();
                CHECK(get_mod_cfg_response.status == everest::config::GenericResponseStatus::OK);
                CHECK(bs2.module_configs_initialized == true);
                CHECK_NOTHROW(Everest::ManagerConfig(ms, std::move(get_mod_cfg_response.module_configs)));
            }
            THEN("It should be possible to bootstrap again from the initialized database") {
                auto bs3 = Everest::init_database_bootstrap(ms);
                auto storage = std::make_unique<everest::config::SqliteStorage>(bs3.db_connection);
                auto get_mod_cfg_response = storage->get_module_configs();
                CHECK(get_mod_cfg_response.status == everest::config::GenericResponseStatus::OK);
                CHECK(bs3.module_configs_initialized == true);
                CHECK_NOTHROW(Everest::ManagerConfig(ms, std::move(get_mod_cfg_response.module_configs)));
            }
        }
    }
    GIVEN("A YAML config without active_modules and an uninitialized database") {
        auto db_path = bin_dir + "empty_yaml_object/everest.db";
        if (fs::exists(db_path)) {
            fs::remove(db_path);
        }
        Everest::ManagerSettings ms(bin_dir + "empty_yaml_object/", bin_dir + "empty_yaml_object/config.yaml", db_path);
        auto bs = Everest::init_database_bootstrap(ms);
        CHECK(bs.module_configs_initialized == true);
        THEN("Reconstructing ManagerConfig from the (empty) database-backed module configs should not throw, "
             "mirroring what Manager::reload_and_update_context does on restart") {
            auto storage = std::make_unique<everest::config::SqliteStorage>(bs.db_connection);
            auto get_mod_cfg_response = storage->get_module_configs();
            CHECK(get_mod_cfg_response.status == everest::config::GenericResponseStatus::OK);
            CHECK(get_mod_cfg_response.module_configs.empty());
            CHECK_NOTHROW(Everest::ManagerConfig(ms, std::move(get_mod_cfg_response.module_configs)));
        }
    }
}

SCENARIO("Check everest config parsing", "[!throws]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto valid_complete_config_json = bin_dir + "valid_complete_config/config.json";
    GIVEN("A complete and valid config") {
        auto config = Everest::load_yaml(valid_complete_config_json);
        THEN("It should not throw") {
            CHECK_NOTHROW(everest::config::parse_module_configs(config.value("active_modules", json::object())));
        }
    }
    GIVEN("A valid config that misses module connections") {
        auto config = Everest::load_yaml(valid_complete_config_json);
        config["active_modules"]["valid_module_requires"].erase("connections");
        THEN("It should not throw") {
            CHECK_NOTHROW(everest::config::parse_module_configs(config.value("active_modules", json::object())));
        }
    }
    GIVEN("A valid config that misses a mapping") {
        auto config = Everest::load_yaml(valid_complete_config_json);
        config["active_modules"]["valid_module"].erase("mapping");
        THEN("It should not throw") {
            CHECK_NOTHROW(everest::config::parse_module_configs(config.value("active_modules", json::object())));
        }
    }
    GIVEN("A config where a module is missing the 'module' field") {
        auto config = Everest::load_yaml(valid_complete_config_json);
        config["active_modules"]["valid_module"].erase("module");
        THEN("It should throw ConfigParseException for missing 'module'") {
            CHECK_THROWS_AS(everest::config::parse_module_configs(config.value("active_modules", json::object())),
                            ConfigParseException);
        }
    }
    GIVEN("A config with only 'active_modules' and no 'settings'") {
        json config;
        config["active_modules"] = {{"valid_module", {{"module", "TESTValidManifest"}}}};
        THEN("It should not throw and parse default settings") {
            CHECK_NOTHROW(everest::config::parse_module_configs(config.value("active_modules", json::object())));
        }
    }
    GIVEN("A config with empty 'active_modules'") {
        json config;
        config["active_modules"] = json::object(); // empty object
        THEN("It should not throw and result in no modules") {
            auto result = everest::config::parse_module_configs(config.value("active_modules", json::object()));
            CHECK(result.empty());
        }
    }

    GIVEN("A config with unsupported JSON type in configuration parameter") {
        json config;
        config["active_modules"] = {
            {"test_module", {{"module", "test"}, {"config_module", {{"param1", json::array({1, 2, 3})}}}}}};
        THEN("It should throw due to unsupported config parameter type") {
            CHECK_THROWS(everest::config::parse_module_configs(config.value("active_modules", json::object())));
        }
    }
}

json complete_serialized_mod_config(json& serialized_mod_config, Everest::ManagerConfig& mc) {
    serialized_mod_config["interface_definitions"] = mc.get_interface_definitions();
    serialized_mod_config["types"] = mc.get_types();
    serialized_mod_config["module_provides"] = mc.get_interfaces();
    serialized_mod_config["settings"] = mc.get_settings();
    serialized_mod_config["schemas"] = mc.get_schemas();
    serialized_mod_config["module_names"] = mc.get_module_names();
    serialized_mod_config["manifests"] = mc.get_manifests();
    serialized_mod_config["error_map"] = mc.get_error_types();
    return serialized_mod_config;
}

SCENARIO("Check config constructor and functions", "[!throws]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto ms = Everest::ManagerSettings(bin_dir + "two_module_test/", bin_dir + "two_module_test/config.yaml");
    GIVEN("A config with two connected modules") {
        THEN("It should not throw") {
            CHECK_NOTHROW([&]() {
                auto mc = Everest::ManagerConfig(ms);
                auto serialized_mod_config =
                    Everest::get_serialized_module_config("module_a", mc.get_module_configurations());
                complete_serialized_mod_config(serialized_mod_config, mc);
                Everest::MQTTSettings mqtt_settings;
                const auto config = Everest::Config(mqtt_settings, serialized_mod_config);
                config.get_requirement_initialization("module_a");
            }());
        }
    }
}

SCENARIO("Config constructor throws on missing required fields in serialized config", "[Config][throws]") {
    GIVEN("A serialized config missing required fields") {
        Everest::MQTTSettings mqtt_settings;
        Everest::json serialized_config = Everest::json::object();
        serialized_config["module_config"] = Everest::json::object();
        serialized_config["module_config"]["module_a"] = Everest::json::object();

        THEN("It should throw an exception") {
            CHECK_THROWS_AS(Everest::Config(mqtt_settings, serialized_config), json::exception);
        }
    }
}

SCENARIO("Config returns correct module info", "[Config]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto ms = Everest::ManagerSettings(bin_dir + "two_module_test/", bin_dir + "two_module_test/config.yaml");
    auto mc = Everest::ManagerConfig(ms);
    auto serialized = Everest::get_serialized_module_config("module_a", mc.get_module_configurations());
    complete_serialized_mod_config(serialized, mc);
    Everest::MQTTSettings mqtt_settings;

    GIVEN("A valid serialized config") {
        Everest::Config config(mqtt_settings, serialized);

        WHEN("Calling get_module_info") {
            auto info = config.get_module_info("module_a");

            THEN("It should return the correct name and license") {
                CHECK(info.id == "module_a");
                CHECK(info.name == "TESTModuleA");
                CHECK(info.license == "https://opensource.org/licenses/Apache-2.0");
                CHECK(info.authors.at(0) == "author@example.com");
            }
        }
    }
}

SCENARIO("Config returns parsed module configs", "[Config]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto ms = Everest::ManagerSettings(bin_dir + "two_module_test/", bin_dir + "two_module_test/config.yaml");
    auto mc = Everest::ManagerConfig(ms);
    auto serialized = Everest::get_serialized_module_config("module_a", mc.get_module_configurations());
    complete_serialized_mod_config(serialized, mc);
    Everest::MQTTSettings mqtt_settings;
    Everest::Config config(mqtt_settings, serialized);

    GIVEN("A valid config for module_a") {
        auto configs = config.get_module_configs("module_a");

        THEN("It should contain the correct config values") {
            CHECK(configs.find("main") != configs.end());
            CHECK(configs.find("!module") != configs.end());
            CHECK(std::get<std::string>(configs["!module"]["valid_module_config_entry"]) == "test");
            CHECK(std::get<int>(configs["main"]["valid_impl_config_entry"]) == 42);
        }
    }
}

SCENARIO("ConfigurationParameterCharacteristics serialization of min_value and max_value", "[types]") {
    using everest::config::ConfigurationParameterCharacteristics;
    using everest::config::Datatype;
    using everest::config::Mutability;

    GIVEN("A characteristics struct with min_value and max_value set") {
        ConfigurationParameterCharacteristics c;
        c.datatype = Datatype::Integer;
        c.mutability = Mutability::ReadWrite;
        c.min_value = -10;
        c.max_value = 100;

        WHEN("Serialized to JSON") {
            nlohmann::json j = c;

            THEN("min_value and max_value appear in the JSON") {
                REQUIRE(j.contains("min_value"));
                REQUIRE(j.contains("max_value"));
                CHECK(j["min_value"].get<int32_t>() == -10);
                CHECK(j["max_value"].get<int32_t>() == 100);
            }
        }

        WHEN("Round-tripped through JSON") {
            nlohmann::json j = c;
            auto c2 = j.get<ConfigurationParameterCharacteristics>();

            THEN("min_value and max_value are preserved") {
                REQUIRE(c2.min_value.has_value());
                REQUIRE(c2.max_value.has_value());
                CHECK(c2.min_value.value() == -10);
                CHECK(c2.max_value.value() == 100);
            }
        }
    }

    GIVEN("A characteristics struct without min_value or max_value") {
        ConfigurationParameterCharacteristics c;
        c.datatype = Datatype::String;
        c.mutability = Mutability::ReadOnly;

        WHEN("Serialized to JSON") {
            nlohmann::json j = c;

            THEN("min_value and max_value are absent from the JSON") {
                CHECK_FALSE(j.contains("min_value"));
                CHECK_FALSE(j.contains("max_value"));
            }
        }

        WHEN("Round-tripped through JSON") {
            nlohmann::json j = c;
            auto c2 = j.get<ConfigurationParameterCharacteristics>();

            THEN("min_value and max_value remain nullopt") {
                CHECK_FALSE(c2.min_value.has_value());
                CHECK_FALSE(c2.max_value.has_value());
            }
        }
    }

    GIVEN("A JSON object with only min_value set") {
        nlohmann::json j = {{"datatype", "integer"}, {"mutability", "ReadWrite"}, {"min_value", 5}};

        WHEN("Deserialized") {
            auto c = j.get<ConfigurationParameterCharacteristics>();

            THEN("min_value is populated and max_value is absent") {
                REQUIRE(c.min_value.has_value());
                CHECK(c.min_value.value() == 5);
                CHECK_FALSE(c.max_value.has_value());
            }
        }
    }

    GIVEN("A JSON object with only max_value set") {
        nlohmann::json j = {{"datatype", "integer"}, {"mutability", "ReadWrite"}, {"max_value", 50}};

        WHEN("Deserialized") {
            auto c = j.get<ConfigurationParameterCharacteristics>();

            THEN("max_value is populated and min_value is absent") {
                CHECK_FALSE(c.min_value.has_value());
                REQUIRE(c.max_value.has_value());
                CHECK(c.max_value.value() == 50);
            }
        }
    }

    GIVEN("A characteristics struct with unit, min_value and max_value") {
        ConfigurationParameterCharacteristics c;
        c.datatype = Datatype::Integer;
        c.mutability = Mutability::ReadWrite;
        c.unit = "ms";
        c.min_value = 0;
        c.max_value = 60000;

        WHEN("Serialized to JSON") {
            nlohmann::json j = c;

            THEN("All optional fields appear") {
                CHECK(j.contains("unit"));
                CHECK(j.contains("min_value"));
                CHECK(j.contains("max_value"));
                CHECK(j["unit"].get<std::string>() == "ms");
                CHECK(j["min_value"].get<int32_t>() == 0);
                CHECK(j["max_value"].get<int32_t>() == 60000);
            }
        }
    }
}

namespace {
/// The undeclared keys of one group of `module_id`, or an empty list when the
/// group has none.
std::vector<std::string> undeclared_keys_of(const Everest::ManagerConfig& mc, const std::string& module_id,
                                            const std::string& group) {
    const auto& undeclared =
        mc.get_module_configurations().at(module_id).undeclared_configuration_parameters;
    const auto it = undeclared.find(group);
    if (it == undeclared.end()) {
        return {};
    }
    return it->second;
}

/// The value of one declared parameter of one group of `module_id`, by name.
everest::config::ConfigEntry declared_value_of(const Everest::ManagerConfig& mc, const std::string& module_id,
                                               const std::string& group, const std::string& name) {
    const auto& parameters = mc.get_module_configurations().at(module_id).configuration_parameters.at(group);
    const auto it = std::find_if(parameters.begin(), parameters.end(),
                                 [&name](const everest::config::ConfigurationParameter& parameter) {
                                     return parameter.name == name;
                                 });
    REQUIRE(it != parameters.end());
    return it->value;
}
} // namespace

SCENARIO("Config reports undeclared config keys per group", "[Config]") {
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto ms = Everest::ManagerSettings(bin_dir + "undeclared_config/", bin_dir + "undeclared_config/config.yaml");
    auto mc = Everest::ManagerConfig(ms);

    GIVEN("A config supplying an undeclared key in the module group and in a provided interface's group") {
        THEN("Each group reports its own key and nothing else") {
            CHECK(undeclared_keys_of(mc, "module_a", "!module") ==
                  std::vector<std::string>{"a_key_the_manifest_does_not_declare"});
            CHECK(undeclared_keys_of(mc, "module_a", "main") ==
                  std::vector<std::string>{"an_undeclared_impl_key"});
        }

        THEN("A module that declares no undeclared keys reports none") {
            CHECK(undeclared_keys_of(mc, "module_b", "!module").empty());
        }

        THEN("The parsed config carries neither key, which is why the names are reported at all") {
            const auto& parameters = mc.get_module_configurations().at("module_a").configuration_parameters;
            for (const auto& [group, group_parameters] : parameters) {
                for (const auto& parameter : group_parameters) {
                    CHECK(parameter.name != "a_key_the_manifest_does_not_declare");
                    CHECK(parameter.name != "an_undeclared_impl_key");
                }
            }
        }

        THEN("They survive the serialization a module receives its config through") {
            auto serialized = Everest::get_serialized_module_config("module_a", mc.get_module_configurations());
            complete_serialized_mod_config(serialized, mc);
            Everest::MQTTSettings mqtt_settings;
            const Everest::Config config(mqtt_settings, serialized);

            const auto& undeclared = config.get_module_config().undeclared_configuration_parameters;
            CHECK(undeclared.at("!module") ==
                  std::vector<std::string>{"a_key_the_manifest_does_not_declare"});
            CHECK(undeclared.at("main") == std::vector<std::string>{"an_undeclared_impl_key"});
        }
    }
}

/// The database boot path as Manager::reload_and_update_context takes it
/// (`src/manager.cpp:792-797`): the config service reloads from the database and
/// hands its module configurations to the preloaded ManagerConfig constructor.
/// ManagerConfig(ms) is the YAML constructor and reads no database at all, so a
/// scenario about a database boot cannot use it.
static Everest::ManagerConfig
boot_from_database(const Everest::ManagerSettings& ms,
                   std::shared_ptr<everest::db::sqlite::ConnectionInterface> db_connection) {
    Everest::config::ConfigServiceCore core(ms, std::move(db_connection));
    core.reinitialize_from_db();
    everest::config::ModuleConfigurations module_configs = *core.get_active_module_configurations();
    return Everest::ManagerConfig(ms, std::move(module_configs));
}

SCENARIO("Config reports undeclared config keys on the database boot path", "[Config]") {
    // The set is not stored: it is recomputed against the manifest by
    // load_and_validate_manifest, which runs whatever the config was loaded
    // from. So what a database boot has to catch is a stored key the manifest
    // stopped declaring, which is what a module upgrade leaves behind.
    //
    // The database is this scenario's precondition and not its subject, so it is
    // seeded directly rather than imported from a YAML. A config file still sits
    // in the prefix and names two undeclared keys of its own, unused by this
    // boot: seeing either of them below would mean this read the YAML instead.
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    const auto prefix = bin_dir + "undeclared_config_db/";
    const auto db = prefix + "config.db";
    fs::remove(db);

    GIVEN("A stored config naming keys the manifest does not declare") {
        Everest::ManagerSettings ms(Everest::ManagerSettings::WithoutConfig{}, prefix, db);
        const auto bs = Everest::init_database_bootstrap(ms);
        REQUIRE(bs.module_configs_initialized);

        everest::config::SqliteConfigSlotManager slot_mgr(bs.db_connection);
        const auto boot_slot_id = slot_mgr.get_next_boot_slot_id();
        everest::config::SqliteStorage storage(bs.db_connection, boot_slot_id);

        everest::config::ConfigurationParameterCharacteristics string_param;
        string_param.datatype = everest::config::Datatype::String;
        string_param.mutability = everest::config::Mutability::ReadOnly;

        everest::config::ConfigurationParameterCharacteristics integer_param;
        integer_param.datatype = everest::config::Datatype::Integer;
        integer_param.mutability = everest::config::Mutability::ReadOnly;

        everest::config::ConfigurationParameterCharacteristics boolean_param;
        boolean_param.datatype = everest::config::Datatype::Boolean;
        boolean_param.mutability = everest::config::Mutability::ReadOnly;

        // One key the manifest declares and one it does not, in each of the two
        // groups module_a owns: its own, and the group of the interface it
        // provides. The declared pair is what proves a boot that reports the
        // undeclared names has not simply dropped everything.
        everest::config::ConfigurationParameter declared_module;
        declared_module.name = "valid_module_config_entry";
        declared_module.value = std::string("test");
        declared_module.characteristics = string_param;

        everest::config::ConfigurationParameter dropped_module;
        dropped_module.name = "a_stored_key_the_manifest_dropped";
        dropped_module.value = true;
        dropped_module.characteristics = boolean_param;

        everest::config::ConfigurationParameter declared_impl;
        declared_impl.name = "valid_impl_config_entry";
        declared_impl.value = 42;
        declared_impl.characteristics = integer_param;

        everest::config::ConfigurationParameter dropped_impl;
        dropped_impl.name = "a_stored_impl_key_the_manifest_dropped";
        dropped_impl.value = true;
        dropped_impl.characteristics = boolean_param;

        everest::config::ModuleConfig module_a;
        module_a.module_name = "TESTModuleA";
        module_a.configuration_parameters["!module"] = {declared_module, dropped_module};
        module_a.configuration_parameters["main"] = {declared_impl, dropped_impl};

        // TESTModuleA requires req1 with min_connections 1, so the stored config
        // has to fulfill it or validation fails before any key is looked at.
        Fulfillment req1_fulfillment;
        req1_fulfillment.module_id = "module_b";
        req1_fulfillment.implementation_id = "impl1";
        req1_fulfillment.requirement = {"req1", 0};
        module_a.connections["req1"] = {req1_fulfillment};

        everest::config::ModuleConfig module_b;
        module_b.module_name = "TESTModuleB";

        everest::config::ModuleConfigurations module_configs;
        module_configs["module_a"] = module_a;
        module_configs["module_b"] = module_b;

        REQUIRE(storage.write_module_configs(module_configs) == everest::config::GenericResponseStatus::OK);

        THEN("A boot from the database reports them, group by group") {
            auto mc = boot_from_database(ms, bs.db_connection);

            CHECK(undeclared_keys_of(mc, "module_a", "!module") ==
                  std::vector<std::string>{"a_stored_key_the_manifest_dropped"});
            CHECK(undeclared_keys_of(mc, "module_a", "main") ==
                  std::vector<std::string>{"a_stored_impl_key_the_manifest_dropped"});

            // Only the stored keys. The config file in the prefix supplies two
            // undeclared keys of its own and a database boot never reads it, so
            // seeing either of those would mean this booted from the YAML.
            const auto module_group = undeclared_keys_of(mc, "module_a", "!module");
            const auto impl_group = undeclared_keys_of(mc, "module_a", "main");
            CHECK(std::find(module_group.begin(), module_group.end(),
                            "a_key_the_manifest_does_not_declare") == module_group.end());
            CHECK(std::find(impl_group.begin(), impl_group.end(), "an_undeclared_impl_key") ==
                  impl_group.end());
        }

        THEN("A boot from the database still delivers the declared keys of both groups") {
            // The recompute rebuilds the parsed parameters as well as the
            // undeclared names, so a database boot must not lose or misfile
            // what the manifest does declare.
            auto mc = boot_from_database(ms, bs.db_connection);

            const auto& module_configs_read = mc.get_module_configurations();
            const auto module_a_read = module_configs_read.find("module_a");
            REQUIRE(module_a_read != module_configs_read.end());

            const auto& params = module_a_read->second.configuration_parameters;
            const auto module_group = params.find("!module");
            REQUIRE(module_group != params.end());
            const auto impl_group = params.find("main");
            REQUIRE(impl_group != params.end());

            const auto has = [](const auto& group, const std::string& name) {
                return std::any_of(group.begin(), group.end(),
                                   [&name](const auto& p) { return p.name == name; });
            };
            CHECK(has(module_group->second, "valid_module_config_entry"));
            CHECK(has(impl_group->second, "valid_impl_config_entry"));
        }
    }
}
SCENARIO("Config reports an undeclared key of a provided interface with an empty module group", "[Config]") {
    // The module's own group carries nothing undeclared and the interface's
    // group carries one key. Reporting it under the module's group would tell a
    // module it was handed a key it was not.
    auto bin_dir = Everest::tests::get_bin_dir().string() + "/";
    auto ms = Everest::ManagerSettings(bin_dir + "undeclared_config_impl_only/",
                                       bin_dir + "undeclared_config_impl_only/config.yaml");
    auto mc = Everest::ManagerConfig(ms);

    GIVEN("A config whose only undeclared key sits under a provided interface") {
        THEN("The module group reports nothing and the interface group reports the key") {
            CHECK(undeclared_keys_of(mc, "module_a", "!module").empty());
            CHECK(undeclared_keys_of(mc, "module_a", "main") ==
                  std::vector<std::string>{"an_undeclared_impl_key"});
        }

        THEN("The declared keys of both groups are untouched") {
            CHECK(std::get<std::string>(
                      declared_value_of(mc, "module_a", "!module", "valid_module_config_entry")) == "test");
            CHECK(std::get<int>(declared_value_of(mc, "module_a", "main", "valid_impl_config_entry")) == 42);
        }
    }
}
