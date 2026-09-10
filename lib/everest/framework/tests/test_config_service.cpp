// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_all.hpp>

#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/schema_updater.hpp>
#include <framework/runtime.hpp>
#include <tests/helpers.hpp>
#include <tests/mock_mqtt_abstraction.hpp>
#include <utils/config.hpp>
#include <utils/config/config_service_core.hpp>
#include <utils/config/storage_sqlite.hpp>
#include <utils/config_service_interface.hpp>
#include <utils/mqtt_config_service.hpp>

using namespace Everest;
using namespace Everest::config;
using namespace Everest::tests;

// ─── Minimal ConfigServiceInterface stub for handler tests ───────────────────

struct StubConfigService : Everest::config::ConfigServiceInterface {
    SetConfigParameterResult last_set_results;
    everest::config::ModuleConfigurations module_configurations;

    using SetParamCallback =
        std::function<void(const everest::config::ConfigurationParameterIdentifier&, const std::string&)>;
    SetParamCallback set_parameter_callback_;

    StubConfigService() {
        last_set_results.parameter_results.emplace(
            1, SetConfigPerParameterResult{SetConfigParameterResultEnum::Applied, ""});
    }

    std::vector<SlotInfo> list_all_slots() override {
        return {};
    }
    int get_active_slot_id() override {
        return 0;
    }
    int get_next_boot_slot_id() override {
        return 0;
    }
    SetActiveSlotStatus mark_active_slot(int) override {
        return SetActiveSlotStatus::Success;
    }
    DeleteSlotStatus delete_slot(int) override {
        return DeleteSlotStatus::Success;
    }
    DuplicateSlotResult duplicate_slot(int, std::optional<std::string>) override {
        return {};
    }
    LoadFromYamlResult load_from_yaml(const std::string&, std::optional<std::string>,
                                      std::optional<int> slot_id) override {
        return {};
    }
    bool set_description(int, const std::string&) {
        return true;
    }
    GetConfigurationResult get_configuration_v(int, bool) override {
        return {GetConfigurationStatus::Success, module_configurations};
    }

    SetConfigParameterResult set_config_parameters(int, const std::vector<ConfigParameterUpdate>& update,
                                                   const Origin& origin) override {
        last_set_results.status = SetConfigParameterStatus::Ok;
        SetConfigPerParameterResult set_result{SetConfigParameterResultEnum::Rejected, ""};

        if (update.front().identifier.configuration_parameter_name == "ro_param") {
            set_result.status = SetConfigParameterResultEnum::WillApplyOnRestart;
        } else if (update.front().identifier.configuration_parameter_name == "no_such_param") {
            set_result.status = SetConfigParameterResultEnum::DoesNotExist;
        } else {
            if (set_parameter_callback_) {
                set_parameter_callback_(update.at(0).identifier, update.at(0).value);
            }
            if (update.front().value == "new_value") {
                set_result.status = SetConfigParameterResultEnum::Applied;
            } else if (update.front().value == "bad_value") {
                set_result.status = SetConfigParameterResultEnum::Rejected;
            }
        }
        last_set_results.parameter_results = {set_result};
        return last_set_results;
    }
    GetConfigParametersResult
    get_config_parameters_v(int slot_id,
                            const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                            bool force_read_from_db) override {
        return GetConfigParametersResult{};
    }
    void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)>) override {
    }
    void register_config_update_handler(std::function<void(const ConfigurationUpdate&)>) override {
    }

    void set_modules_stopped() override {
    }
    void set_modules_running() override {
    }
    void set_modules_starting() override {
    }
    void set_modules_stopping() override {
    }
    void notice_cfg_validation_failed() override {
    }
    void notice_module_restart_triggered() override {
    }
    bool throw_on_get_active_module_configurations{false};

    std::shared_ptr<const everest::config::ModuleConfigurations> get_active_module_configurations() const override {
        if (throw_on_get_active_module_configurations) {
            throw std::runtime_error("stub failure");
        }
        return std::make_shared<const everest::config::ModuleConfigurations>(module_configurations);
    }
};

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static nlohmann::json make_ok_get_response(const std::string& get_type, const nlohmann::json& data) {
    return {{"status", "Ok"}, {"status_info", ""}, {"type", "Get"}, {"response", {{"type", get_type}, {"data", data}}}};
}

