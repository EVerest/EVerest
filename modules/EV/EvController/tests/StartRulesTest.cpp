// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "../ev_manager/ev_managerImpl.hpp"
#include "../main/states/Charging.hpp"
#include "../main/states/Plugged.hpp"
#include "../main/states/Unplugged.hpp"
#include "TestFixture.hpp"
#include <ModuleAdapterStub.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <everest/util/async/monitor.hpp>
#include <everest/util/fsm/fsm.hpp>
#include <everest_api_types/ev_controller/codec.hpp>
#include <stdexcept>
#include <utility>

using namespace module;
using namespace module::test;
namespace api = API_types::ev_controller;

namespace {
Event pwm(float duty) {
    BspMeasurementPayload measurement{};
    measurement.cp_pwm_duty_cycle = duty;
    return Event{measurement};
}
} // namespace

TEST_CASE("EvController pilot ISO waits for the 5 percent band", "[evctl][start-rules]") {
    const auto duty = GENERATE(0.0f, 4.0f, 4.1f, 5.0f, 6.9f, 7.0f, 97.0f, 100.0f);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::DcIso2SessionParams{};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    machine.feed(fx.timer.enqueued_events.back());
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(fx.mocks.slac.records.empty());
    machine.feed(pwm(duty));
    const bool matching = duty > 4 && duty < 7;
    CHECK(machine.get_current_state_id() == (matching ? api::FsmState::SlacMatching : api::FsmState::Plugged));
    CHECK(contains_substr(fx.mocks.slac.records, "trigger_matching()") == matching);
}

TEST_CASE("EvController nominal PWM bounds the ISO wait", "[evctl][start-rules]") {
    const auto mode = GENERATE(api::ChargeMode::AcIso2, api::ChargeMode::AcIsoD20, api::ChargeMode::DcIso2,
                               api::ChargeMode::DcIsoD20);
    TestFixture fx;
    fx.cfg.iso_pwm_wait_s = 2;
    auto ctx = fx.make_ctx();
    if (mode == api::ChargeMode::AcIso2)
        ctx->configured_session = api::AcIso2SessionParams{};
    if (mode == api::ChargeMode::AcIsoD20)
        ctx->configured_session = api::AcIsoD20SessionParams{};
    if (mode == api::ChargeMode::DcIso2)
        ctx->configured_session = api::DcIso2SessionParams{};
    if (mode == api::ChargeMode::DcIsoD20)
        ctx->configured_session = api::DcIsoD20SessionParams{};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    machine.feed(fx.timer.enqueued_events.back());
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(fx.timer.state_timer_arms.empty());
    machine.feed(pwm(50));
    machine.feed(pwm(60));
    REQUIRE(fx.timer.state_timer_arms == std::vector<std::chrono::milliseconds>{std::chrono::seconds(2)});
    machine.feed(Event{StateDeadlineEvt{}});
    if (mode == api::ChargeMode::AcIso2 || mode == api::ChargeMode::AcIsoD20) {
        CHECK(machine.get_current_state_id() == api::FsmState::Charging);
        CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
        CHECK(contains_substr(fx.mocks.bsp.records, "set_cp_state(cp_state=C)"));
        CHECK(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
        CHECK(fx.timer.tick_arms == std::vector<int>{fx.cfg.tick_interval_ms});
    } else {
        CHECK(machine.get_current_state_id() == api::FsmState::Faulted);
        REQUIRE(ctx->vars.last_fault);
        CHECK(ctx->vars.last_fault->type == api::FaultType::Internal);
        CHECK(ctx->vars.last_fault->message == "no 5 % PWM");
        CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
    }
    CHECK(fx.mocks.slac.records.empty());
    CHECK_FALSE(contains_substr(fx.mocks.iso.records, "start_charging"));
}

TEST_CASE("EvController 5 percent PWM cancels the fallback wait", "[evctl][start-rules]") {
    TestFixture fx;
    fx.cfg.iso_pwm_wait_s = 10;
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::AcIso2SessionParams{};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    machine.feed(fx.timer.enqueued_events.back());
    machine.feed(pwm(50));
    REQUIRE(fx.timer.state_timer_arms.size() == 1);
    const auto cancels = fx.timer.state_timer_cancels;
    machine.feed(pwm(5));
    CHECK(machine.get_current_state_id() == api::FsmState::SlacMatching);
    CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIso2);
    CHECK(fx.timer.state_timer_cancels == cancels + 1);
    CHECK(fx.timer.state_timer_arms.back() == std::chrono::seconds(30));
    CHECK_FALSE(contains_substr(fx.mocks.bsp.records, "allow_power_on(value=true)"));
}

