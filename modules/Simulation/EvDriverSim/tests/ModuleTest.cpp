// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvDriverSim.hpp"
#include "board_support/ev_board_supportImpl.hpp"
#include <ModuleAdapterStub.hpp>
#include <boost/log/core.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <catch2/catch_test_macros.hpp>
#include <everest/util/async/monitor.hpp>
#include <everest_api_types/ev_driver_sim/codec.hpp>
#include <future>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace module {
static EvDriverSim* test_module;
void LdEverest::init(ModuleConfigs, const ModuleInfo&) {
    test_module->init();
}
void LdEverest::ready() {
    test_module->ready();
}
} // namespace module

namespace {
namespace api = everest::lib::API::V1_0::types::ev_driver_sim;
using types::board_support_common::Event;

struct Observations {
    std::vector<std::pair<std::string, Value>> car;
    std::vector<std::pair<std::string, Parameters>> board;
    std::vector<std::pair<std::string, std::string>> mqtt;
    std::vector<Everest::error::Error> raised;
    std::vector<Everest::error::Error> cleared;
    std::vector<std::string> failed_operations;
};

struct Adapter : module::stub::QuietModuleAdapterStub {
    everest::lib::util::monitor<Observations> observed;
    std::map<std::string, ValueCallback> board_callbacks;
    std::map<std::string, StringHandler> mqtt_callbacks;
    struct Failure {
        unsigned remaining;
        bool nonstandard;
    };
    everest::lib::util::monitor<std::map<std::string, Failure>> failures;
    void fail_next(const std::string& operation, bool nonstandard = false) {
        failures.handle()->emplace(operation, Failure{1, nonstandard});
    }
    void maybe_throw(const std::string& operation) {
        auto pending = failures.handle();
        const auto it = pending->find(operation);
        if (it != pending->end() && it->second.remaining > 0) {
            --it->second.remaining;
            observed.handle()->failed_operations.push_back(operation);
            observed.notify_all();
            if (it->second.nonstandard) {
                throw 42;
            }
            throw std::runtime_error("injected " + operation + " failure");
        }
    }
    bool echo_cp{false};
    bool pwm_error_f{false};
    std::vector<Value> cp_echoes;
    std::shared_ptr<Everest::error::ErrorTypeMap> error_types = std::make_shared<Everest::error::ErrorTypeMap>();
    std::shared_ptr<Everest::error::ErrorDatabaseMap> errors = std::make_shared<Everest::error::ErrorDatabaseMap>();

    Adapter() {
        error_types->load_error_types_map({{"generic/CommunicationFault", "Communication lost"}});
    }
    std::shared_ptr<Everest::error::ErrorManagerImpl> get_error_manager_impl_fn(const std::string&) override {
        return std::make_shared<Everest::error::ErrorManagerImpl>(
            error_types, errors, std::list<Everest::error::ErrorType>{"generic/CommunicationFault"},
            [this](const auto& error) {
                observed.handle()->raised.push_back(error);
                observed.notify_all();
            },
            [this](const auto& error) {
                observed.handle()->cleared.push_back(error);
                observed.notify_all();
            });
    }
    std::shared_ptr<Everest::error::ErrorStateMonitor> get_error_state_monitor_impl_fn(const std::string&) override {
        return std::make_shared<Everest::error::ErrorStateMonitor>(errors);
    }
    std::shared_ptr<Everest::error::ErrorFactory> get_error_factory_fn(const std::string&) override {
        return std::make_shared<Everest::error::ErrorFactory>(
            error_types, ImplementationIdentifier{"test_driver", "board_support"}, Everest::error::Severity::High);
    }

    Result call_fn(const Requirement&, const std::string& name, Parameters params) override {
        maybe_throw("board:" + name);
        observed.handle()->board.emplace_back(name, params);
        if (echo_cp && !pwm_error_f && name == "set_cp_state") {
            cp_echoes.push_back({{"event", params.at("cp_state")}});
        }
        return {};
    }
    void deliver_echoes() {
        // Board publications arrive on a separate framework dispatch, outside the command's state lock.
        for (const auto& event : std::exchange(cp_echoes, {})) {
            board_callbacks.at("bsp_event")(event);
        }
    }
    void publish_fn(const std::string&, const std::string& name, Value value) override {
        maybe_throw("car:" + name);
        observed.handle()->car.emplace_back(name, value);
    }
    void subscribe_fn(const Requirement&, const std::string& name, ValueCallback callback) override {
        board_callbacks[name] = std::move(callback);
    }
    void ext_mqtt_publish_fn(const std::string& topic, const std::string& payload) override {
        maybe_throw("mqtt:" + topic);
        if (topic.find("/e2m/scenario_event") != std::string::npos) {
            maybe_throw("scenario_event:" + nlohmann::json::parse(payload).at("kind").get<std::string>());
        }
        observed.handle()->mqtt.emplace_back(topic, payload);
        observed.notify_all();
    }
    Everest::UnsubscribeToken ext_mqtt_subscribe_fn(const std::string& topic, StringHandler callback) override {
        mqtt_callbacks[topic] = std::move(callback);
        return [this, topic] { mqtt_callbacks.erase(topic); };
    }
};

struct Fixture {
    Adapter adapter;
    Everest::MqttProvider mqtt{adapter};
    ModuleInfo info;
    module::Conf config{"test_car", false, 10, 0, 0};
    module::board_support::Conf board_config;
    Everest::PtrContainer<module::EvDriverSim> ptr;
    std::unique_ptr<module::EvDriverSim> mod;
    const std::string base = "everest_api/1/ev_driver_sim/test_driver/";

