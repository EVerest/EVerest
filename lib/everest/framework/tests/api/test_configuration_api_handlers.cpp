// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Drives ConfigurationAPI through its registered MQTT handlers (as the manager's message
// dispatcher would), backed by a FakeConfigService and the recording MockMQTTAbstraction.

#include <string>

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <configuration_api.hpp>
#include <tests/mock_mqtt_abstraction.hpp>

#include "api_test_helpers.hpp"
#include "fake_config_service.hpp"

using json = nlohmann::json;
using namespace Everest::tests;
using Everest::api::configuration::ConfigurationAPI;

namespace {

// The command topic ConfigurationAPI registers for <command>.
std::string command_topic(const std::string& command) {
    return "everest_api/1/configuration/m2e/" + command;
}

void invoke_command(MockMQTTAbstraction& mock, const std::string& command, const json& request) {
    invoke(mock, command_topic(command), request);
}

/// \brief A ModuleConfig with every optional branch of the conversion populated.
///
/// Deliberately exercises what a minimal config would not: a module-level and per-implementation
/// tier mapping (one with and one without a connector), a requirement fulfilled once and one
/// fulfilled three times, module-level ("!module") and implementation-scoped parameters, all four
/// datatypes, telemetry and the access-control block.
everest::config::ModuleConfig make_populated_module_config() {
    using namespace everest::config;

    ModuleConfig config;
    config.module_id = "module_id";
    config.module_name = "Module Name";
    config.standalone = true;
    config.capabilities = std::nullopt;
    config.telemetry_enabled = true;
    config.telemetry_config = std::make_optional<TelemetryConfig>(5);

    ConfigurationParameterCharacteristics integer_characteristics;
    integer_characteristics.datatype = Datatype::Integer;
    integer_characteristics.mutability = Mutability::ReadWrite;
    integer_characteristics.unit = "ms";

    ConfigurationParameterCharacteristics string_characteristics;
    string_characteristics.datatype = Datatype::String;
    string_characteristics.mutability = Mutability::ReadOnly;

    ConfigurationParameterCharacteristics decimal_characteristics;
    decimal_characteristics.datatype = Datatype::Decimal;
    decimal_characteristics.mutability = Mutability::ReadWrite;

    ConfigurationParameterCharacteristics boolean_characteristics;
    boolean_characteristics.datatype = Datatype::Boolean;
    boolean_characteristics.mutability = Mutability::WriteOnly;

    ConfigurationParameter integer_param;
    integer_param.name = "integer_param";
    integer_param.value = 10;
    integer_param.characteristics = integer_characteristics;

    ConfigurationParameter string_param;
    string_param.name = "string_param";
    string_param.value = std::string("example_value");
    string_param.characteristics = string_characteristics;

    ConfigurationParameter decimal_param;
    decimal_param.name = "decimal_param";
    decimal_param.value = 42.23;
    decimal_param.characteristics = decimal_characteristics;

    ConfigurationParameter boolean_param;
    boolean_param.name = "boolean_param";
    boolean_param.value = true;
    boolean_param.characteristics = boolean_characteristics;

    config.configuration_parameters["!module"] = {integer_param, decimal_param, boolean_param};
    config.configuration_parameters["impl_1"] = {string_param};

    Fulfillment f1{"module_a", "impl_a1", {"conn1", 0}};
    Fulfillment f2{"module_b", "impl_b1", {"conn2", 0}};
    Fulfillment f3{"module_c", "impl_c1", {"conn2", 1}};
    config.connections = {
        {"conn1", {f1}},
        {"conn2", {f2, f3}},
    };

    ModuleTierMappings mappings;
    mappings.module = std::make_optional<Mapping>(1, 2);
    mappings.implementations = {
        {"impl_1", std::make_optional<Mapping>(3)},
    };
    config.mapping = mappings;

    ModuleConfigAccess module_access;
    module_access.allow_read = true;
    module_access.allow_write = false;
    module_access.allow_set_read_only = true;
    ConfigAccess access;
    access.allow_global_read = false;
    access.allow_global_write = true;
    access.allow_set_read_only = false;
    access.modules["other_module_id"] = module_access;
    config.access = {access};

    return config;
}

} // namespace