namespace {
void stop_and_finish(TestFixture& fx, fsm::v2::FSM<StateBase>& machine, Event stop) {
    machine.feed(std::move(stop));
    REQUIRE(machine.get_current_state_id() == api::FsmState::Stopping);
    machine.feed(Event{IsoV2GFinishedEvt{}});
    REQUIRE(machine.get_current_state_id() == api::FsmState::Plugged);
    while (!fx.timer.enqueued_events.empty()) {
        auto ev = std::move(fx.timer.enqueued_events.front());
        fx.timer.enqueued_events.erase(fx.timer.enqueued_events.begin());
        machine.feed(std::move(ev));
    }
}
} // namespace

TEST_CASE("EvController restarts on the resumed 5 percent PWM after a charger stop", "[evctl][start-rules][restart]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    api::DcIso2SessionParams session;
    session.payment = api::PaymentOption::ExternalPayment;
    ctx->configured_session = session;
    set_mode(*ctx, api::ChargeMode::DcIso2);
    fsm::v2::FSM<StateBase> machine{std::make_unique<Charging>(*ctx)};
    fx.mocks.slac.clear();

    stop_and_finish(fx, machine, Event{IsoStopFromChargerEvt{}});

    CHECK_FALSE(ctx->vars.hold_session);
    REQUIRE(ctx->vars.charge_mode() == api::ChargeMode::DcIso2);
    CHECK(ctx->vars.session->payment == api::PaymentOption::ExternalPayment);
    CHECK(fx.mocks.slac.records == std::vector<std::string>{"reset()"});
    // The charger keeps 5 % on briefly after the stop; that is not the wake-up.
    machine.feed(pwm(5.0f));
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    machine.feed(pwm(100.0f));
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    machine.feed(pwm(5.0f));
    CHECK(machine.get_current_state_id() == api::FsmState::SlacMatching);
    CHECK(fx.mocks.slac.records == std::vector<std::string>{"reset()", "trigger_matching()"});
}

TEST_CASE("EvController waits in Plugged after a charger stop with no wake-up", "[evctl][start-rules][restart]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::DcIso2SessionParams{};
    set_mode(*ctx, api::ChargeMode::DcIso2);
    fsm::v2::FSM<StateBase> machine{std::make_unique<Charging>(*ctx)};
    fx.mocks.slac.clear();

    stop_and_finish(fx, machine, Event{IsoStopFromChargerEvt{}});
    for (int i = 0; i < 20; ++i) {
        machine.feed(pwm(5.0f));
    }
    machine.feed(Event{StateDeadlineEvt{}});

    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK_FALSE(contains_substr(fx.mocks.slac.records, "trigger_matching()"));
    CHECK_FALSE(ctx->vars.last_fault);
}

TEST_CASE("EvController stays held after an EV stop despite 5 percent PWM", "[evctl][start-rules][restart]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::DcIso2SessionParams{};
    set_mode(*ctx, api::ChargeMode::DcIso2);
    fsm::v2::FSM<StateBase> machine{std::make_unique<Charging>(*ctx)};
    fx.mocks.slac.clear();

    stop_and_finish(fx, machine, Event{StopSessionCmd{}});

    CHECK(ctx->vars.hold_session);
    CHECK_FALSE(ctx->vars.session);
    machine.feed(pwm(5.0f));
    CHECK(machine.get_current_state_id() == api::FsmState::Plugged);
    CHECK(fx.mocks.slac.records.empty());
}