    void init() {
        info.id = "test_driver";
        mod = std::make_unique<module::EvDriverSim>(
            info, mqtt, std::make_unique<module::board_support::ev_board_supportImpl>(&adapter, ptr, board_config),
            std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "board", std::nullopt),
            config);
        ptr.set(mod.get());
        adapter.gather_cmds(*mod->p_board_support);
        module::test_module = mod.get();
        module::LdEverest::init({}, info);
    }
    void ready() {
        module::test_module = mod.get();
        module::LdEverest::ready();
        adapter.deliver_echoes();
    }
    void command(const std::string& name, const std::string& payload = "") {
        REQUIRE(adapter.mqtt_callbacks.count(base + "m2e/" + name) == 1);
        adapter.mqtt_callbacks.at(base + "m2e/" + name)(payload);
        adapter.deliver_echoes();
    }
    void car_ack(const std::string& payload = R"({"command":"configure_session","status":"Accepted"})") {
        const std::string topic = "everest_api/1/ev_controller/test_car/e2m/command_ack";
        REQUIRE(adapter.mqtt_callbacks.count(topic) == 1);
        adapter.mqtt_callbacks.at(topic)(payload);
    }
    std::vector<api::ScenarioEvent> scenario_events() {
        std::vector<api::ScenarioEvent> result;
        for (const auto& [topic, payload] : observations().mqtt) {
            if (topic == base + "e2m/scenario_event") {
                result.push_back(api::deserialize<api::ScenarioEvent>(payload));
            }
        }
        return result;
    }
    void car_command(const std::string& name, Parameters params) {
        const auto it = std::find_if(adapter.registered_commands.begin(), adapter.registered_commands.end(),
                                     [&](const auto& cmd) { return cmd.cmd_name == name; });
        REQUIRE(it != adapter.registered_commands.end());
        it->cmd(params);
        adapter.deliver_echoes();
    }
    Observations observations() {
        return *adapter.observed.handle();
    }
    template <class Predicate>
    bool wait_for(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        auto out = adapter.observed.handle();
        return out.wait_for([&] { return predicate(*out); }, timeout);
    }
    template <class T> T last_message(const std::string& name) {
        const auto out = observations();
        const auto it = std::find_if(out.mqtt.rbegin(), out.mqtt.rend(),
                                     [&](const auto& message) { return message.first == base + "e2m/" + name; });
        REQUIRE(it != out.mqtt.rend());
        T value{};
        REQUIRE(api::adl_deserialize(it->second, value));
        return value;
    }
};
} // namespace

TEST_CASE("EvDriverSim startup disconnects once and holds board CP A", "[evdriver]") {
    Fixture f;
    f.init();
    CHECK(f.observations().board.empty());
    CHECK(f.observations().car.empty());
    f.ready();
    const auto out = f.observations();
    REQUIRE(out.board.size() == 1);
    CHECK(out.board.front().first == "set_cp_state");
    CHECK(out.board.front().second.at("cp_state") == "A");
    REQUIRE(out.car.size() == 1);
    CHECK(out.car.front().second.get<types::board_support_common::BspEvent>().event == Event::Disconnected);
}

TEST_CASE("EvDriverSim forwards board variables only when ready and passed", "[evdriver]") {
    for (const auto plugged : {false, true}) {
        Fixture f;
        f.config.plugged_at_startup = plugged;
        f.init();
        const std::map<std::string, Value> values{
            {"bsp_event", types::board_support_common::BspEvent{Event::B}},
            {"bsp_measurement", {{"cp_pwm_duty_cycle", 50.0}, {"proximity_pilot", {{"ampacity", "A_32"}}}}},
            {"ev_info", types::evse_manager::EVInfo{}}};
        for (const auto& [name, value] : values) {
            REQUIRE(f.adapter.board_callbacks.count(name) == 1);
            f.adapter.board_callbacks.at(name)(value);
        }
        CHECK(f.observations().car.empty());
        f.ready();
        CHECK(f.observations().car.front().second.get<types::board_support_common::BspEvent>().event ==
              (plugged ? Event::A : Event::Disconnected));
        for (const auto& [name, value] : values) {
            f.adapter.board_callbacks.at(name)(value);
        }
        CHECK(f.observations().car.size() == (plugged ? 4 : 1));
    }
}

TEST_CASE("EvDriverSim forwards car commands and gates CP commands", "[evdriver]") {
    for (const auto plugged : {false, true}) {
        Fixture f;
        f.config.plugged_at_startup = plugged;
        f.init();
        f.ready();
        const std::map<std::string, Parameters> commands{{"enable", {{"value", true}}},
                                                         {"allow_power_on", {{"value", false}}},
                                                         {"diode_fail", {{"value", true}}},
                                                         {"set_ac_max_current", {{"current", 16.0}}},
                                                         {"set_three_phases", {{"three_phases", true}}},
                                                         {"set_rcd_error", {{"rcd_current_mA", 25.0}}}};
        for (const auto& [name, params] : commands) {
            const auto before = f.observations().board.size();
            f.car_command(name, params);
            const auto out = f.observations();
            REQUIRE(out.board.size() == before + 1);
            CHECK(out.board.back().first == name);
            CHECK(out.board.back().second == params);
        }
        const auto before = f.observations().board.size();
        f.car_command("set_cp_state", {{"cp_state", "C"}});
        CHECK(f.observations().board.size() == before + (plugged ? 1 : 0));
        if (plugged) {
            CHECK(f.observations().board.back().second.at("cp_state") == "C");
        }
    }
}