// One section per command. Each drives the handler the API registered for that command, so a missing
// or misspelled registration fails here too (invoke() REQUIREs the handler exists), and asserts the
// reply's on-the-wire field names against the spec.
TEST_CASE("ConfigurationAPI happy-path reply shapes", "[configuration_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;

    SECTION("list_all_slots -> slots with their metadata") {
        FakeConfigService::SlotInfo slot;
        slot.id = 2;
        slot.last_updated = "2026-07-31T00:00:00Z";
        slot.description = "my slot";
        slot.config_file_path = "/etc/everest/config.yaml";
        svc.slots = {slot};

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "list_all_slots", make_request(json::object()));

        REQUIRE(svc.list_all_slots_calls == 1);
        REQUIRE(mock.published().size() == 1);
        CHECK(mock.published().back().first == REPLY_TO);
        const auto reply = last_reply(mock);
        REQUIRE(reply.at("slots").size() == 1);
        const auto& entry = reply.at("slots").at(0);
        CHECK(entry.at("slot_id") == 2);
        CHECK(entry.at("last_updated") == "2026-07-31T00:00:00Z");
        CHECK(entry.at("description") == "my slot");
        CHECK(entry.at("config_file_path") == "/etc/everest/config.yaml");
    }

    SECTION("get_active_slot -> active and next-boot slot ids") {
        svc.active_slot_id = 1;
        svc.next_boot_slot_id = 2;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "get_active_slot", make_request(json::object()));

        const auto reply = last_reply(mock);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(reply.at("active_slot_id") == 1);
        CHECK(reply.at("next_boot_slot_id") == 2);
    }

    SECTION("mark_active_slot -> the internal status as 'result'") {
        svc.mark_active_slot_result = FakeConfigService::SetActiveSlotStatus::Success;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "mark_active_slot", make_request(json{{"slot_id", 1}}));

        REQUIRE(svc.mark_active_slot_calls == 1);
        REQUIRE(mock.published().size() == 1);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(last_reply(mock).at("result") == "Success");
    }

    SECTION("delete_slot -> the internal status as 'result'") {
        svc.delete_slot_result = FakeConfigService::DeleteSlotStatus::CannotDeleteActiveSlot;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "delete_slot", make_request(json{{"slot_id", 1}}));

        REQUIRE(svc.delete_slot_calls == 1);
        CHECK(last_reply(mock).at("result") == "CannotDeleteActiveSlot");
    }

    SECTION("duplicate_slot -> success and the new slot id") {
        svc.duplicate_slot_result = FakeConfigService::DuplicateSlotResult{true, 7};

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "duplicate_slot", make_request(json{{"slot_id", 1}, {"new_description", "a copy"}}));

        REQUIRE(svc.duplicate_slot_calls == 1);
        const auto reply = last_reply(mock);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(reply.at("success") == true);
        CHECK(reply.at("slot_id") == 7);
    }

    SECTION("load_from_yaml -> success and the slot the config landed in") {
        FakeConfigService::LoadFromYamlResult result;
        result.success = true;
        result.slot_id = 3;
        svc.load_from_yaml_result = result;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "load_from_yaml", make_request(json{{"raw_yaml", "active_modules: {}"}}));

        REQUIRE(svc.load_from_yaml_calls == 1);
        const auto reply = last_reply(mock);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(reply.at("success") == true);
        CHECK(reply.at("slot_id") == 3);
    }

    SECTION("load_from_yaml failure -> success=false carries the service's error message") {
        FakeConfigService::LoadFromYamlResult result;
        result.success = false;
        result.error_message = "could not parse yaml";
        svc.load_from_yaml_result = result;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "load_from_yaml", make_request(json{{"raw_yaml", "not: [valid"}}));

        const auto reply = last_reply(mock);
        CHECK(reply.at("success") == false);
        CHECK(reply.at("error_message") == "could not parse yaml");
    }

    SECTION("set_description -> success") {
        svc.set_description_result = true;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "set_description", make_request(json{{"slot_id", 1}, {"description", "renamed"}}));

        REQUIRE(svc.set_description_calls == 1);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(last_reply(mock).at("success") == true);
    }

    SECTION("set_config_parameters -> one result per requested update, in order") {
        FakeConfigService::SetConfigParameterResult result;
        result.status = Everest::config::SetConfigParameterStatus::Ok;
        result.parameter_results = std::vector<Everest::config::SetConfigPerParameterResult>{
            {Everest::config::SetConfigParameterResultEnum::Applied, ""},
            {Everest::config::SetConfigParameterResultEnum::WillApplyOnRestart, ""},
        };
        svc.set_config_parameters_result = result;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        const json first = {{"cfg_param_id", {{"module_id", "m"}, {"parameter_name", "a"}}}, {"value", "1"}};
        const json second = {{"cfg_param_id", {{"module_id", "m"}, {"parameter_name", "b"}}}, {"value", "2"}};
        invoke_command(mock, "set_config_parameters",
                       make_request(json{{"slot_id", 0}, {"parameter_updates", json::array({first, second})}}));

        REQUIRE(svc.set_config_parameters_calls == 1);
        const auto reply = last_reply(mock);
        CHECK(mock.published().back().first == REPLY_TO);
        REQUIRE(reply.at("results").size() == 2);
        CHECK(reply.at("results").at(0) == "Applied");
        CHECK(reply.at("results").at(1) == "WillApplyOnRestart");
    }

    SECTION("get_config_parameters -> status, per-parameter results, and the request is forwarded") {
        everest::config::ConfigurationParameter found;
        found.name = "log_interval";
        found.value = 3;
        found.characteristics.datatype = everest::config::Datatype::Integer;
        found.characteristics.mutability = everest::config::Mutability::ReadWrite;

        FakeConfigService::GetConfigParametersResult result;
        result.status = Everest::config::GetConfigurationStatus::Success;
        // second entry is nullopt: the parameter does not exist in that slot
        result.parameters = {found, std::nullopt};
        svc.get_config_parameters_result = result;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        const json wanted = {{"module_id", "example"}, {"parameter_name", "log_interval"}};
        const json missing = {{"module_id", "example"}, {"parameter_name", "nope"}, {"implementation_id", "example"}};
        invoke_command(
            mock, "get_config_parameters",
            make_request(
                json{{"slot_id", 4}, {"parameters", json::array({wanted, missing})}, {"force_read_from_db", true}}));

        REQUIRE(svc.get_config_parameters_calls == 1);
        CHECK(svc.last_get_config_parameters_slot_id == 4);
        CHECK(svc.last_get_config_parameters_force_read_from_db == true);
        // an absent implementation_id means the module level, which the manager spells "!module"
        REQUIRE(svc.last_requested_parameters.size() == 2);
        CHECK(svc.last_requested_parameters.at(0).module_implementation_id == "!module");
        CHECK(svc.last_requested_parameters.at(0).configuration_parameter_name == "log_interval");
        CHECK(svc.last_requested_parameters.at(1).module_implementation_id == "example");

        const auto reply = last_reply(mock);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(reply.at("status") == "Success");
        REQUIRE(reply.at("parameter_values").size() == 2);
        const auto& ok = reply.at("parameter_values").at(0);
        CHECK(ok.at("status") == "OK");
        CHECK(ok.at("parameter").at("name") == "log_interval");
        CHECK(ok.at("parameter").at("value") == "3");
        CHECK(ok.at("parameter").at("characteristics").at("datatype") == "Integer");
        const auto& absent = reply.at("parameter_values").at(1);
        CHECK(absent.at("status") == "DoesNotExist");
        CHECK_FALSE(absent.contains("parameter"));
    }

    SECTION("get_config_parameters with a non-Success status -> no parameter_values at all") {
        FakeConfigService::GetConfigParametersResult result;
        result.status = Everest::config::GetConfigurationStatus::SlotDoesNotExist;
        svc.get_config_parameters_result = result;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        const json wanted = {{"module_id", "example"}, {"parameter_name", "log_interval"}};
        invoke_command(mock, "get_config_parameters",
                       make_request(json{{"slot_id", 99}, {"parameters", json::array({wanted})}}));

        const auto reply = last_reply(mock);
        CHECK(reply.at("status") == "SlotDoesNotExist");
        CHECK_FALSE(reply.contains("parameter_values"));
        // force_read_from_db is optional in the request and must default to false
        CHECK(svc.last_get_config_parameters_force_read_from_db == false);
    }

    SECTION("get_configuration -> the full module configuration with spec field names") {
        FakeConfigService::GetConfigurationResult result;
        result.status = Everest::config::GetConfigurationStatus::Success;
        result.module_configurations["module_id"] = make_populated_module_config();
        svc.get_configuration_result = result;

        ConfigurationAPI api(mock, svc, /*readonly=*/false);
        mock.clear_published();

        invoke_command(mock, "get_configuration", make_request(json{{"slot_id", 0}}));

        REQUIRE(svc.get_configuration_calls == 1);
        CHECK(svc.last_get_configuration_slot_id == 0);
        // force_read_from_db is optional in the request and must default to false
        CHECK(svc.last_get_configuration_force_read_from_db == false);

        const auto reply = last_reply(mock);
        CHECK(mock.published().back().first == REPLY_TO);
        CHECK(reply.at("status") == "Success");
        REQUIRE(reply.at("module_configurations").size() == 1);
        const auto& module = reply.at("module_configurations").at(0);

        CHECK(module.at("module_id") == "module_id");
        CHECK(module.at("module_name") == "Module Name");
        CHECK(module.at("standalone") == true);
        CHECK(module.at("telemetry_enabled") == true);
        CHECK(module.at("telemetry_config").at("id") == 5);

        // connections: a map of requirement id -> fulfillments becomes a list of {requirement_id,
        // fulfillments}; the internal map is ordered, so conn1 precedes conn2
        REQUIRE(module.at("connections").size() == 2);
        CHECK(module.at("connections").at(0).at("requirement_id") == "conn1");
        REQUIRE(module.at("connections").at(0).at("fulfillments").size() == 1);
        CHECK(module.at("connections").at(0).at("fulfillments").at(0).at("module_id") == "module_a");
        CHECK(module.at("connections").at(0).at("fulfillments").at(0).at("implementation_id") == "impl_a1");
        CHECK(module.at("connections").at(0).at("fulfillments").at(0).at("index") == 0);
        CHECK(module.at("connections").at(1).at("requirement_id") == "conn2");
        REQUIRE(module.at("connections").at(1).at("fulfillments").size() == 2);
        CHECK(module.at("connections").at(1).at("fulfillments").at(1).at("index") == 1);

        // tier mappings: the module-level one carries a connector, the implementation one does not
        CHECK(module.at("mapping").at("module").at("evse") == 1);
        CHECK(module.at("mapping").at("module").at("connector") == 2);
        REQUIRE(module.at("mapping").at("implementations").size() == 1);
        CHECK(module.at("mapping").at("implementations").at(0).at("implementation_id") == "impl_1");
        CHECK(module.at("mapping").at("implementations").at(0).at("mapping").at("evse") == 3);
        CHECK_FALSE(module.at("mapping").at("implementations").at(0).at("mapping").contains("connector"));

        // "!module" parameters are hoisted out of the map into module_configuration_parameters,
        // every other implementation id becomes an implementation_configuration_parameters entry
        REQUIRE(module.at("module_configuration_parameters").size() == 3);
        const auto& integer_param = module.at("module_configuration_parameters").at(0);
        CHECK(integer_param.at("name") == "integer_param");
        CHECK(integer_param.at("value") == "10");
        CHECK(integer_param.at("characteristics").at("datatype") == "Integer");
        CHECK(integer_param.at("characteristics").at("mutability") == "ReadWrite");
        CHECK(integer_param.at("characteristics").at("unit") == "ms");
        const auto& boolean_param = module.at("module_configuration_parameters").at(2);
        CHECK(boolean_param.at("value") == "true");
        CHECK(boolean_param.at("characteristics").at("datatype") == "Boolean");
        CHECK(boolean_param.at("characteristics").at("mutability") == "WriteOnly");

        REQUIRE(module.at("implementation_configuration_parameters").size() == 1);
        const auto& impl_params = module.at("implementation_configuration_parameters").at(0);
        CHECK(impl_params.at("implementation_id") == "impl_1");
        REQUIRE(impl_params.at("configuration_parameters").size() == 1);
        CHECK(impl_params.at("configuration_parameters").at(0).at("name") == "string_param");
        CHECK(impl_params.at("configuration_parameters").at(0).at("value") == "example_value");

        const auto& config_access = module.at("config_access").at("config");
        CHECK(config_access.at("allow_global_read") == false);
        CHECK(config_access.at("allow_global_write") == true);
        CHECK(config_access.at("allow_set_read_only") == false);
        REQUIRE(config_access.at("module_config_access").size() == 1);
        const auto& module_access = config_access.at("module_config_access").at(0);
        CHECK(module_access.at("module_id") == "other_module_id");
        CHECK(module_access.at("allow_read") == true);
        CHECK(module_access.at("allow_write") == false);
        CHECK(module_access.at("allow_set_read_only") == true);
    }
}

