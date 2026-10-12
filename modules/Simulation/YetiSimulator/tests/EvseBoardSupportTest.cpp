// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "board_support/evse_board_supportImpl.hpp"
#include <ModuleAdapterStub.hpp>
#include <gtest/gtest.h>

#include <algorithm>

namespace {

using types::board_support_common::Event;

class BoardSupport : public module::board_support::evse_board_supportImpl {
public:
    using evse_board_supportImpl::evse_board_supportImpl;
    using evse_board_supportImpl::handle_allow_power_on;
};

struct Adapter : module::stub::QuietModuleAdapterStub {
    std::vector<Event> events;

    void publish_fn(const std::string& impl_id, const std::string& name, Value value) override {
        if (impl_id == "board_support" and name == "event") {
            events.push_back(value.get<types::board_support_common::BspEvent>().event);
        }
    }
};

class EvseBoardSupportTest : public testing::Test {
protected:
    Adapter adapter;
    Everest::MqttProvider mqtt{adapter};
    Everest::TelemetryProvider telemetry{adapter};
    module::Conf config{};
    Everest::PtrContainer<module::YetiSimulator> simulator_ptr;
    module::board_support::Conf bsp_config;
    BoardSupport* bsp{new BoardSupport{&adapter, simulator_ptr, bsp_config}};
    module::YetiSimulator simulator{
        ModuleInfo{}, mqtt,    telemetry, nullptr, nullptr, std::unique_ptr<evse_board_supportImplBase>{bsp},
        nullptr,      nullptr, nullptr,   config};

    void SetUp() override {
        simulator_ptr.set(&simulator);
        simulator.reset_module_state();
    }

    void allow_power_on(bool allow) {
        using types::evse_board_support::Reason;
        types::evse_board_support::PowerOnOff value{allow, allow ? Reason::FullPowerCharging : Reason::PowerOff};
        bsp->handle_allow_power_on(value);
    }

    void charge_in_state_c() {
        simulator.module_state->current_state = module::state::State::STATE_C;
        simulator.pwm_on(0.05);
        allow_power_on(true);
        simulator.simulation_statemachine();
    }

    long count(Event event) const {
        return std::count(adapter.events.begin(), adapter.events.end(), event);
    }
};

TEST_F(EvseBoardSupportTest, PowerOnAllowedInStateCWithPwmClosesRelay) {
    charge_in_state_c();

    EXPECT_TRUE(simulator.module_state->relais_on);
    EXPECT_EQ(count(Event::PowerOn), 1);
}

TEST_F(EvseBoardSupportTest, PowerOnWithdrawnInStateCWithPwmOpensRelay) {
    charge_in_state_c();
    adapter.events.clear();

    allow_power_on(false);
    simulator.simulation_statemachine();
    simulator.simulation_statemachine();

    EXPECT_FALSE(simulator.module_state->relais_on);
    EXPECT_EQ(count(Event::PowerOff), 1);
    EXPECT_EQ(count(Event::PowerOn), 0);
}

} // namespace