TEST_CASE("EvDriverSim MQTT world commands acknowledge results and publish changes", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    f.command("plug");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
    CHECK(f.observations().car.back().second.get<types::board_support_common::BspEvent>().event == Event::A);
    f.command("inject_fault", R"({"type":"RcdError","rcd_mA":42,"message":"test"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").command == "inject_fault");
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::RcdError);
    CHECK(f.observations().board.back().first == "set_rcd_error");
    CHECK(f.observations().board.back().second.at("rcd_current_mA") == 42.0);
    const auto before = f.observations().board.size();
    f.command("inject_fault", R"({"type":"DiodeFail"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
    CHECK(f.observations().board.size() == before);
    f.command("clear_fault");
    CHECK_FALSE(f.last_message<api::WorldState>("world_state").fault);
    CHECK(f.observations().board.back().second.at("rcd_current_mA") == 0.0);
    f.command("unplug");
    CHECK(f.last_message<api::CommandAck>("command_ack").command == "unplug");
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    CHECK(f.observations().board.back().second.at("cp_state") == "A");
    CHECK(f.observations().car.back().second.get<types::board_support_common::BspEvent>().event == Event::Disconnected);
    for (const auto& payload : {"", "{", "true", "{}", R"({"type":"unknown"})", R"({"type":"RcdError"})",
                                R"({"type":"RcdError","rcd_mA":"bad"})"}) {
        const auto calls = f.observations().board.size();
        const auto messages = f.observations().mqtt.size();
        f.command("inject_fault", payload);
        CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
        CHECK(f.last_message<api::CommandAck>("command_ack").message.has_value());
        CHECK(f.observations().board.size() == calls);
        CHECK(f.observations().mqtt.size() == messages + 1);
    }
}

TEST_CASE("EvDriverSim startup heartbeat timeout names the car and clears on first heartbeat", "[evdriver]") {
    Fixture f;
    f.config.heartbeat_wait_s = 0;
    f.init();
    CHECK(f.observations().raised.empty());
    f.ready();
    REQUIRE(f.wait_for([](const auto& out) { return !out.raised.empty(); }));
    const auto error = f.observations().raised.front();
    CHECK(error.type == "generic/CommunicationFault");
    CHECK(error.message.find("test_car") != std::string::npos);
    const std::string heartbeat = "everest_api/1/ev_controller/test_car/e2m/heartbeat";
    REQUIRE(f.adapter.mqtt_callbacks.count(heartbeat) == 1);
    f.adapter.mqtt_callbacks.at(heartbeat)("0");
    REQUIRE(f.wait_for([](const auto& out) { return out.cleared.size() == 1; }));
    CHECK(f.observations().cleared.front().sub_type == error.sub_type);
    f.adapter.mqtt_callbacks.at(heartbeat)("1");
    CHECK(f.observations().cleared.size() == 1);
}

TEST_CASE("EvDriverSim emits heartbeat counters and accepts the car heartbeat before ready", "[evdriver]") {
    Fixture f;
    f.config.cfg_heartbeat_interval_ms = 5;
    f.config.heartbeat_wait_s = 0;
    f.init();
    const auto car_heartbeat = "everest_api/1/ev_controller/test_car/e2m/heartbeat";
    REQUIRE(f.adapter.mqtt_callbacks.count(car_heartbeat) == 1);
    f.adapter.mqtt_callbacks.at(car_heartbeat)("0");
    f.ready();
    const auto heartbeat = f.base + "e2m/heartbeat";
    REQUIRE(f.wait_for([&](const auto& out) {
        return std::count_if(out.mqtt.begin(), out.mqtt.end(),
                             [&](const auto& msg) { return msg.first == heartbeat; }) >= 2;
    }));
    f.mod.reset();
    CHECK(f.adapter.mqtt_callbacks.empty());
    CHECK(f.observations().raised.empty());
    size_t counter = 0;
    for (const auto& [topic, payload] : f.observations().mqtt) {
        if (topic == heartbeat) {
            CHECK(payload == std::to_string(counter++));
        }
    }
}

TEST_CASE("EvDriverSim optional bridge check rejects bad payloads and clears on a check", "[evdriver]") {
    Fixture f;
    f.config.cfg_communication_check_to_s = 1;
    f.init();
    f.ready();
    REQUIRE(f.wait_for([](const auto& out) { return !out.raised.empty(); }));
    f.command("communication_check", "{}");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
    CHECK(f.observations().cleared.empty());
    f.command("communication_check", "true");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    REQUIRE(f.wait_for([](const auto& out) { return !out.cleared.empty(); }));
}

TEST_CASE("EvDriverSim composes CP E hold clear and unplug through MQTT and BSP", "[evdriver]") {
    Fixture f;
    f.adapter.echo_cp = true;
    f.init();
    f.ready();
    f.command("inject_fault", R"({"type":"CpErrorE"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").message == "unplugged");
    f.command("plug");
    f.car_command("set_cp_state", {{"cp_state", "B"}});
    const auto events = f.observations().car.size();
    f.command("inject_fault", R"({"type":"CpErrorE"})");
    CHECK(f.observations().car.size() == events + 1);
    CHECK(f.observations().board.back().second.at("cp_state") == "E");
    CHECK(f.observations().car.back().second.get<types::board_support_common::BspEvent>().event == Event::E);
    const auto calls = f.observations().board.size();
    f.car_command("set_cp_state", {{"cp_state", "C"}});
    CHECK(f.observations().board.size() == calls + 1);
    CHECK(f.observations().car.size() == events + 2);
    CHECK(f.observations().car.back().second.get<types::board_support_common::BspEvent>().event == Event::E);
    CHECK(f.observations().board.back().second.at("cp_state") == "E");
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("cp_state") == "C");
    CHECK_FALSE(f.last_message<api::WorldState>("world_state").fault);
    f.command("inject_fault", R"({"type":"CpErrorE"})");
    f.command("unplug");
    CHECK(f.observations().board.back().second.at("cp_state") == "A");
    CHECK_FALSE(f.last_message<api::WorldState>("world_state").fault);
    const auto messages = f.observations().mqtt.size();
    f.command("clear_fault");
    CHECK(f.observations().mqtt.size() == messages + 1);
    CHECK(f.observations().board.back().second.at("cp_state") == "A");
    f.command("inject_fault", R"({"type":"DiodeFail"})");
    CHECK(f.observations().board.back().first == "diode_fail");
    CHECK(f.observations().board.back().second.at("value") == true);
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("value") == false);
}

