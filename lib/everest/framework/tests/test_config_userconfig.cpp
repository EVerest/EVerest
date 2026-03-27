// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <filesystem>

#include <catch2/catch_all.hpp>

#include <everest/utils/yaml_loader.hpp>
#include <framework/runtime.hpp>
#include <tests/helpers.hpp>
#include <utils/config.hpp>
#include <utils/config/config_service_core.hpp>
#include <utils/config/storage_userconfig.hpp>

using namespace everest::config;

namespace fs = std::filesystem;

namespace {
// The fixture directories are assembled into the build tree at CMake configure time, so tests that
// write files must work on a copy in a temporary directory to keep re-runs reproducible.
fs::path make_temp_dir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}
} // namespace

TEST_CASE("UserConfigStorage::write_configuration_parameter", "[userconfig_storage]") {
    const auto tmp_dir = make_temp_dir("everest_userconfig_storage_test");
    const auto user_config_path = tmp_dir / "config.yaml";

    UserConfigStorage storage(user_config_path); // the file does not exist yet

    ConfigurationParameterCharacteristics string_characteristics;
    string_characteristics.datatype = Datatype::String;
    string_characteristics.mutability = Mutability::ReadWrite;

    ConfigurationParameterIdentifier id;
    id.module_id = "some_module";
    id.configuration_parameter_name = "some_param";

    SECTION("Module-level write ends up in config_module and is persisted to disk") {
        REQUIRE(storage.write_configuration_parameter(id, string_characteristics, "some_value") ==
                GetSetResponseStatus::OK);

        const auto& user_config = storage.get_user_config();
        REQUIRE(user_config.at("active_modules").at("some_module").at("config_module").at("some_param") ==
                "some_value");

        REQUIRE(fs::exists(user_config_path));
        const nlohmann::json on_disk = Everest::load_yaml(user_config_path);
        CHECK(on_disk == user_config);
    }

    SECTION("Explicit '!module' implementation id also ends up in config_module") {
        id.module_implementation_id = "!module";
        REQUIRE(storage.write_configuration_parameter(id, string_characteristics, "some_value") ==
                GetSetResponseStatus::OK);

        const auto& user_config = storage.get_user_config();
        CHECK(user_config.at("active_modules").at("some_module").at("config_module").at("some_param") == "some_value");
        CHECK(not user_config.at("active_modules").at("some_module").contains("config_implementation"));
    }

    SECTION("Implementation-level write ends up in config_implementation") {
        id.module_implementation_id = "main";
        REQUIRE(storage.write_configuration_parameter(id, string_characteristics, "some_value") ==
                GetSetResponseStatus::OK);

        const auto& user_config = storage.get_user_config();
        CHECK(user_config.at("active_modules")
                  .at("some_module")
                  .at("config_implementation")
                  .at("main")
                  .at("some_param") == "some_value");
        CHECK(not user_config.at("active_modules").at("some_module").contains("config_module"));
    }

    SECTION("Values are parsed according to the characteristics' datatype") {
        ConfigurationParameterCharacteristics characteristics;
        characteristics.mutability = Mutability::ReadWrite;

        characteristics.datatype = Datatype::String;
        id.configuration_parameter_name = "string_param";
        REQUIRE(storage.write_configuration_parameter(id, characteristics, "value") == GetSetResponseStatus::OK);

        characteristics.datatype = Datatype::Integer;
        id.configuration_parameter_name = "integer_param";
        REQUIRE(storage.write_configuration_parameter(id, characteristics, "42") == GetSetResponseStatus::OK);

        characteristics.datatype = Datatype::Decimal;
        id.configuration_parameter_name = "decimal_param";
        REQUIRE(storage.write_configuration_parameter(id, characteristics, "42.23") == GetSetResponseStatus::OK);

        characteristics.datatype = Datatype::Boolean;
        id.configuration_parameter_name = "boolean_param";
        REQUIRE(storage.write_configuration_parameter(id, characteristics, "true") == GetSetResponseStatus::OK);

        const auto& config_module =
            storage.get_user_config().at("active_modules").at("some_module").at("config_module");
        CHECK(config_module.at("string_param").is_string());
        CHECK(config_module.at("string_param") == "value");
        CHECK(config_module.at("integer_param").is_number_integer());
        CHECK(config_module.at("integer_param") == 42);
        CHECK(config_module.at("decimal_param").is_number_float());
        CHECK(config_module.at("decimal_param") == 42.23);
        CHECK(config_module.at("boolean_param").is_boolean());
        CHECK(config_module.at("boolean_param") == true);
    }

    SECTION("Writing the same parameter again overwrites the value") {
        REQUIRE(storage.write_configuration_parameter(id, string_characteristics, "first_value") ==
                GetSetResponseStatus::OK);
        REQUIRE(storage.write_configuration_parameter(id, string_characteristics, "second_value") ==
                GetSetResponseStatus::OK);

        CHECK(storage.get_user_config().at("active_modules").at("some_module").at("config_module").at("some_param") ==
              "second_value");

        const nlohmann::json on_disk = Everest::load_yaml(user_config_path);
        CHECK(on_disk.at("active_modules").at("some_module").at("config_module").at("some_param") == "second_value");
    }

    SECTION("Writes merge into an existing user-config instead of replacing it") {
        const nlohmann::json seed = {{"active_modules", {{"other_module", {{"config_module", {{"other_param", 1}}}}}}}};
        Everest::save_yaml(seed, user_config_path);

        UserConfigStorage seeded_storage(user_config_path);
        REQUIRE(seeded_storage.write_configuration_parameter(id, string_characteristics, "some_value") ==
                GetSetResponseStatus::OK);

        const nlohmann::json on_disk = Everest::load_yaml(user_config_path);
        CHECK(on_disk.at("active_modules").at("other_module").at("config_module").at("other_param") == 1);
        CHECK(on_disk.at("active_modules").at("some_module").at("config_module").at("some_param") == "some_value");
    }
}