static nlohmann::json make_ok_set_response(const std::string& set_status) {
    return {{"status", "Ok"}, {"status_info", ""}, {"type", "Set"}, {"response", {{"status", set_status}}}};
}

static nlohmann::json make_error_response(const std::string& info = "") {
    return {{"status", "Error"}, {"status_info", info}};
}

// ─── ConfigServiceClient ──────────────────────────────────────────────────────

TEST_CASE("ConfigServiceClient::get_module_configs", "[config_service]") {
    const std::string prefix = "everest/";
    const std::string module_id = "my_module";
    const std::map<std::string, std::string, std::less<>> module_names = {{"module_a", "TypeA"}};

    auto mock = std::make_shared<MockMQTTAbstraction>(prefix);
    ConfigServiceClient client(mock, module_id, module_names);

    SECTION("sends request to correct topics") {
        mock->set_get_response(make_ok_get_response("All", {{"module_a", nlohmann::json::object()}}));

        client.get_module_configs();

        REQUIRE(mock->last_get_request().has_value());
        CHECK(mock->last_get_request()->request_topic == prefix + "config/request");
        CHECK(mock->last_get_request()->response_topic == prefix + "modules/" + module_id + "/response");
    }

    SECTION("returns parsed ModuleIdType entries on success") {
        mock->set_get_response(make_ok_get_response("All", {{"module_a", nlohmann::json::object()}}));

        const auto result = client.get_module_configs();

        REQUIRE(result.size() == 1);
        CHECK(result.begin()->first.module_id == "module_a");
        CHECK(result.begin()->first.module_type == "TypeA");
    }

    SECTION("returns empty map on error response") {
        mock->set_get_response(make_error_response("service unavailable"));

        CHECK(client.get_module_configs().empty());
    }

    SECTION("returns empty map when response contains unknown module id") {
        // "unknown" is not in module_names — at() throws, catch block returns {}
        mock->set_get_response(make_ok_get_response("All", {{"unknown", nlohmann::json::object()}}));

        CHECK(client.get_module_configs().empty());
    }
}

TEST_CASE("ConfigServiceClient::get_config_value", "[config_service]") {
    const std::string prefix = "everest/";
    const std::string module_id = "my_module";

    auto mock = std::make_shared<MockMQTTAbstraction>(prefix);
    ConfigServiceClient client(mock, module_id, {});

    everest::config::ConfigurationParameterIdentifier id;
    id.module_id = "module_a";
    id.configuration_parameter_name = "my_param";

    SECTION("sends request to correct topics") {
        mock->set_get_response(make_ok_get_response("Value", nlohmann::json::object()));

        client.get_config_value(id);

        REQUIRE(mock->last_get_request().has_value());
        CHECK(mock->last_get_request()->request_topic == prefix + "config/request");
        CHECK(mock->last_get_request()->response_topic == prefix + "modules/" + module_id + "/response");
    }

    SECTION("returns Ok status on success") {
        mock->set_get_response(make_ok_get_response("Value", nlohmann::json::object()));

        const auto result = client.get_config_value(id);

        CHECK(result.status == ResponseStatus::Ok);
    }

    SECTION("returns Error status on error response") {
        mock->set_get_response(make_error_response("not found"));

        const auto result = client.get_config_value(id);

        CHECK(result.status == ResponseStatus::Error);
    }
}