TEST_CASE("EvDriverSim reapplies CP E on the next car command after forced F clears", "[evdriver]") {
    Fixture f;
    f.adapter.echo_cp = true;
    f.init();
    f.ready();
    f.command("plug");
    f.car_command("set_cp_state", {{"cp_state", "C"}});
    f.adapter.pwm_error_f = true;
    const auto events = f.observations().car.size();
    f.command("inject_fault", R"({"type":"CpErrorE"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::CpErrorE);
    CHECK(f.observations().board.back().second.at("cp_state") == "E");
    CHECK(f.observations().car.size() == events);
    const auto calls = f.observations().board.size();
    f.adapter.pwm_error_f = false;
    f.car_command("set_cp_state", {{"cp_state", "B"}});
    CHECK(f.observations().board.size() == calls + 1);
    CHECK(f.observations().board.back().second.at("cp_state") == "E");
    CHECK(f.observations().car.size() == events + 1);
    CHECK(f.observations().car.back().second.get<types::board_support_common::BspEvent>().event == Event::E);
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::CpErrorE);
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("cp_state") == "B");
}

TEST_CASE("EvDriverSim rejects repeated plug and unplug without board or car effects", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    auto reject = [&](const std::string& name, const std::string& reason) {
        const auto before = f.observations();
        f.command(name);
        const auto ack = f.last_message<api::CommandAck>("command_ack");
        CHECK(ack.status == api::CommandAckStatus::Rejected);
        CHECK(ack.message == reason);
        const auto after = f.observations();
        CHECK(after.board.size() == before.board.size());
        CHECK(after.car.size() == before.car.size());
        CHECK(after.mqtt.size() == before.mqtt.size() + 1);
    };
    reject("unplug", "already unplugged");
    f.command("plug");
    reject("plug", "already plugged");
    f.car_command("set_cp_state", {{"cp_state", "B"}});
    f.command("inject_fault", R"({"type":"CpErrorE"})");
    reject("plug", "already plugged");
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::CpErrorE);
    f.command("unplug");
    reject("unplug", "already unplugged");
}

TEST_CASE("EvDriverSim world diode fault blocks car clears until clear_fault", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    f.car_command("diode_fail", {{"value", true}});
    f.command("inject_fault", R"({"type":"DiodeFail"})");
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("value") == true);
    f.command("inject_fault", R"({"type":"DiodeFail"})");
    const auto calls = f.observations().board.size();
    f.car_command("diode_fail", {{"value", false}});
    CHECK(f.observations().board.size() == calls);
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::DiodeFail);
    f.command("clear_fault");
    REQUIRE(f.observations().board.size() == calls + 1);
    CHECK(f.observations().board.back().second.at("value") == false);
}

TEST_CASE("EvDriverSim world RCD fault blocks car clears until clear_fault", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    f.car_command("set_rcd_error", {{"rcd_current_mA", 12.0}});
    f.command("inject_fault", R"({"type":"RcdError","rcd_mA":30})");
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("rcd_current_mA") == 12.0);
    f.command("inject_fault", R"({"type":"RcdError","rcd_mA":30})");
    const auto calls = f.observations().board.size();
    f.car_command("set_rcd_error", {{"rcd_current_mA", 0.0}});
    CHECK(f.observations().board.size() == calls);
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::RcdError);
    f.command("clear_fault");
    REQUIRE(f.observations().board.size() == calls + 1);
    CHECK(f.observations().board.back().second.at("rcd_current_mA") == 0.0);
}

TEST_CASE("EvDriverSim reapplies the world diode fault after car disable while withheld", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    f.command("inject_fault", R"({"type":"DiodeFail"})");
    const auto calls = f.observations().board.size();
    f.car_command("enable", {{"value", false}});
    const auto out = f.observations();
    REQUIRE(out.board.size() == calls + 3);
    CHECK(out.board[calls] == std::make_pair(std::string("enable"), Parameters{{"value", false}}));
    CHECK(out.board[calls + 1] == std::make_pair(std::string("diode_fail"), Parameters{{"value", true}}));
    CHECK(out.board[calls + 2] == std::make_pair(std::string("set_cp_state"), Parameters{{"cp_state", "A"}}));
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::DiodeFail);
}

TEST_CASE("EvDriverSim reapplies the injected RCD current after car disable while withheld", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    f.command("inject_fault", R"({"type":"RcdError","rcd_mA":42})");
    f.car_command("set_rcd_error", {{"rcd_current_mA", 0.0}});
    const auto calls = f.observations().board.size();
    f.car_command("enable", {{"value", false}});
    const auto out = f.observations();
    REQUIRE(out.board.size() == calls + 3);
    CHECK(out.board[calls] == std::make_pair(std::string("enable"), Parameters{{"value", false}}));
    CHECK(out.board[calls + 1] == std::make_pair(std::string("set_rcd_error"), Parameters{{"rcd_current_mA", 42.0}}));
    CHECK(out.board[calls + 2] == std::make_pair(std::string("set_cp_state"), Parameters{{"cp_state", "A"}}));
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::RcdError);
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("rcd_current_mA") == 0.0);
}

