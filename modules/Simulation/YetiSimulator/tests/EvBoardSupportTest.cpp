// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_board_support/ev_board_supportImpl.hpp"
#include <ModuleAdapterStub.hpp>
#include <gtest/gtest.h>

namespace {

using types::ev_board_support::EvCpState;

class EvBoardSupport : public module::ev_board_support::ev_board_supportImpl {
public:
    using ev_board_supportImpl::ev_board_supportImpl;
    using ev_board_supportImpl::handle_set_cp_state;
};

struct Adapter : module::stub::QuietModuleAdapterStub {
    std::vector<types::board_support_common::Event> events;

    void publish_fn(const std::string&, const std::string& name, Value value) override {
        if (name == "bsp_event") {
            events.push_back(value.get<types::board_support_common::BspEvent>().event);
        }
    }
};

class EvBoardSupportTest : public testing::Test {
protected:
    Adapter adapter;
    Everest::MqttProvider mqtt{adapter};
    Everest::TelemetryProvider telemetry{adapter};
    module::Conf config{};
    module::YetiSimulator simulator{ModuleInfo{}, mqtt,    telemetry, nullptr, nullptr,
                                    nullptr,      nullptr, nullptr,   nullptr, config};
    Everest::PtrContainer<module::YetiSimulator> simulator_ptr;
    module::ev_board_support::Conf ev_config;
    EvBoardSupport ev_bsp{&adapter, simulator_ptr, ev_config};

    void SetUp() override {
        simulator_ptr.set(&simulator);
        simulator.reset_module_state();
    }

    void set_cp_state(EvCpState state) {
        ev_bsp.handle_set_cp_state(state);
    }
};

TEST_F(EvBoardSupportTest, UnplugDuringForcedFIsRemembered) {
    set_cp_state(EvCpState::B);
    adapter.events.clear();
    simulator.module_state->pwm_error_f = true;

    set_cp_state(EvCpState::A);

    // The EVSE must continue driving F, while the physical unplug is retained for
    // the simulation to observe after the EVSE releases F.
    EXPECT_TRUE(simulator.module_state->pwm_error_f);
    EXPECT_TRUE(adapter.events.empty());
    EXPECT_DOUBLE_EQ(simulator.module_state->simdata_setting.cp_voltage, 12.0);
}

TEST_F(EvBoardSupportTest, LatestEvStateDuringForcedFIsRemembered) {
    set_cp_state(EvCpState::B);
    adapter.events.clear();
    simulator.module_state->pwm_error_f = true;

    set_cp_state(EvCpState::C);
    EXPECT_TRUE(simulator.module_state->pwm_error_f);
    EXPECT_TRUE(adapter.events.empty());
    EXPECT_DOUBLE_EQ(simulator.module_state->simdata_setting.cp_voltage, 6.0);

    set_cp_state(EvCpState::B);
    EXPECT_TRUE(simulator.module_state->pwm_error_f);
    EXPECT_TRUE(adapter.events.empty());
    EXPECT_DOUBLE_EQ(simulator.module_state->simdata_setting.cp_voltage, 9.0);
}

} // namespace