TEST_CASE("ConfigServiceClient::set_config_value", "[config_service]") {
    const std::string prefix = "everest/";
    const std::string module_id = "my_module";

    auto mock = std::make_shared<MockMQTTAbstraction>(prefix);
    ConfigServiceClient client(mock, module_id, {});

    everest::config::ConfigurationParameterIdentifier id;
    id.module_id = "module_a";
    id.configuration_parameter_name = "my_param";

    SECTION("sends request to correct topics") {
        mock->set_get_response(make_ok_set_response("Accepted"));

        client.set_config_value(id, "42");

        REQUIRE(mock->last_get_request().has_value());
        CHECK(mock->last_get_request()->request_topic == prefix + "config/request");
        CHECK(mock->last_get_request()->response_topic == prefix + "modules/" + module_id + "/response");
    }

    SECTION("returns Accepted when module accepts") {
        mock->set_get_response(make_ok_set_response("Accepted"));

        const auto result = client.set_config_value(id, "42");

        CHECK(result.status == ResponseStatus::Ok);
        CHECK(result.set_status == everest::config::SetConfigStatus::Accepted);
    }

    SECTION("returns Rejected when module rejects") {
        mock->set_get_response(make_ok_set_response("Rejected"));

        const auto result = client.set_config_value(id, "bad");

        CHECK(result.status == ResponseStatus::Ok);
        CHECK(result.set_status == everest::config::SetConfigStatus::Rejected);
    }

    SECTION("returns RebootRequired when module signals reboot") {
        mock->set_get_response(make_ok_set_response("RebootRequired"));

        const auto result = client.set_config_value(id, "42");

        CHECK(result.status == ResponseStatus::Ok);
        CHECK(result.set_status == everest::config::SetConfigStatus::RebootRequired);
    }

    SECTION("returns Error status on error response") {
        mock->set_get_response(make_error_response("access denied"));

        const auto result = client.set_config_value(id, "42");

        CHECK(result.status == ResponseStatus::Error);
    }
}

TEST_CASE("ConfigServiceClient::get_mappings", "[config_service]") {
    const std::string prefix = "everest/";
    const std::string module_id = "my_module";

    auto mock = std::make_shared<MockMQTTAbstraction>(prefix);
    ConfigServiceClient client(mock, module_id, {});

    SECTION("sends request to correct topics") {
        mock->set_get_response(make_ok_get_response("AllMappings", nlohmann::json::object()));

        client.get_mappings();

        REQUIRE(mock->last_get_request().has_value());
        CHECK(mock->last_get_request()->request_topic == prefix + "config/request");
        CHECK(mock->last_get_request()->response_topic == prefix + "modules/" + module_id + "/response");
    }

    SECTION("returns empty map when data is empty") {
        mock->set_get_response(make_ok_get_response("AllMappings", nlohmann::json::object()));

        CHECK(client.get_mappings().empty());
    }

    SECTION("returns empty map on error response") {
        mock->set_get_response(make_error_response());

        CHECK(client.get_mappings().empty());
    }
}

// ─── MqttConfigServiceHandler ─────────────────────────────────────────────────
//
// Fixture: "config_service_test" directory (created by setup_test_directory in CMakeLists.txt)
//
// Modules:
//   target_module    (TESTCSTarget)    — rw_param (ReadWrite), ro_param (ReadOnly)
//   manager_module   (TESTValidManifest) — has read+write+allow_set_read_only access to target_module
//   restricted_module (TESTValidManifest) — no access to target_module

namespace {

// Parse the Response out of the MqttMessagePayload JSON stored by MockMQTTAbstraction::publish().
// The service publishes: MqttMessagePayload{ConfigurationResponse, response}
// which serializes to: {"msg_type": "ConfigurationResponse", "data": <Response JSON>}
Response parse_published_response(const std::pair<std::string, nlohmann::json>& published_entry) {
    return published_entry.second.at("data").get<Response>();
}

} // namespace