TEST_CASE("EvDriverSim reapplies CP E after car disable while passed", "[evdriver]") {
    Fixture f;
    f.init();
    f.ready();
    f.command("plug");
    f.command("inject_fault", R"({"type":"CpErrorE"})");
    f.car_command("set_cp_state", {{"cp_state", "C"}});
    const auto calls = f.observations().board.size();
    f.car_command("enable", {{"value", false}});
    const auto out = f.observations();
    REQUIRE(out.board.size() == calls + 2);
    CHECK(out.board[calls] == std::make_pair(std::string("enable"), Parameters{{"value", false}}));
    CHECK(out.board[calls + 1] == std::make_pair(std::string("set_cp_state"), Parameters{{"cp_state", "E"}}));
    CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::CpErrorE);
    f.command("clear_fault");
    CHECK(f.observations().board.back().second.at("cp_state") == "C");
}

TEST_CASE("EvDriverSim refuses scenarios until the controller heartbeat", "[evdriver][scenario]") {
    Fixture f;
    f.init();
    f.ready();
    f.command("run_scenario", R"({"name":"AcIecBasic"})");
    const auto ack = f.last_message<api::CommandAck>("command_ack");
    CHECK(ack.command == "run_scenario");
    CHECK(ack.status == api::CommandAckStatus::Rejected);
    REQUIRE(ack.message.has_value());
    CHECK(ack.message->find("heartbeat") != std::string::npos);
    const auto out = f.observations();
    const auto event = std::find_if(out.mqtt.begin(), out.mqtt.end(),
                                    [&](const auto& msg) { return msg.first == f.base + "e2m/scenario_event"; });
    REQUIRE(event != out.mqtt.end());
    const auto payload = nlohmann::json::parse(event->second);
    CHECK(payload.at("name") == "AcIecBasic");
    CHECK(payload.at("kind") == "Rejected");
    CHECK(out.board.size() == 1);
}

TEST_CASE("EvDriverSim runs timed scenarios on its own clock and refuses overlap", "[evdriver][scenario]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command(
        "run_scenario",
        R"({"name":"AcIecPauseResume","timing":{"pause_at_ms":100,"resume_at_ms":200,"stop_after_ms":300,"unplug_after_ms":400}})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    f.car_ack();
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
    const auto initial = f.observations();
    const auto config = std::find_if(initial.mqtt.begin(), initial.mqtt.end(), [](const auto& msg) {
        return msg.first == "everest_api/1/ev_controller/test_car/m2e/configure_session";
    });
    REQUIRE(config != initial.mqtt.end());
    CHECK(nlohmann::json::parse(config->second).at("mode") == "AcIec");
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
    CHECK(f.last_message<api::CommandAck>("command_ack").message == "scenario already running");
    REQUIRE(f.wait_for([&](const auto& out) {
        return std::any_of(out.mqtt.begin(), out.mqtt.end(), [&](const auto& msg) {
            return msg.first == f.base + "e2m/scenario_event" &&
                   nlohmann::json::parse(msg.second).at("kind") == "Finished";
        });
    }));
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    std::vector<std::string> commands;
    std::vector<std::string> events;
    for (const auto& [topic, payload] : f.observations().mqtt) {
        const std::string prefix = "everest_api/1/ev_controller/test_car/m2e/";
        if (topic.find(prefix) == 0) {
            commands.push_back(topic.substr(prefix.size()));
        }
        if (topic == f.base + "e2m/scenario_event") {
            const auto event = nlohmann::json::parse(payload);
            if (event.at("name") == "AcIecPauseResume") {
                events.push_back(event.at("kind"));
            }
        }
    }
    CHECK(commands == std::vector<std::string>{"configure_session", "pause_session", "resume_session", "stop_session"});
    CHECK(events == std::vector<std::string>{"Started", "StepFired", "StepFired", "StepFired", "StepFired", "StepFired",
                                             "StepFired", "Finished"});
}