TEST_CASE("ConfigurationAPI read-only mode denies every mutating command", "[configuration_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;
    ConfigurationAPI api(mock, svc, /*readonly=*/true);
    mock.clear_published();

    SECTION("mark_active_slot -> AccessDenied, service untouched") {
        invoke_command(mock, "mark_active_slot", make_request(json{{"slot_id", 1}}));
        CHECK(svc.mark_active_slot_calls == 0);
        CHECK(last_reply(mock).at("result") == "AccessDenied");
    }

    SECTION("delete_slot -> AccessDenied, service untouched") {
        invoke_command(mock, "delete_slot", make_request(json{{"slot_id", 1}}));
        CHECK(svc.delete_slot_calls == 0);
        CHECK(last_reply(mock).at("result") == "AccessDenied");
    }

    SECTION("duplicate_slot -> success=false, service untouched") {
        invoke_command(mock, "duplicate_slot", make_request(json{{"slot_id", 1}}));
        CHECK(svc.duplicate_slot_calls == 0);
        const auto reply = last_reply(mock);
        CHECK(reply.at("success") == false);
        CHECK_FALSE(reply.contains("slot_id"));
    }

    SECTION("set_description -> success=false, service untouched") {
        invoke_command(mock, "set_description", make_request(json{{"slot_id", 1}, {"description", "x"}}));
        CHECK(svc.set_description_calls == 0);
        CHECK(last_reply(mock).at("success") == false);
    }

    SECTION("load_from_yaml -> success=false with 'Not Allowed', service untouched") {
        invoke_command(mock, "load_from_yaml", make_request(json{{"raw_yaml", "a: b"}}));
        CHECK(svc.load_from_yaml_calls == 0);
        const auto reply = last_reply(mock);
        CHECK(reply.at("success") == false);
        CHECK(reply.at("error_message") == "Not Allowed");
    }

    SECTION("set_config_parameters -> one Rejected per requested update, service untouched") {
        const json update = {{"cfg_param_id", {{"module_id", "m"}, {"parameter_name", "p"}}}, {"value", "1"}};
        invoke_command(mock, "set_config_parameters",
                       make_request(json{{"slot_id", 0}, {"parameter_updates", json::array({update, update})}}));
        CHECK(svc.set_config_parameters_calls == 0);
        const auto reply = last_reply(mock);
        REQUIRE(reply.at("results").size() == 2);
        CHECK(reply.at("results").at(0) == "Rejected");
        CHECK(reply.at("results").at(1) == "Rejected");
    }
}