TEST_CASE("EvController pilot MCS wakes before matching", "[evctl][start-rules]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    api::DcIsoD20SessionParams session;
    session.mcs_enabled = true;
    ctx->configured_session = session;
    std::vector<std::string> calls;
    ctx->peer_actions.slac.reset = [&] {
        fx.mocks.slac.call_reset();
        calls.push_back("reset");
    };
    ctx->peer_actions.bsp.set_cp = [&](auto cp) {
        fx.mocks.bsp.call_set_cp_state(cp);
        calls.push_back(types::ev_board_support::ev_cp_state_to_string(cp));
    };
    ctx->peer_actions.slac.trigger_matching = [&] {
        calls.push_back("trigger_matching");
        return fx.mocks.slac.call_trigger_matching();
    };
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    machine.feed(Event{BspEventPayload{types::board_support_common::BspEvent{types::board_support_common::Event::A}}});
    machine.feed(fx.timer.enqueued_events.back());
    machine.feed(pwm(5));
    REQUIRE(machine.get_current_state_id() == api::FsmState::SlacMatching);
    CHECK(calls == std::vector<std::string>{"A", "reset", "B", "C"});
    REQUIRE(fx.timer.state_timer_arms == std::vector<std::chrono::milliseconds>{std::chrono::seconds(4)});
    machine.feed(Event{SlacStatePayload{"MATCHED"}});
    REQUIRE(machine.get_current_state_id() == api::FsmState::SlacMatching);
    machine.feed(Event{StateDeadlineEvt{}});
    CHECK(calls == std::vector<std::string>{"A", "reset", "B", "C", "B", "trigger_matching"});
    CHECK(machine.get_current_state_id() == api::FsmState::SlacMatching);
    CHECK(fx.timer.state_timer_arms.back() == std::chrono::seconds(30));
}

namespace module {
static EvController* test_module;
void LdEverest::init(ModuleConfigs, const ModuleInfo&) {
    test_module->init();
}
void LdEverest::ready() {
    test_module->ready();
}
} // namespace module