TEST_CASE("EvDriverSim waits for configuration acceptance before starting the preset clock",
          "[evdriver][scenario][barrier]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command("run_scenario", R"({"name":"DcIsoD20Basic","timing":{"stop_after_ms":200,"unplug_after_ms":400}})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    REQUIRE(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    CHECK(f.scenario_events().empty());
    const auto before = f.observations();
    CHECK(before.car.size() == 1);
    const auto config = std::find_if(before.mqtt.begin(), before.mqtt.end(), [](const auto& msg) {
        return msg.first == "everest_api/1/ev_controller/test_car/m2e/configure_session";
    });
    REQUIRE(config != before.mqtt.end());
    CHECK(nlohmann::json::parse(config->second).at("mode") == "DcIsoD20");
    CHECK_FALSE(f.wait_for([&](const auto& out) { return out.mqtt.size() != before.mqtt.size(); },
                           std::chrono::milliseconds(450)));
    const auto accepted_at = std::chrono::steady_clock::now();
    f.car_ack();
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
    const auto started = f.scenario_events();
    REQUIRE(started.size() == 3);
    CHECK(started[0].kind == api::ScenarioEventKind::Started);
    CHECK(started[1].message == "configure_session");
    CHECK(started[2].message == "plug");
    REQUIRE(f.wait_for([&](const auto& out) {
        return std::any_of(out.mqtt.begin(), out.mqtt.end(), [&](const auto& msg) {
            return msg.first == "everest_api/1/ev_controller/test_car/m2e/stop_session";
        });
    }));
    CHECK(std::chrono::steady_clock::now() >= accepted_at + std::chrono::milliseconds(200));
    REQUIRE(f.wait_for([&](const auto& out) {
        return std::any_of(out.mqtt.begin(), out.mqtt.end(), [&](const auto& msg) {
            return msg.first == f.base + "e2m/scenario_event" &&
                   nlohmann::json::parse(msg.second).at("kind") == "Finished";
        });
    }));
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    const auto out = f.observations();
    CHECK(std::count_if(out.mqtt.begin(), out.mqtt.end(),
                        [&](const auto& msg) { return msg.first == f.base + "e2m/command_ack"; }) == 1);
}

TEST_CASE("EvDriverSim abandons configuration on car rejection or acknowledgement timeout",
          "[evdriver][scenario][configuration-failure]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    std::string reason;
    SECTION("rejected configuration") {
        reason = "session already active";
        f.car_ack(R"({"command":"configure_session","status":"Rejected","reason":"session already active"})");
    }
    SECTION("missing acknowledgement") {
        reason = "timed out waiting for EvController configure_session acknowledgement";
    }
    REQUIRE(f.wait_for(
        [&](const auto& out) {
            return std::any_of(out.mqtt.begin(), out.mqtt.end(), [&](const auto& msg) {
                return msg.first == f.base + "e2m/scenario_event" &&
                       nlohmann::json::parse(msg.second).at("kind") == "Rejected";
            });
        },
        std::chrono::seconds(11)));
    const auto events = f.scenario_events();
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == api::ScenarioEventKind::Rejected);
    CHECK(events.front().message == reason);
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    CHECK(f.observations().car.size() == 1);
    f.car_ack();
    CHECK(f.scenario_events().size() == 1);
    CHECK(f.observations().car.size() == 1);
    const auto out = f.observations();
    CHECK(std::count_if(out.mqtt.begin(), out.mqtt.end(),
                        [&](const auto& msg) { return msg.first == f.base + "e2m/command_ack"; }) == 1);
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    f.car_ack();
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
}

TEST_CASE("EvDriverSim refuses another preset after DcIsoBasic leaves the pilot passed",
          "[evdriver][scenario][plugged-start]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    f.car_ack();
    CHECK(f.scenario_events().back().kind == api::ScenarioEventKind::Finished);
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
    const auto before = f.observations();
    const auto events_before = f.scenario_events().size();
    f.command("run_scenario", R"({"name":"DiodeFailSmoke","timing":{"fault_at_ms":20,"clear_fault_at_ms":40}})");
    const auto ack = f.last_message<api::CommandAck>("command_ack");
    CHECK(ack.status == api::CommandAckStatus::Rejected);
    CHECK(ack.message == "already plugged");
    const auto events = f.scenario_events();
    REQUIRE(events.size() == events_before + 1);
    CHECK(events.back().name == api::ScenarioName::DiodeFailSmoke);
    CHECK(events.back().kind == api::ScenarioEventKind::Rejected);
    CHECK(events.back().message == ack.message);
    const auto after = f.observations();
    CHECK(after.board == before.board);
    CHECK(after.car == before.car);
    CHECK(after.mqtt.size() == before.mqtt.size() + 2);
    f.car_ack();
    CHECK_FALSE(f.wait_for([&](const auto& out) { return out.mqtt.size() != after.mqtt.size(); },
                           std::chrono::milliseconds(100)));
}

TEST_CASE("EvDriverSim stops a scenario when a world step is refused", "[evdriver][scenario][step-refusal]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command("run_scenario", R"({"name":"DiodeFailSmoke","timing":{"fault_at_ms":100,"clear_fault_at_ms":200}})");
    std::string reason;
    bool started = false;
    SECTION("initial plug refused after another caller plugs while configuration is pending") {
        reason = "already plugged";
        f.command("plug");
        f.car_ack();
    }
    SECTION("later fault injection refused because another fault is active") {
        started = true;
        reason = "a fault is already active";
        f.car_ack();
        f.command("inject_fault", R"({"type":"RcdError","rcd_mA":30})");
    }
    REQUIRE(f.wait_for([&](const auto& out) {
        return std::any_of(out.mqtt.begin(), out.mqtt.end(), [&](const auto& msg) {
            return msg.first == f.base + "e2m/scenario_event" &&
                   nlohmann::json::parse(msg.second).at("kind") == "Rejected";
        });
    }));
    const auto events = f.scenario_events();
    REQUIRE(events.size() == (started ? 4 : 1));
    CHECK(events.back().kind == api::ScenarioEventKind::Rejected);
    CHECK(events.back().message == reason);
    CHECK(std::none_of(events.begin(), events.end(), [](const auto& event) {
        return event.kind == api::ScenarioEventKind::Finished || event.message == "inject_fault";
    }));
    const auto rejected = f.observations();
    CHECK_FALSE(f.wait_for([&](const auto& out) { return out.mqtt.size() != rejected.mqtt.size(); },
                           std::chrono::milliseconds(250)));
    CHECK(f.observations().board == rejected.board);
    if (started) {
        CHECK(f.last_message<api::WorldState>("world_state").fault == api::WorldFault::RcdError);
    }
    CHECK(std::count_if(rejected.mqtt.begin(), rejected.mqtt.end(), [&](const auto& msg) {
              return msg.first == f.base + "e2m/command_ack" &&
                     nlohmann::json::parse(msg.second).at("command") == "run_scenario";
          }) == 1);
    f.command("unplug");
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
}