TEST_CASE("MqttConfigServiceHandler", "[config_service]") {
    const std::string prefix = "everest/";
    MockMQTTAbstraction mock(prefix);

    const auto bin_dir = get_bin_dir().string() + "/";
    const auto ms = ManagerSettings(bin_dir + "config_service_test/", bin_dir + "config_service_test/config.yaml");
    auto config = std::make_shared<ManagerConfig>(ms);
    StubConfigService stub_svc;
    stub_svc.module_configurations = config->get_module_configurations();

    MqttConfigServiceHandler service(mock, stub_svc);

    stub_svc.set_parameter_callback_ = [&service](const everest::config::ConfigurationParameterIdentifier& cfg_param_id,
                                                  const std::string& value) {
        service.cmd_set_cfg_param(cfg_param_id, value);
    };

    const std::string config_topic = prefix + "config/request";

    // Convenience: invoke the registered handler with a request JSON
    auto invoke = [&](const nlohmann::json& req) {
        REQUIRE(mock.registered_handlers().count(config_topic) == 1);
        (*mock.registered_handlers().at(config_topic)->handler)(config_topic, req);
    };

    SECTION("constructor registers handler on config/request topic") {
        CHECK(mock.registered_handlers().count(config_topic) == 1);
    }

    SECTION("handler replies with an error to Get requests from an unknown origin") {
        // Regression: these used to throw inside the handler and produce NO reply at all,
        // leaving the requesting client to block until its timeout.
        const auto get_type =
            GENERATE(std::string("Module"), std::string("Value"), std::string("All"), std::string("AllMappings"));
        nlohmann::json request = {{"type", "Get"}, {"origin", "ghost_module"}, {"request", {{"type", get_type}}}};
        if (get_type == "Value") {
            request["request"]["identifier"] = {{"module_id", "target_module"},
                                                {"configuration_parameter_name", "some_param"}};
        }

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const auto& [resp_topic, payload] = mock.published().front();
        CHECK(resp_topic == prefix + "modules/ghost_module/response");

        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Error);
        CHECK(resp.status_info == "Unknown origin module: ghost_module");
        // Error responses carry no type/payload (see Response::type documentation).
        CHECK_FALSE(resp.type.has_value());
    }

    SECTION("handler replies with an error when request handling throws") {
        // Regression guard: once the request is parsed the requester is known, so even an
        // unexpected exception must produce an error reply instead of silence.
        stub_svc.throw_on_get_active_module_configurations = true;
        const nlohmann::json request = {
            {"type", "Get"}, {"origin", "target_module"}, {"request", {{"type", "Module"}}}};

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const auto& [resp_topic, payload] = mock.published().front();
        CHECK(resp_topic == prefix + "modules/target_module/response");

        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Error);
        CHECK(resp.status_info == "Exception during handling of request: stub failure");
        CHECK_FALSE(resp.type.has_value());
    }

    SECTION("handler responds to Get::Module request") {
        const nlohmann::json request = {
            {"type", "Get"}, {"origin", "target_module"}, {"request", {{"type", "Module"}}}};

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const auto& [resp_topic, payload] = mock.published().front();
        CHECK(resp_topic == prefix + "modules/target_module/response");

        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Ok);
        REQUIRE(resp.type.has_value());
        CHECK(resp.type.value() == Type::Get);
        const auto& get_resp = std::get<GetResponse>(resp.response);
        CHECK(get_resp.type == GetType::Module);
        CHECK(get_resp.data.contains("module_config"));
    }

    SECTION("handler responds to Get::All — includes own and accessible modules") {
        const nlohmann::json request = {{"type", "Get"}, {"origin", "manager_module"}, {"request", {{"type", "All"}}}};

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& get_resp = std::get<GetResponse>(resp.response);
        CHECK(get_resp.type == GetType::All);
        // manager_module can always read its own config, and has allow_read for target_module
        CHECK(get_resp.data.contains("manager_module"));
        CHECK(get_resp.data.contains("target_module"));
        // restricted_module is not in manager_module's access list
        CHECK_FALSE(get_resp.data.contains("restricted_module"));
    }

    SECTION("handler responds to Get::All — restricted module only sees itself") {
        const nlohmann::json request = {
            {"type", "Get"}, {"origin", "restricted_module"}, {"request", {{"type", "All"}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& get_resp = std::get<GetResponse>(resp.response);
        CHECK(get_resp.data.contains("restricted_module"));
        CHECK_FALSE(get_resp.data.contains("target_module"));
        CHECK_FALSE(get_resp.data.contains("manager_module"));
    }

    SECTION("handler responds to Get::AllMappings") {
        const nlohmann::json request = {
            {"type", "Get"}, {"origin", "manager_module"}, {"request", {{"type", "AllMappings"}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& get_resp = std::get<GetResponse>(resp.response);
        CHECK(get_resp.type == GetType::AllMappings);
        // No explicit mappings defined in config, so accessible modules appear with empty mappings
        CHECK(get_resp.data.contains("manager_module"));
        CHECK(get_resp.data.contains("target_module"));
    }

    SECTION("handler responds to Get::Value") {
        const nlohmann::json request = {
            {"type", "Get"},
            {"origin", "manager_module"},
            {"request",
             {{"type", "Value"},
              {"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& get_resp = std::get<GetResponse>(resp.response);
        CHECK(get_resp.type == GetType::Value);
    }

    SECTION("handler returns AccessDenied when origin has no read access to target") {
        const nlohmann::json request = {
            {"type", "Get"},
            {"origin", "restricted_module"},
            {"request",
             {{"type", "Value"},
              {"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::AccessDenied);
    }

    SECTION("handler forwards Set request for ReadWrite param to target module") {
        // Pre-configure the mock so that get() (used to forward to target module) returns Accepted
        mock.set_get_response(make_ok_set_response("Accepted"));

        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "manager_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}},
              {"value", "new_value"}}}};

        invoke(request);

        // Verify the request was forwarded to the target module
        REQUIRE(mock.last_get_request().has_value());
        const auto& fwd = mock.last_get_request().value();
        CHECK(fwd.request_topic == prefix + "modules/target_module/config/set_request");
        CHECK(fwd.response_topic == prefix + "modules/target_module/config/set_response");

        // Verify the response published back to the origin
        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& set_resp = std::get<SetResponse>(resp.response);
        CHECK(set_resp.status == SetResponseStatus::Accepted);
    }

    SECTION("handler does not persist ReadWrite value when module rejects") {
        mock.set_get_response(make_ok_set_response("Rejected"));

        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "manager_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}},
              {"value", "bad_value"}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& set_resp = std::get<SetResponse>(resp.response);
        CHECK(set_resp.status == SetResponseStatus::Rejected);
    }

    SECTION("handler returns AccessDenied when origin has no write access to target") {
        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "restricted_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}},
              {"value", "new_value"}}}};

        invoke(request);

        // No forwarding should have happened
        CHECK_FALSE(mock.last_get_request().has_value());

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::AccessDenied);
    }

    SECTION("handler persists ReadOnly param without forwarding when allow_set_read_only is granted") {
        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "manager_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "ro_param"}}},
              {"value", "new_fixed"}}}};

        invoke(request);

        // ReadOnly set is handled locally; no forwarding via get() to target module
        CHECK_FALSE(mock.last_get_request().has_value());

        // Response status is Ok regardless of storage result
        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Ok);
    }

    SECTION("handler returns Error for unknown origin module") {
        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "no_such_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}},
              {"value", "x"}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Error);
    }

    SECTION("handler returns Error for unknown config parameter") {
        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "manager_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "no_such_param"}}},
              {"value", "x"}}}};

        invoke(request);

        const Response resp = parse_published_response(mock.published().back());
        CHECK(resp.status == ResponseStatus::Error);
    }
}