namespace {
struct ModuleObservations {
    MockBoardSupport board;
    std::vector<Everest::error::Error> errors;
    std::map<std::string, nlohmann::json> publications;
};

struct Adapter : module::stub::QuietModuleAdapterStub {
    everest::lib::util::monitor<ModuleObservations> observed;
    std::map<std::string, StringHandler> commands;
    std::map<std::string, ValueCallback> peers;
    void subscribe_fn(const Requirement&, const std::string& name, ValueCallback callback) override {
        peers[name] = std::move(callback);
    }
    std::shared_ptr<Everest::error::ErrorTypeMap> error_types = std::make_shared<Everest::error::ErrorTypeMap>();
    std::shared_ptr<Everest::error::ErrorDatabaseMap> errors = std::make_shared<Everest::error::ErrorDatabaseMap>();
    Adapter() {
        error_types->load_error_types_map({{"generic/CommunicationFault", "Communication lost"}});
    }
    std::shared_ptr<Everest::error::ErrorManagerImpl> get_error_manager_impl_fn(const std::string&) override {
        return std::make_shared<Everest::error::ErrorManagerImpl>(
            error_types, errors, std::list<Everest::error::ErrorType>{"generic/CommunicationFault"},
            [this](const auto& error) {
                observed.handle()->errors.push_back(error);
                observed.notify_all();
            },
            [](const auto&) {});
    }
    std::shared_ptr<Everest::error::ErrorStateMonitor> get_error_state_monitor_impl_fn(const std::string&) override {
        return std::make_shared<Everest::error::ErrorStateMonitor>(errors);
    }
    std::shared_ptr<Everest::error::ErrorFactory> get_error_factory_fn(const std::string&) override {
        return std::make_shared<Everest::error::ErrorFactory>(
            error_types, ImplementationIdentifier{"test_car", "ev_manager"}, Everest::error::Severity::High);
    }
    Result call_fn(const Requirement&, const std::string& name, Parameters params) override {
        if (name == "set_ac_max_current") {
            observed.handle()->board.call_set_ac_max_current(params.at("current"));
            observed.notify_all();
        }
        return {};
    }
    void ext_mqtt_publish_fn(const std::string& topic, const std::string& payload) override {
        observed.handle()->publications[topic] = nlohmann::json::parse(payload);
        observed.notify_all();
    }
    Everest::UnsubscribeToken ext_mqtt_subscribe_fn(const std::string& topic, StringHandler callback) override {
        commands[topic] = std::move(callback);
        return [this, topic] { commands.erase(topic); };
    }
};

struct ModuleFixture {
    TestFixture fx;
    Adapter adapter;
    Everest::MqttProvider mqtt{adapter};
    module::ev_manager::Conf impl_config;
    Everest::PtrContainer<EvController> ptr;
    std::unique_ptr<EvController> mod;
    void init(bool with_iso = false) {
        std::vector<std::unique_ptr<ISO15118_evIntf>> iso;
        if (with_iso) {
            iso.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "iso", std::nullopt));
        }
        ModuleInfo info;
        info.id = "test_car";
        mod = std::make_unique<EvController>(
            info, mqtt, std::make_unique<module::ev_manager::ev_managerImpl>(&adapter, ptr, impl_config),
            std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "board", std::nullopt),
            std::move(iso), std::vector<std::unique_ptr<ev_slacIntf>>{}, std::vector<std::unique_ptr<kvsIntf>>{},
            fx.cfg);
        ptr.set(mod.get());
        adapter.gather_cmds(*mod->p_ev_manager);
        module::test_module = mod.get();
        module::LdEverest::init({}, info);
    }
    void ready() {
        module::test_module = mod.get();
        module::LdEverest::ready();
    }
    void command(const std::string& name, const std::string& payload = "") {
        adapter.commands.at("everest_api/1/ev_controller/test_car/m2e/" + name)(payload);
    }
    bool wait_for_current() {
        auto out = adapter.observed.handle();
        return out.wait_for([&] { return !out->board.records.empty(); }, std::chrono::seconds(2));
    }
};
} // namespace

TEST_CASE("EvController default session is parsed at init and used by BeginSession", "[evctl][start-rules]") {
    ModuleFixture f;
    f.fx.cfg.default_session = R"({"mode":"AcIec","params":{"charging_current_a":12,"three_phases":false}})";
    f.init();
    CHECK(f.adapter.observed.handle()->errors.empty());
    CHECK(f.adapter.observed.handle()->board.records.empty());
    f.fx.cfg.default_session = "invalid after init";
    f.ready();
    f.command("enable", "true");
    f.adapter.peers.at("bsp_event")(nlohmann::json{{"event", "A"}});
    REQUIRE(f.wait_for_current());
    f.mod.reset();
    CHECK(f.adapter.observed.handle()->board.records == std::vector<std::string>{"set_ac_max_current(current=12)"});
    CHECK(f.adapter.observed.handle()->errors.empty());
}

TEST_CASE("EvController invalid default session faults in ready and falls back to AcIec", "[evctl][start-rules]") {
    ModuleFixture f;
    f.fx.cfg.default_session = GENERATE(std::string{"{"}, std::string{R"({"mode":"unknown"})"});
    REQUIRE_NOTHROW(f.init());
    CHECK(f.adapter.observed.handle()->errors.empty());
    f.ready();
    {
        auto out = f.adapter.observed.handle();
        REQUIRE(out->errors.size() == 1);
        CHECK(out->errors.front().type == "generic/CommunicationFault");
        CHECK(out->errors.front().message.find("default_session") != std::string::npos);
        CHECK(out->errors.front().message.find(f.fx.cfg.default_session == "{" ? "parse_error" : "unknown") !=
              std::string::npos);
    }
    f.command("enable", "true");
    f.adapter.peers.at("bsp_event")(nlohmann::json{{"event", "A"}});
    REQUIRE(f.wait_for_current());
    f.mod.reset();
    CHECK(f.adapter.observed.handle()->board.records == std::vector<std::string>{"set_ac_max_current(current=16)"});
}