TEST_CASE("EvDriverSim owns one pending configuration and ignores unrelated acknowledgements", "[evdriver][scenario]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.car_ack();
    CHECK(f.scenario_events().empty());
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    const auto pending = f.observations();
    for (const auto& ack : {R"({"command":"pause_session","status":"Accepted"})",
                            R"({"command":"stop_session","status":"Rejected","reason":"not running"})",
                            R"({"command":"configure_session","status":"invalid"})", "{"}) {
        f.car_ack(ack);
    }
    CHECK(f.scenario_events().empty());
    CHECK(f.observations().mqtt == pending.mqtt);
    CHECK(f.observations().car == pending.car);
    SECTION("pending run refuses a second request and retains the first configuration") {
        f.command("run_scenario", R"({"name":"AcIecBasic"})");
        CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
        CHECK(f.last_message<api::CommandAck>("command_ack").message == "scenario already running");
        const auto events = f.scenario_events();
        REQUIRE(events.size() == 1);
        CHECK(events.front().kind == api::ScenarioEventKind::Rejected);
        CHECK(events.front().name == api::ScenarioName::AcIecBasic);
        CHECK(f.observations().mqtt.size() == pending.mqtt.size() + 2);
        f.car_ack();
        CHECK(f.scenario_events().back().kind == api::ScenarioEventKind::Finished);
        CHECK(f.scenario_events().back().name == api::ScenarioName::DcIsoBasic);
        const auto finished = f.observations();
        f.car_ack();
        CHECK(f.observations().mqtt == finished.mqtt);
        CHECK(f.observations().car == finished.car);
    }
    SECTION("shutdown cancels the pending wait promptly") {
        const auto shutdown_at = std::chrono::steady_clock::now();
        f.mod.reset();
        CHECK(std::chrono::steady_clock::now() < shutdown_at + std::chrono::seconds(1));
        CHECK(f.adapter.mqtt_callbacks.empty());
        CHECK(f.observations().mqtt == pending.mqtt);
        CHECK(f.observations().car == pending.car);
    }
}

TEST_CASE("EvDriverSim contains configuration callback failures and marks uncertain world state",
          "[evdriver][scenario][ack-exception]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    SECTION("car BSP publication throws during plug") {
        f.adapter.fail_next("car:bsp_event");
    }
    SECTION("world state publication throws after plug") {
        f.adapter.fail_next("mqtt:" + f.base + "e2m/world_state");
    }
    SECTION("Started publication throws after plug") {
        f.adapter.fail_next("mqtt:" + f.base + "e2m/scenario_event");
    }
    CHECK_NOTHROW(f.car_ack());
    f.command("communication_check", "true");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    const auto events = f.scenario_events();
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == api::ScenarioEventKind::Rejected);
    REQUIRE(events.front().message);
    CHECK(events.front().message->find("plug") != std::string::npos);
    CHECK(events.front().message->find("test_car") != std::string::npos);
    CHECK(events.front().message->find("injected") != std::string::npos);
    CHECK(events.front().message->find("world state may be partially applied") != std::string::npos);
    f.command("unplug");
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    f.car_ack();
    CHECK(f.scenario_events().back().kind == api::ScenarioEventKind::Finished);
}

TEST_CASE("EvDriverSim reports timed action exceptions with step and car context",
          "[evdriver][scenario][timer-exception]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    std::string step;
    SECTION("board RPC throws during fault injection") {
        step = "inject_fault";
        f.adapter.fail_next("board:diode_fail");
        f.command("run_scenario", R"({"name":"DiodeFailSmoke","timing":{"fault_at_ms":100,"clear_fault_at_ms":200}})");
    }
    SECTION("MQTT publish throws during pause intent") {
        step = "pause_session";
        f.adapter.fail_next("mqtt:everest_api/1/ev_controller/test_car/m2e/pause_session");
        f.command(
            "run_scenario",
            R"({"name":"AcIecPauseResume","timing":{"pause_at_ms":100,"resume_at_ms":200,"stop_after_ms":300,"unplug_after_ms":400}})");
    }
    f.car_ack();
    REQUIRE(f.wait_for([&](const auto& out) {
        return std::any_of(out.mqtt.begin(), out.mqtt.end(), [&](const auto& msg) {
            return msg.first == f.base + "e2m/scenario_event" &&
                   nlohmann::json::parse(msg.second).at("kind") == "Rejected";
        });
    }));
    const auto events = f.scenario_events();
    REQUIRE(events.size() == 4);
    CHECK(events.back().kind == api::ScenarioEventKind::Rejected);
    REQUIRE(events.back().message);
    CHECK(events.back().message->find("step " + step) != std::string::npos);
    CHECK(events.back().message->find("test_car") != std::string::npos);
    CHECK(events.back().message->find("injected") != std::string::npos);
    if (step == "inject_fault") {
        CHECK(events.back().message->find("world state may be partially applied") != std::string::npos);
    }
    const auto failed = f.observations();
    CHECK_FALSE(f.wait_for([&](const auto& out) { return out.mqtt.size() != failed.mqtt.size(); },
                           std::chrono::milliseconds(450)));
    CHECK(f.observations().board == failed.board);
    f.command("communication_check", "true");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    f.command("clear_fault");
    f.command("unplug");
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    f.car_ack();
    CHECK(f.scenario_events().back().kind == api::ScenarioEventKind::Finished);
}