TEST_CASE("ConfigurationAPI failure and no-replyTo paths", "[configuration_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;
    ConfigurationAPI api(mock, svc, /*readonly=*/false);
    mock.clear_published();

    // A malformed inner payload is covered end-to-end by
    // tests/management_api_tests/configuration_api_tests.py::test_malformed_payload_gets_error_reply.
    // The sections below have no counterpart there: the real config service cannot be made to throw,
    // and the Python client always sets headers.replyTo.

    SECTION("a throwing config service -> Failed") {
        svc.throw_on_mark_active_slot = true;
        invoke_command(mock, "mark_active_slot", make_request(json{{"slot_id", 1}}));
        REQUIRE(svc.mark_active_slot_calls == 1);
        CHECK(last_reply(mock).at("result") == "Failed");
    }

    SECTION("a throwing config service on a reply without a status field -> empty slot list") {
        svc.throw_on_list_all_slots = true;
        invoke_command(mock, "list_all_slots", make_request(json::object()));
        REQUIRE(svc.list_all_slots_calls == 1);
        CHECK(last_reply(mock).at("slots").empty());
    }

    SECTION("no headers.replyTo -> command still runs, reply is discarded to the empty topic") {
        REQUIRE_NOTHROW(invoke_command(mock, "mark_active_slot", make_request_without_reply_to(json{{"slot_id", 1}})));

        CHECK(svc.mark_active_slot_calls == 1);
        for (const auto& [topic, payload] : mock.published()) {
            CHECK(topic.empty());
        }
    }
}