// ─── Integration: real handler wired to the real core ────────────────────────
//
// The suites above test MqttConfigServiceHandler against StubConfigService and (elsewhere)
// ConfigServiceCore against hand-written lambdas. This TEST_CASE wires the two real halves
// together - only the MQTT transport is mocked - so the wire-visible behavior is asserted
// against the core's real semantics instead of a stub's beliefs about them.
TEST_CASE("MqttConfigServiceHandler wired to a real ConfigServiceCore", "[config_service][integration]") {
    const std::string prefix = "everest/";
    MockMQTTAbstraction mock(prefix);

    const auto bin_dir = get_bin_dir().string() + "/";
    ManagerSettings ms(bin_dir + "config_service_test/", bin_dir + "config_service_test/config.yaml");
    ms.validate_schema = false;

    auto db = std::make_shared<everest::db::sqlite::Connection>("file::memory:?cache=shared");
    REQUIRE(db->open_connection());
    everest::db::sqlite::SchemaUpdater updater{db.get()};
    REQUIRE(updater.apply_migration_files(bin_dir + "migrations", TARGET_MIGRATION_FILE_VERSION));

    ConfigServiceCore core(ms, db);

    // Same modules and access rules as the config_service_test fixture, loaded through the real
    // yaml/manifest validation path into the real storage.
    const std::string config_yaml = R"(
active_modules:
  target_module:
    module: TESTCSTarget
    config_module:
      rw_param: "initial"
      ro_param: "fixed"
  manager_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello"
    config_implementation:
      main:
        valid_config_entry: "hello"
    access:
      config:
        modules:
          target_module:
            allow_read: true
            allow_write: true
            allow_set_read_only: true
  restricted_module:
    module: TESTValidManifest
    config_module:
      valid_config_entry: "hello"
    config_implementation:
      main:
        valid_config_entry: "hello"
)";
    auto load_result = core.load_from_yaml(config_yaml, "integration", 0);
    INFO(load_result.error_message);
    REQUIRE(load_result.success);
    core.mark_active_slot(0);
    core.reinitialize_from_db(true);

    MqttConfigServiceHandler service(mock, core);

    const std::string config_topic = prefix + "config/request";
    auto invoke = [&](const nlohmann::json& req) {
        REQUIRE(mock.registered_handlers().count(config_topic) == 1);
        (*mock.registered_handlers().at(config_topic)->handler)(config_topic, req);
    };

    // Registers the runtime set-parameter forwarder the way the manager wires it: forward to
    // target module via the handler and map the module's reply.
    const auto register_manager_style_forwarder = [&core, &service]() {
        core.register_set_runtime_parameter_handler(
            [&service](const everest::config::ConfigurationParameterIdentifier& cfg_param_id,
                       const std::string& value) {
                const auto result = service.cmd_set_cfg_param(cfg_param_id, value);
                if (not result.has_value()) {
                    return SetParameterResponse::SetCallFailed;
                }
                switch (result->status) {
                case SetResponseStatus::Accepted:
                    return SetParameterResponse::ModuleReplied_Applied;
                case SetResponseStatus::RebootRequired:
                    return SetParameterResponse::ModuleReplied_RequiresRestart;
                default:
                    return SetParameterResponse::ModuleReplied_Rejected;
                }
            });
    };

    const nlohmann::json set_rw_param_request = {
        {"type", "Set"},
        {"origin", "manager_module"},
        {"request",
         {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}},
          {"value", "new_value"}}}};

    SECTION("Set of a ReadWrite param is forwarded to the running module; its Accepted reply applies at runtime") {
        register_manager_style_forwarder();
        core.set_modules_running();
        mock.set_get_response(nlohmann::json{{"status", "Ok"},
                                             {"status_info", ""},
                                             {"type", "Set"},
                                             {"response", {{"status", "Accepted"}, {"status_info", ""}}}});

        invoke(set_rw_param_request);

        // The forward really went out to the target module's set_request topic.
        REQUIRE(mock.last_get_request().has_value());
        CHECK(mock.last_get_request()->request_topic == prefix + "modules/target_module/config/set_request");
        CHECK(mock.last_get_request()->response_topic == prefix + "modules/target_module/config/set_response");

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Ok);
        REQUIRE(resp.type.has_value());
        CHECK(resp.type.value() == Type::Set);
        const auto& set_resp = std::get<SetResponse>(resp.response);
        CHECK(set_resp.status == SetResponseStatus::Accepted);

        // Persisted (persist-first) ...
        everest::config::SqliteStorage storage(db, 0);
        auto persisted = storage.get_configuration_parameter({"target_module", "rw_param", "!module"});
        REQUIRE(persisted.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<std::string>(persisted.configuration_parameter.value().value) == "new_value");

        // ... and, because the module confirmed the runtime change, applied in memory too.
        auto active_configs = core.get_active_module_configurations();
        const auto& params = active_configs->at("target_module").configuration_parameters.at("!module");
        const auto param_it =
            std::find_if(params.begin(), params.end(), [](const auto& p) { return p.name == "rw_param"; });
        REQUIRE(param_it != params.end());
        CHECK(std::get<std::string>(param_it->value) == "new_value");
    }

    SECTION("Set of a ReadWrite param persists and reports RebootRequired when the module does not respond") {
        register_manager_style_forwarder();
        core.set_modules_running();
        // The forward reaches MQTT but the module replies with an error (e.g. timeout): the change
        // is persisted (WillApplyOnRestart), and reported as RebootRequired.
        mock.set_get_response(nlohmann::json{{"status", "Error"}, {"status_info", "timeout"}});

        invoke(set_rw_param_request);

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Ok);
        REQUIRE(resp.type.has_value());
        CHECK(resp.type.value() == Type::Set);
        const auto& set_resp = std::get<SetResponse>(resp.response);
        CHECK(set_resp.status == SetResponseStatus::RebootRequired);

        // The change is really persisted in the core's storage ...
        everest::config::SqliteStorage storage(db, 0);
        auto persisted = storage.get_configuration_parameter({"target_module", "rw_param", "!module"});
        REQUIRE(persisted.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<std::string>(persisted.configuration_parameter.value().value) == "new_value");

        // ... while the in-memory (runtime) value stays untouched until the next restart.
        auto active_configs = core.get_active_module_configurations();
        const auto& params = active_configs->at("target_module").configuration_parameters.at("!module");
        const auto param_it =
            std::find_if(params.begin(), params.end(), [](const auto& p) { return p.name == "rw_param"; });
        REQUIRE(param_it != params.end());
        CHECK(std::get<std::string>(param_it->value) == "initial");
    }

    SECTION("registering nullptr clears the forwarder slot") {
        // It's possible to unregister a runtime_parameter_handler by setting it to nullptr.
        register_manager_style_forwarder();
        core.set_modules_running();

        core.register_set_runtime_parameter_handler(nullptr);

        invoke(set_rw_param_request);

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& set_resp = std::get<SetResponse>(resp.response);
        CHECK(set_resp.status == SetResponseStatus::RebootRequired);
        CHECK(set_resp.status_info.find("No runtime-change callback") != std::string::npos);
    }

    SECTION("Get::Module returns the real core's configuration") {
        const nlohmann::json request = {
            {"type", "Get"}, {"origin", "target_module"}, {"request", {{"type", "Module"}}}};

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Ok);
        const auto& get_resp = std::get<GetResponse>(resp.response);
        CHECK(get_resp.type == GetType::Module);
        REQUIRE(get_resp.data.contains("module_config"));
        const auto dump = get_resp.data.dump();
        CHECK(dump.find("rw_param") != std::string::npos);
        CHECK(dump.find("initial") != std::string::npos);
    }

    SECTION("Access rules from the real configuration are enforced") {
        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "restricted_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "rw_param"}}},
              {"value", "new_value"}}}};

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::AccessDenied);

        // Nothing was persisted
        everest::config::SqliteStorage storage(db, 0);
        auto persisted = storage.get_configuration_parameter({"target_module", "rw_param", "!module"});
        REQUIRE(persisted.status == everest::config::GetSetResponseStatus::OK);
        CHECK(std::get<std::string>(persisted.configuration_parameter.value().value) == "initial");
    }

    SECTION("Set of an unknown parameter surfaces the real core's DoesNotExist as an error") {
        const nlohmann::json request = {
            {"type", "Set"},
            {"origin", "manager_module"},
            {"request",
             {{"identifier", {{"module_id", "target_module"}, {"configuration_parameter_name", "no_such_param"}}},
              {"value", "new_value"}}}};

        invoke(request);

        REQUIRE(mock.published().size() == 1);
        const Response resp = parse_published_response(mock.published().front());
        CHECK(resp.status == ResponseStatus::Error);
        CHECK(resp.status_info.find("not found") != std::string::npos);
    }
}