TEST_CASE("EvDriverSim cancels a scenario whose configuration publish throws",
          "[evdriver][scenario][start-exception]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.adapter.fail_next("mqtt:everest_api/1/ev_controller/test_car/m2e/configure_session");
    CHECK_NOTHROW(f.command("run_scenario", R"({"name":"DcIsoBasic"})"));
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
    const auto events = f.scenario_events();
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == api::ScenarioEventKind::Rejected);
    REQUIRE(events.front().message);
    CHECK(events.front().message->find("configure_session") != std::string::npos);
    CHECK(events.front().message->find("test_car") != std::string::npos);
    f.car_ack();
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
    CHECK(f.scenario_events().size() == 1);
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
    f.car_ack();
    CHECK(f.scenario_events().back().kind == api::ScenarioEventKind::Finished);
}

TEST_CASE("EvDriverSim remains usable when reporting a scenario exception also throws", "[evdriver][scenario]") {
    for (const bool timed : {false, true}) {
        for (const bool nonstandard : {false, true}) {
            CAPTURE(timed, nonstandard);
            Fixture f;
            f.init();
            f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
            f.ready();
            f.adapter.fail_next(timed ? "board:diode_fail" : "car:bsp_event", nonstandard);
            f.adapter.fail_next("scenario_event:Rejected", nonstandard);
            f.command("run_scenario",
                      R"({"name":"DiodeFailSmoke","timing":{"fault_at_ms":100,"clear_fault_at_ms":200}})");
            CHECK_NOTHROW(f.car_ack());
            REQUIRE(f.wait_for([](const auto& out) { return out.failed_operations.size() == 2; }));
            f.command("communication_check", "true");
            CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
            const auto failed = f.observations();
            CHECK_FALSE(f.wait_for([&](const auto& out) { return out.mqtt.size() != failed.mqtt.size(); },
                                   std::chrono::milliseconds(250)));
            CHECK(f.observations().board == failed.board);
            CHECK(f.scenario_events().size() == (timed ? 3 : 0));
            f.command("clear_fault");
            f.command("unplug");
            f.command("run_scenario", R"({"name":"DcIsoBasic"})");
            CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
            f.car_ack();
            CHECK(f.scenario_events().back().kind == api::ScenarioEventKind::Finished);
        }
    }
}

TEST_CASE("EvDriverSim preserves a pending run if reporting an overlapping rejection throws",
          "[evdriver][scenario][overlap-report-exception]") {
    Fixture f;
    f.init();
    f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
    f.ready();
    f.command("run_scenario", R"({"name":"DcIsoBasic"})");
    f.adapter.fail_next("scenario_event:Rejected");
    CHECK_NOTHROW(f.command("run_scenario", R"({"name":"AcIecBasic"})"));
    CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Rejected);
    f.car_ack();
    CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
    const auto events = f.scenario_events();
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().name == api::ScenarioName::DcIsoBasic);
    CHECK(events.back().kind == api::ScenarioEventKind::Finished);
}

TEST_CASE("EvDriverSim contains command acknowledgement failures at the external callback",
          "[evdriver][scenario][command-ack-exception]") {
    for (const bool controller_seen : {false, true}) {
        for (const bool nonstandard : {false, true}) {
            CAPTURE(controller_seen, nonstandard);
            std::ostringstream logs;
            const auto sink = boost::log::add_console_log(logs);
            struct RemoveSink {
                boost::shared_ptr<boost::log::sinks::sink> sink;
                ~RemoveSink() {
                    boost::log::core::get()->remove_sink(sink);
                }
            } remove_sink{sink};
            Fixture f;
            f.init();
            if (controller_seen) {
                f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
            }
            f.ready();
            const auto ack_topic = f.base + "e2m/command_ack";
            f.adapter.fail_next("mqtt:" + ack_topic, nonstandard);
            auto worker = std::async(std::launch::async, [&] {
                f.adapter.mqtt_callbacks.at(f.base + "m2e/run_scenario")(R"({"name":"AcIecBasic"})");
                f.adapter.mqtt_callbacks.at(f.base + "m2e/communication_check")("true");
            });
            REQUIRE_NOTHROW(worker.get());
            CHECK(f.observations().failed_operations == std::vector<std::string>{"mqtt:" + ack_topic});
            const auto out = f.observations();
            CHECK(std::count_if(out.mqtt.begin(), out.mqtt.end(),
                                [&](const auto& message) { return message.first == ack_topic; }) == 1);
            const auto ack = f.last_message<api::CommandAck>("command_ack");
            CHECK(ack.command == "communication_check");
            CHECK(ack.status == api::CommandAckStatus::Accepted);
            sink->flush();
            CHECK(logs.str().find("command_ack") != std::string::npos);
            CHECK(logs.str().find("run_scenario") != std::string::npos);
            CHECK(logs.str().find(nonstandard ? "unknown exception" : "injected") != std::string::npos);
            if (controller_seen) {
                CHECK(f.scenario_events().empty());
                f.command("run_scenario", R"({"name":"AcIecBasic"})");
                CHECK(f.last_message<api::CommandAck>("command_ack").message == "scenario already running");
                f.car_ack();
                CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Passed);
                const auto events = f.scenario_events();
                CHECK(std::any_of(events.begin(), events.end(),
                                  [](const auto& event) { return event.kind == api::ScenarioEventKind::Started; }));
            } else {
                REQUIRE(f.scenario_events().size() == 1);
                CHECK(f.scenario_events().front().kind == api::ScenarioEventKind::Rejected);
                f.car_ack();
                CHECK(f.scenario_events().size() == 1);
                CHECK(f.last_message<api::WorldState>("world_state").pilot == api::PilotState::Withheld);
                f.adapter.mqtt_callbacks.at("everest_api/1/ev_controller/test_car/e2m/heartbeat")("0");
                f.command("run_scenario", R"({"name":"AcIecBasic"})");
                CHECK(f.last_message<api::CommandAck>("command_ack").status == api::CommandAckStatus::Accepted);
            }
        }
    }
}