// This covers the config-set path of a manager running without --db: ConfigServiceCore persists
// active-slot writes to the in-memory database AND to the user-config YAML via its persistence
// mirror (the successor of the old ManagerConfig::set_config_value() YamlFile-mode path). The
// user-config merge of the next config parse closes the loop, so the write applies on restart.
TEST_CASE("ConfigServiceCore persists writes to the user-config via its mirror", "[userconfig_storage]") {
    const auto tmp_dir = make_temp_dir("everest_userconfig_manager_test");
    fs::copy(Everest::tests::get_bin_dir() / "valid_module_config_userconfig", tmp_dir, fs::copy_options::recursive);

    const auto prefix = tmp_dir.string() + "/";
    const auto config_file = (tmp_dir / "config.yaml").string();
    const auto user_config_file = tmp_dir / "user-config" / "config.yaml";

    // No --db: the manager would run on an in-memory database seeded from the YAML. A shared-cache
    // in-memory database persists within this test process for as long as one connection stays
    // open, so use a test-unique URI instead of the default one.
    Everest::ManagerSettings ms(prefix, config_file, "");
    ms.db_dir = fs::path("file:test_userconfig_mirror?mode=memory&cache=shared");
    const auto bs = Everest::init_database_bootstrap(ms);
    REQUIRE(bs.module_configs_initialized);

    Everest::config::ConfigServiceCore core(ms, bs.db_connection,
                                            std::make_unique<UserConfigStorage>(user_config_file));

    ConfigurationParameterIdentifier id;
    id.module_id = "valid_module";
    id.configuration_parameter_name = "valid_config_entry";

    const Everest::config::Origin origin{true, std::nullopt};

    SECTION("Setting a config value writes it to the user-config file") {
        const Everest::config::ConfigParameterUpdate update{id, "new_value"};
        const auto result =
            core.set_config_parameters(Everest::config::ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);
        REQUIRE(result.status == Everest::config::SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        INFO(result.parameter_results->front().status_info);
        CHECK(result.parameter_results->front().status ==
              Everest::config::SetConfigParameterResultEnum::WillApplyOnRestart);

        const nlohmann::json on_disk = Everest::load_yaml(user_config_file);
        CHECK(on_disk.at("active_modules").at("valid_module").at("config_module").at("valid_config_entry") ==
              "new_value");

        // a freshly parsed config (as on the next boot) picks up the new value via the user-config merge
        auto ms2 = Everest::ManagerSettings(prefix, config_file);
        auto mc2 = Everest::ManagerConfig(ms2);
        const auto& config_params =
            mc2.get_module_configurations().at("valid_module").configuration_parameters.at("!module");
        const auto it = std::find_if(config_params.begin(), config_params.end(),
                                     [](const auto& param) { return param.name == "valid_config_entry"; });
        REQUIRE(it != config_params.end());
        CHECK(std::get<std::string>(it->value) == "new_value");
    }

    SECTION("Setting an unknown parameter is rejected and does not touch the user-config file") {
        const nlohmann::json before = Everest::load_yaml(user_config_file);

        id.configuration_parameter_name = "unknown_parameter";
        const Everest::config::ConfigParameterUpdate update{id, "value"};
        const auto result =
            core.set_config_parameters(Everest::config::ConfigServiceInterface::ACTIVE_SLOT, {update}, origin);
        REQUIRE(result.status == Everest::config::SetConfigParameterStatus::Ok);
        REQUIRE(result.parameter_results.has_value());
        CHECK(result.parameter_results->front().status == Everest::config::SetConfigParameterResultEnum::DoesNotExist);

        const nlohmann::json after = Everest::load_yaml(user_config_file);
        CHECK(after == before);
    }
}