TEST_CASE("EvController configured session overrides the default session", "[evctl][start-rules]") {
    ModuleFixture f;
    f.fx.cfg.default_session = R"({"mode":"AcIec","params":{"charging_current_a":12}})";
    f.init();
    f.ready();
    f.command("configure_session", R"({"mode":"AcIec","params":{"charging_current_a":9}})");
    f.command("enable", "true");
    f.adapter.peers.at("bsp_event")(nlohmann::json{{"event", "A"}});
    REQUIRE(f.wait_for_current());
    f.mod.reset();
    CHECK(f.adapter.observed.handle()->board.records == std::vector<std::string>{"set_ac_max_current(current=9)"});
}

TEST_CASE("EvController default session gets configure_session validation", "[evctl][start-rules]") {
    ModuleFixture f;
    f.fx.cfg.default_session = GENERATE(
        std::string{
            R"({"mode":"AcIec","params":{"curve":{"loop":true,"points":[{"t_offset_ms":0,"current_a":12,"three_phases":false}]}}})"},
        std::string{R"({"mode":"DcIso2"})"});
    f.init();
    CHECK(f.adapter.observed.handle()->errors.empty());
    f.ready();
    REQUIRE(f.adapter.observed.handle()->errors.size() == 1);
    CHECK(f.adapter.observed.handle()->errors.front().type == "generic/CommunicationFault");
    f.command("enable", "true");
    f.adapter.peers.at("bsp_event")(nlohmann::json{{"event", "A"}});
    REQUIRE(f.wait_for_current());
    f.mod.reset();
    CHECK(f.adapter.observed.handle()->board.records == std::vector<std::string>{"set_ac_max_current(current=16)"});
}

TEST_CASE("EvController nonpositive PWM wait expires without arming a disabled timer", "[evctl][start-rules]") {
    TestFixture fx;
    fx.cfg.iso_pwm_wait_s = GENERATE(0, -1);
    auto ctx = fx.make_ctx();
    ctx->configured_session = api::AcIso2SessionParams{};
    fsm::v2::FSM<StateBase> machine{std::make_unique<Plugged>(*ctx)};
    machine.feed(fx.timer.enqueued_events.back());
    fx.timer.enqueued_events.clear();
    machine.feed(pwm(50));
    REQUIRE(fx.timer.state_timer_arms == std::vector<std::chrono::milliseconds>{std::chrono::milliseconds(1)});
    machine.feed(Event{StateDeadlineEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::Charging);
    CHECK(ctx->vars.charge_mode() == api::ChargeMode::AcIec);
}

TEST_CASE("EvController subscribes only to car commands", "[evctl][world-commands]") {
    ModuleFixture f;
    f.init();
    f.ready();
    const std::string base = "everest_api/1/ev_controller/test_car/m2e/";
    for (const auto verb : {"plug", "unplug", "inject_fault", "clear_fault"}) {
        INFO(verb);
        CHECK_FALSE(f.adapter.commands.count(base + verb));
    }
    for (const auto verb : {"configure_session", "stop_session", "pause_session", "resume_session", "query_state",
                            "bcb_toggle", "raise_error", "clear_error"}) {
        INFO(verb);
        CHECK(f.adapter.commands.count(base + verb) == 1);
    }
}

TEST_CASE("EvController DER pilot start selects the requested annex", "[evctl][der]") {
    const auto sae = GENERATE(false, true);
    TestFixture fx;
    auto ctx = fx.make_ctx();
    api::AcIsoD20DerParams params{};
    params.charging_current_a = 12;
    params.three_phases = false;
    params.payment = api::PaymentOption::Contract;
    params.departure_time_s = 3600;
    params.e_amount_wh = 22000;
    ctx->configure_session(sae ? api::SessionConfigParams{api::AcIsoD20DerSaeSessionParams{params}}
                               : api::SessionConfigParams{api::AcIsoD20DerIecSessionParams{params}});
    REQUIRE(ctx->configured_session);
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    machine.feed(pilot_input(::types::board_support_common::Event::A));
    machine.feed(fx.timer.enqueued_events.back());
    CHECK(ctx->vars.charging_current_a == 12);
    CHECK_FALSE(ctx->vars.three_phases);
    machine.feed(pwm(5));
    REQUIRE(machine.get_current_state_id() == api::FsmState::SlacMatching);
    machine.feed(Event{SlacStatePayload{"MATCHED"}});
    REQUIRE(machine.get_current_state_id() == api::FsmState::V2GNegotiating);
    CHECK(contains_substr(fx.mocks.iso.records,
                          sae ? "start_charging(mode=AC_DER_SAE," : "start_charging(mode=AC_DER_IEC,"));
    CHECK(contains_substr(fx.mocks.iso.records, "departure_time=3600,e_amount=22000"));
    machine.feed(Event{IsoPowerReadyEvt{}});
    CHECK(machine.get_current_state_id() == api::FsmState::Charging);
    CHECK(contains_substr(fx.mocks.bsp.records, "set_ac_max_current(current=12)"));
}

TEST_CASE("EvController republishes DER peer summaries", "[evctl][der-outputs]") {
    const auto sae = GENERATE(false, true);
    ModuleFixture f;
    f.init(true);
    f.ready();
    const nlohmann::json negotiated{{"flavor", sae ? "AC_DER_SAE" : "AC_DER_IEC"}, {"functions", {"one", "two"}}};
    const nlohmann::json control = sae ? nlohmann::json{{"flavor", "AC_DER_SAE"},
                                                        {"source", "ChargeLoop"},
                                                        {"permit_service", false},
                                                        {"enabled_functions", {"one"}},
                                                        {"problems", {"bad curve"}}}
                                       : nlohmann::json{{"flavor", "AC_DER_IEC"},
                                                        {"source", "ChargeParameterDiscovery"},
                                                        {"dso_q_setpoint", -1200.5},
                                                        {"dso_cos_phi_setpoint", 0.95f}};
    REQUIRE(f.adapter.peers.count("der_negotiated_functions") == 1);
    REQUIRE(f.adapter.peers.count("der_control_received") == 1);
    f.adapter.peers.at("der_negotiated_functions")(negotiated);
    f.adapter.peers.at("der_control_received")(control);
    const std::string base = "everest_api/1/ev_controller/test_car/e2m/";
    auto observed = f.adapter.observed.handle();
    REQUIRE(observed.wait_for([&] { return observed->publications.count(base + "der_control") != 0; },
                              std::chrono::seconds(2)));
    auto expected_negotiated = negotiated;
    auto expected_control = control;
    expected_negotiated["flavor"] = sae ? "SAE" : "IEC";
    expected_control["flavor"] = sae ? "SAE" : "IEC";
    CHECK(observed->publications.at(base + "der_negotiated_functions") == expected_negotiated);
    CHECK(observed->publications.at(base + "der_control") == expected_control);
}

TEST_CASE("EvController rejected DER start publishes the internal diagnostic", "[evctl][der]") {
    TestFixture fx;
    fx.mocks.iso.next_start_charging_result = false;
    auto ctx = fx.make_ctx();
    api::AcIsoD20DerSaeSessionParams params{};
    ctx->configure_session(params);
    fsm::v2::FSM<StateBase> machine{std::make_unique<Unplugged>(*ctx)};
    machine.feed(pilot_input(::types::board_support_common::Event::A));
    machine.feed(fx.timer.enqueued_events.back());
    machine.feed(pwm(5));
    machine.feed(Event{SlacStatePayload{"MATCHED"}});
    REQUIRE(kind_of(fx.timer.enqueued_events.back()) == EventKind::InternalFault);
    const auto fault = std::get<InternalFault>(fx.timer.enqueued_events.back().payload);
    CHECK(fault.type == api::FaultType::Internal);
    CHECK(fault.message == "iso_start_charging rejected");
    machine.feed(fx.timer.enqueued_events.back());
    REQUIRE(machine.get_current_state_id() == api::FsmState::Faulted);
    const auto report = api::deserialize<api::FaultReport>(payload_for(fx.sink, fx.topics.everest_to_extern("fault")));
    CHECK(report.type == api::FaultType::Internal);
    CHECK(report.message == "iso_start_charging rejected");
}

TEST_CASE("EvController DER publications convert every ISO flavor", "[evctl][der]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    using IsoFlavor = ::types::iso15118::DerFlavor;
    for (const auto& [input, expected] : {std::pair{IsoFlavor::AC_DER_IEC, api::DerFlavor::IEC},
                                          std::pair{IsoFlavor::AC_DER_SAE, api::DerFlavor::SAE}}) {
        fx.sink.clear();
        ::types::iso15118::DerNegotiatedFunctions negotiated{};
        negotiated.flavor = input;
        ctx->publish_e2m_der_negotiated_functions(negotiated);
        CHECK(api::deserialize<api::DerNegotiatedFunctions>(
                  payload_for(fx.sink, fx.topics.everest_to_extern("der_negotiated_functions")))
                  .flavor == expected);
        ::types::iso15118::DerControlReceived control{};
        control.flavor = input;
        ctx->publish_e2m_der_control(control);
        CHECK(api::deserialize<api::DerControl>(payload_for(fx.sink, fx.topics.everest_to_extern("der_control")))
                  .flavor == expected);
    }
    ::types::iso15118::DerNegotiatedFunctions invalid{};
    invalid.flavor = static_cast<IsoFlavor>(-1);
    CHECK_THROWS_AS(ctx->publish_e2m_der_negotiated_functions(invalid), std::out_of_range);
}

TEST_CASE("EvController DER publications convert every ISO control source", "[evctl][der]") {
    TestFixture fx;
    auto ctx = fx.make_ctx();
    using IsoSource = ::types::iso15118::DerControlSource;
    for (const auto& [input, expected] :
         {std::pair{IsoSource::ChargeParameterDiscovery, api::DerControlSource::ChargeParameterDiscovery},
          std::pair{IsoSource::ChargeLoop, api::DerControlSource::ChargeLoop}}) {
        fx.sink.clear();
        ::types::iso15118::DerControlReceived control{};
        control.source = input;
        ctx->publish_e2m_der_control(control);
        CHECK(api::deserialize<api::DerControl>(payload_for(fx.sink, fx.topics.everest_to_extern("der_control")))
                  .source == expected);
    }
    ::types::iso15118::DerControlReceived invalid{};
    invalid.source = static_cast<IsoSource>(-1);
    CHECK_THROWS_AS(ctx->publish_e2m_der_control(invalid), std::out_of_range);
}

TEST_CASE("EvController identifies every AC charge mode", "[evctl][start-rules]") {
    CHECK(is_ac_mode(api::ChargeMode::AcIec));
    CHECK(is_ac_mode(api::ChargeMode::AcIso2));
    CHECK(is_ac_mode(api::ChargeMode::AcIsoD20));
    CHECK(is_ac_mode(api::ChargeMode::AcIsoD20DerIec));
    CHECK(is_ac_mode(api::ChargeMode::AcIsoD20DerSae));
    CHECK_FALSE(is_ac_mode(api::ChargeMode::DcIso2));
    CHECK_FALSE(is_ac_mode(api::ChargeMode::DcIsoD20));
}
