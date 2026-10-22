// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generated/types/evse_manager.hpp"
#include <gtest/gtest.h>

#include <Charger.hpp>
#include <memory>
#include <optional>

namespace module {
std::vector<std::string> observed_cp_state_commands;
}

namespace {
using namespace module;
using namespace types::evse_manager;

// ----------------------------------------------------------------------------
// test classes

// class that provides access to internal state from the Charger class
struct ChargerDerived : public Charger {
    using Charger::Charger;
    using Charger::get_config_context;
    using Charger::get_enable_disable_source_table;
    using Charger::get_hlc_use_5percent_current_session;
    using Charger::get_internal_context;
    using Charger::get_shared_context;
    using Charger::process_event;
    using Charger::run_state_machine;

    // updated when a non-zero connector is used to enable_disable()
    constexpr const auto& connector_enabled() {
        return get_shared_context().connector_enabled;
    }

    constexpr const auto& current_state() {
        return get_shared_context().current_state;
    }

    constexpr void current_state(EvseState state) {
        get_shared_context().current_state = state;
    }

    constexpr const auto& flag_disable_requested() {
        return get_shared_context().flag_disable_requested;
    }
};

// class that creates a consistent starting state for tests
struct ChargerTest : public testing::Test {
    // charger requirements
    std::unique_ptr<IECStateMachine> charger_bsp;
    std::unique_ptr<ErrorHandling> charger_error_handling;
    std::vector<std::unique_ptr<powermeterIntf>> charger_powermeter_billing;
    std::unique_ptr<PersistentStore> charger_store;

    // error handling requirements
    std::unique_ptr<evse_board_supportIntf> error_handler_bsp;
    std::vector<std::unique_ptr<ISO15118_chargerIntf>> error_handler_hlc;
    std::vector<std::unique_ptr<connector_lockIntf>> error_handler_connector_lock;
    std::vector<std::unique_ptr<ac_rcdIntf>> error_handler_ac_rcd;
    std::unique_ptr<evse_managerImplBase> error_handler_evse;
    std::vector<std::unique_ptr<isolation_monitorIntf>> error_handler_imd;
    std::vector<std::unique_ptr<power_supply_DCIntf>> error_handler_powersupply;
    std::vector<std::unique_ptr<powermeterIntf>> error_handler_powermeter;
    std::vector<std::unique_ptr<slacIntf>> error_handler_slac;
    std::vector<std::unique_ptr<over_voltage_monitorIntf>> error_handler_over_voltage_monitor;

    std::unique_ptr<ChargerDerived> charger;

    ChargerTest() :
        charger_error_handling(std::make_unique<ErrorHandling>(
            error_handler_bsp, error_handler_hlc, error_handler_connector_lock, error_handler_ac_rcd,
            error_handler_evse, error_handler_imd, error_handler_powersupply, error_handler_powermeter,
            error_handler_slac, error_handler_over_voltage_monitor, false)) {
    }

    void SetUp() override {
        reset_last_event();
        observed_cp_state_commands.clear();
        charger = std::make_unique<ChargerDerived>(
            charger_bsp, charger_error_handling, charger_powermeter_billing, charger_store,
            types::evse_board_support::Connector_type::IEC62196Type2Socket, "EVSETEST");
        charger->signal_simple_event.connect(&ChargerTest::session_event, this);
    }

    void TearDown() override {
        charger.reset(nullptr);
    }

    void session_event(SessionEventEnum event) {
        last_event = event;
    }

    static constexpr SessionEventEnum default_event{SessionEventEnum::SessionFinished};
    static constexpr EnableDisableSource default_source{Enable_source::Unspecified, Enable_state::Unassigned, 10000};

    SessionEventEnum last_event{default_event};

    constexpr void reset_last_event() {
        last_event = default_event;
    }
};

// ----------------------------------------------------------------------------
// tests for enable_disable()
// interesting variables:
// - enable_disable_source_table        (not directly available)
// - shared_context.connector_enabled - charger->connector_enabled()
// - shared_context.current_state     - charger->current_state()
// - signal_simple_event              - last_event

TEST_F(ChargerTest, EnableDisableSourceInit) {
    // check the default values on startup
    // this is the starting point for all tests
    const auto last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);
    EXPECT_EQ(last_event, default_event);
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
}

TEST_F(ChargerTest, EnableDisableSourceInitPlusStateEnabled) {
    charger->current_state(Charger::EvseState::Idle);

    // check the default values on startup
    // this is the starting point for all tests
    auto last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);
    EXPECT_EQ(last_event, default_event);
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);

    reset_last_event();
    constexpr EnableDisableSource enable_default{Enable_source::Unspecified, Enable_state::Enable, 10000};
    charger->enable_disable_initial_state_publish();
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_default);
    EXPECT_EQ(last_event, SessionEventEnum::Enabled);
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);

    // already enabled so no event
    reset_last_event();
    constexpr EnableDisableSource enable_source{Enable_source::CSMS, Enable_state::Enable, 100};
    EXPECT_TRUE(charger->enable_disable(0, enable_source));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source);
    EXPECT_EQ(last_event, default_event);
}

TEST_F(ChargerTest, EnableDisableSourceInitPlusStateDisabled) {
    charger->current_state(Charger::EvseState::Disabled);

    // check the default values on startup
    // this is the starting point for all tests
    auto last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);
    EXPECT_EQ(last_event, default_event);
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);

    reset_last_event();
    constexpr EnableDisableSource disable_default{Enable_source::Unspecified, Enable_state::Disable, 10000};
    charger->enable_disable_initial_state_publish();
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_default);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);

    // already disabled so no event
    reset_last_event();
    constexpr EnableDisableSource disable_source{Enable_source::CSMS, Enable_state::Disable, 100};
    EXPECT_FALSE(charger->enable_disable(0, disable_source));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source);
    EXPECT_EQ(last_event, default_event);
}

TEST_F(ChargerTest, EnableDisableSourceConnectorEnabled0) {
    // connector_enabled must only change when a non-zero connector ID is used
    ASSERT_TRUE(charger->connector_enabled());

    constexpr EnableDisableSource enable_source{Enable_source::CSMS, Enable_state::Enable, 100};
    constexpr EnableDisableSource disable_source{Enable_source::CSMS, Enable_state::Disable, 100};

    // test with connector ID 0: connector_enabled must not change
    EXPECT_FALSE(charger->enable_disable(0, disable_source));
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
    EXPECT_TRUE(charger->enable_disable(0, enable_source));
    EXPECT_TRUE(charger->connector_enabled());
    // tricky case, connector_enabled is true but the change was to connector 0
    // this is what the original code does ...
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);

    // force connector_enabled false
    EXPECT_FALSE(charger->enable_disable(1, disable_source));
    EXPECT_FALSE(charger->connector_enabled());

    // test with connector ID 0: connector_enabled must not change
    EXPECT_FALSE(charger->enable_disable(0, disable_source));
    EXPECT_FALSE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
    EXPECT_TRUE(charger->enable_disable(0, enable_source));
    EXPECT_FALSE(charger->connector_enabled());
    // enable on connector 0 does not change state to Idle
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
}

TEST_F(ChargerTest, EnableDisableSourceConnectorEnabled1) {
    // connector_enabled must only change when a non-zero connector ID is used
    ASSERT_TRUE(charger->connector_enabled());

    constexpr EnableDisableSource enable_source{Enable_source::CSMS, Enable_state::Enable, 100};
    constexpr EnableDisableSource disable_source{Enable_source::CSMS, Enable_state::Disable, 100};

    // test with connector ID 1: connector_enabled must change
    EXPECT_FALSE(charger->enable_disable(1, disable_source));
    EXPECT_FALSE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
    EXPECT_TRUE(charger->enable_disable(1, enable_source));
    EXPECT_TRUE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_FALSE(charger->enable_disable(1, disable_source));
    EXPECT_FALSE(charger->connector_enabled());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
}

constexpr EnableDisableSource enable_source_10{Enable_source::CSMS, Enable_state::Enable, 10};
constexpr EnableDisableSource enable_source_100{Enable_source::CSMS, Enable_state::Enable, 100};
constexpr EnableDisableSource enable_source_1000{Enable_source::CSMS, Enable_state::Enable, 1000};
constexpr EnableDisableSource disable_source_10{Enable_source::CSMS, Enable_state::Disable, 10};
constexpr EnableDisableSource disable_source_100{Enable_source::CSMS, Enable_state::Disable, 100};
constexpr EnableDisableSource disable_source_1000{Enable_source::CSMS, Enable_state::Disable, 1000};
constexpr EnableDisableSource unassigned_source_10{Enable_source::CSMS, Enable_state::Unassigned, 10};
constexpr EnableDisableSource unassigned_source_100{Enable_source::CSMS, Enable_state::Unassigned, 100};
constexpr EnableDisableSource unassigned_source_1000{Enable_source::CSMS, Enable_state::Unassigned, 1000};

TEST_F(ChargerTest, EnableDisableTableSingleSource) {
    // enable_disable settings are added to a table
    // parse_enable_disable_source_table() processes the table and updates
    // active_enable_disable_source which is available via get_last_enable_disable_source()
    // enable_disable() updates the table and calls parse_enable_disable_source_table()

    // EnableDisableTable
    // Evaluation will be done based on priorities. 0 is the highest priority, 10000 the lowest
    // If all sources are unassigned, the connector is enabled
    // If two sources have the same priority, "disabled" has priority over "enabled"

    const int connector_id = 1; // use a consistent value

    const auto& enable_disable_source_table = charger->get_enable_disable_source_table();

    EXPECT_TRUE(enable_disable_source_table.empty());
    EXPECT_EQ(enable_disable_source_table.size(), 0);
    auto last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);

    // check source change of state

    EXPECT_TRUE(charger->enable_disable(connector_id, enable_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_100);

    EXPECT_FALSE(charger->enable_disable(connector_id, disable_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_100);

    EXPECT_TRUE(charger->enable_disable(connector_id, unassigned_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    // unexpected
    EXPECT_EQ(last_source, default_source);

    // check source change of priority

    EXPECT_TRUE(charger->enable_disable(connector_id, enable_source_1000));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_1000);

    EXPECT_TRUE(charger->enable_disable(connector_id, enable_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_100);

    EXPECT_TRUE(charger->enable_disable(connector_id, enable_source_10));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_FALSE(charger->enable_disable(connector_id, disable_source_1000));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_1000);

    EXPECT_FALSE(charger->enable_disable(connector_id, disable_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_100);

    EXPECT_FALSE(charger->enable_disable(connector_id, disable_source_10));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_10);

    // last_source is always the default since Unassigned
    // entries are ignored

    EXPECT_TRUE(charger->enable_disable(connector_id, unassigned_source_1000));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);

    EXPECT_TRUE(charger->enable_disable(connector_id, unassigned_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);

    EXPECT_TRUE(charger->enable_disable(connector_id, unassigned_source_10));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);
}

TEST_F(ChargerTest, EnableDisableTableSourceMulti) {
    // enable_disable settings are added to a table
    // parse_enable_disable_source_table() processes the table and updates
    // active_enable_disable_source which is available via get_last_enable_disable_source()
    // enable_disable() updates the table and calls parse_enable_disable_source_table()

    // EnableDisableTable
    // Evaluation will be done based on priorities. 0 is the highest priority, 10000 the lowest
    // If all sources are unassigned, the connector is enabled
    // If two sources have the same priority, "disabled" has priority over "enabled"

    const int connector_id = 1; // use a consistent value

    const auto& enable_disable_source_table = charger->get_enable_disable_source_table();

    EXPECT_TRUE(enable_disable_source_table.empty());
    EXPECT_EQ(enable_disable_source_table.size(), 0);
    auto last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);

    // check multiple sources are added to the table

    EXPECT_TRUE(charger->enable_disable(connector_id, enable_source_10));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_TRUE(charger->enable_disable(connector_id, {Enable_source::FirmwareUpdate, Enable_state::Enable, 11}));
    EXPECT_EQ(enable_disable_source_table.size(), 2);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_TRUE(charger->enable_disable(connector_id, {Enable_source::LocalAPI, Enable_state::Enable, 11}));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_TRUE(charger->enable_disable(connector_id, {Enable_source::LocalKeyLock, Enable_state::Enable, 11}));
    EXPECT_EQ(enable_disable_source_table.size(), 4);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_TRUE(charger->enable_disable(connector_id, {Enable_source::MobileApp, Enable_state::Enable, 11}));
    EXPECT_EQ(enable_disable_source_table.size(), 5);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_TRUE(charger->enable_disable(connector_id, {Enable_source::RemoteKeyLock, Enable_state::Enable, 11}));
    EXPECT_EQ(enable_disable_source_table.size(), 6);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    const EnableDisableSource disable_source{Enable_source::ServiceTechnician, Enable_state::Disable, 11};
    EXPECT_TRUE(charger->enable_disable(connector_id, disable_source));
    EXPECT_EQ(enable_disable_source_table.size(), 7);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    EXPECT_TRUE(charger->enable_disable(connector_id, {Enable_source::Unspecified, Enable_state::Enable, 11}));
    EXPECT_EQ(enable_disable_source_table.size(), 8);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, enable_source_10);

    // update CSMS - next highest expected - disable_source
    EXPECT_FALSE(charger->enable_disable(connector_id, enable_source_100));
    EXPECT_EQ(enable_disable_source_table.size(), 8);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source);
}

TEST_F(ChargerTest, EnableDisableTablePriority) {
    // enable_disable settings are added to a table
    // parse_enable_disable_source_table() processes the table and updates
    // active_enable_disable_source which is available via get_last_enable_disable_source()
    // enable_disable() updates the table and calls parse_enable_disable_source_table()

    // EnableDisableTable
    // Evaluation will be done based on priorities. 0 is the highest priority, 10000 the lowest
    // If all sources are unassigned, the connector is enabled
    // If two sources have the same priority, "disabled" has priority over "enabled"

    const int connector_id = 1; // use a consistent value

    const auto& enable_disable_source_table = charger->get_enable_disable_source_table();

    EXPECT_TRUE(enable_disable_source_table.empty());
    EXPECT_EQ(enable_disable_source_table.size(), 0);
    auto last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, default_source);

    // check priority is being respected (via different sources)
    // base priority is 10 - higher values must be ignored

    EXPECT_FALSE(charger->enable_disable(connector_id, disable_source_10));
    EXPECT_EQ(enable_disable_source_table.size(), 1);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_10);

    // ignored because disable has higher priority over enabled
    const EnableDisableSource next_expected{Enable_source::FirmwareUpdate, Enable_state::Enable, 10};
    EXPECT_FALSE(charger->enable_disable(connector_id, next_expected));
    EXPECT_EQ(enable_disable_source_table.size(), 2);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_10);

    // ignored because unassigned is ignored
    EXPECT_FALSE(charger->enable_disable(connector_id, {Enable_source::LocalAPI, Enable_state::Unassigned, 9}));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_10);

    // ignored because priority is lower
    EXPECT_FALSE(charger->enable_disable(connector_id, {Enable_source::LocalAPI, Enable_state::Disable, 12}));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_10);

    // ignored because priority is even lower
    EXPECT_FALSE(charger->enable_disable(connector_id, {Enable_source::LocalAPI, Enable_state::Enable, 200}));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, disable_source_10);

    // overrides CSMS so it is ignored - expected is
    EXPECT_TRUE(charger->enable_disable(connector_id, unassigned_source_10));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, next_expected);

    // disabled takes priority
    const EnableDisableSource last_expected{Enable_source::LocalAPI, Enable_state::Disable, 10};
    EXPECT_FALSE(charger->enable_disable(connector_id, last_expected));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, last_expected);

    // higher priority
    const EnableDisableSource higher_expected{Enable_source::FirmwareUpdate, Enable_state::Enable, 5};
    EXPECT_TRUE(charger->enable_disable(connector_id, higher_expected));
    EXPECT_EQ(enable_disable_source_table.size(), 3);
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, higher_expected);
}

TEST_F(ChargerTest, EnableDisableSourceEnable0) {
    constexpr EnableDisableSource sourceA{Enable_source::CSMS, Enable_state::Unassigned, 100};
    constexpr EnableDisableSource sourceB{Enable_source::FirmwareUpdate, Enable_state::Enable, 90};

    // enable from default state

    EXPECT_TRUE(charger->enable_disable(0, sourceA));
    auto last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, default_source);
    // There should not be an update event - default state is enabled
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_TRUE(charger->connector_enabled()); // default state

    reset_last_event();
    EXPECT_TRUE(charger->enable_disable(0, sourceB));
    last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, sourceB);
    // There should not be an update event - enabled -> enabled
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_TRUE(charger->connector_enabled()); // default state
}

TEST_F(ChargerTest, EnableDisableSourceEnable1A) {
    constexpr EnableDisableSource sourceA{Enable_source::CSMS, Enable_state::Disable, 100};
    constexpr EnableDisableSource sourceB{Enable_source::FirmwareUpdate, Enable_state::Enable, 90};

    // force a complete disable state
    EXPECT_FALSE(charger->enable_disable(1, sourceA));
    auto last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, sourceA);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
    EXPECT_FALSE(charger->connector_enabled());

    // enable on connector 1
    reset_last_event();
    EXPECT_TRUE(charger->enable_disable(1, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    EXPECT_EQ(last_event, SessionEventEnum::Enabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_TRUE(charger->connector_enabled());
}

TEST_F(ChargerTest, EnableDisableSourceEnable1B) {
    constexpr EnableDisableSource sourceA{Enable_source::CSMS, Enable_state::Disable, 100};
    constexpr EnableDisableSource sourceB{Enable_source::FirmwareUpdate, Enable_state::Enable, 90};

    // force a complete disable state
    EXPECT_FALSE(charger->enable_disable(1, sourceA));
    auto last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, sourceA);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
    EXPECT_FALSE(charger->connector_enabled());

    // enable on connector 0
    reset_last_event();
    EXPECT_TRUE(charger->enable_disable(0, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    EXPECT_EQ(last_event, SessionEventEnum::Enabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
    EXPECT_FALSE(charger->connector_enabled());

    // enable on connector 1
    reset_last_event();
    EXPECT_TRUE(charger->enable_disable(1, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    // enable -> enable so no event
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_TRUE(charger->connector_enabled());
}

TEST_F(ChargerTest, EnableDisableSourceDisable0) {
    constexpr EnableDisableSource sourceA{Enable_source::CSMS, Enable_state::Unassigned, 100};
    constexpr EnableDisableSource sourceB{Enable_source::FirmwareUpdate, Enable_state::Disable, 100};

    // disable from default state

    EXPECT_TRUE(charger->enable_disable(0, sourceA));
    auto last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, default_source);
    // This is possibly an error
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);

    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(0, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
}

TEST_F(ChargerTest, EnableDisableSourceDisable1) {
    constexpr EnableDisableSource sourceA{Enable_source::CSMS, Enable_state::Unassigned, 100};
    constexpr EnableDisableSource sourceB{Enable_source::FirmwareUpdate, Enable_state::Disable, 100};

    // disable from default state

    EXPECT_TRUE(charger->enable_disable(0, sourceA));
    auto last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, default_source);
    // This is possibly an error
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);

    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(0, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);

    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(1, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    // disable -> disable so no event
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);
}

TEST_F(ChargerTest, EnableDisableSourceDisableEnable) {
    constexpr EnableDisableSource sourceA{Enable_source::CSMS, Enable_state::Unassigned, 100};
    constexpr EnableDisableSource sourceB{Enable_source::FirmwareUpdate, Enable_state::Disable, 100};
    constexpr EnableDisableSource sourceC{Enable_source::LocalAPI, Enable_state::Enable, 90};

    // default state
    EXPECT_TRUE(charger->enable_disable(0, sourceA));
    auto last_source = charger->get_last_enable_disable_source();
    // Unassigned updates do not change the result from get_last_enable_disable_source()
    EXPECT_EQ(last_source, default_source);
    // This is possibly an error
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);

    // disable 0
    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(0, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);

    // disable 1
    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(1, sourceB));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceB);
    // no event: disable -> disable
    EXPECT_EQ(last_event, default_event);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);

    // enable 0
    reset_last_event();
    EXPECT_TRUE(charger->enable_disable(0, sourceC));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceC);
    EXPECT_EQ(last_event, SessionEventEnum::Enabled);
    // remains disabled because the enable was on connector 0
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Disabled);

    // enable 1
    reset_last_event();
    EXPECT_TRUE(charger->enable_disable(1, sourceC));
    last_source = charger->get_last_enable_disable_source();
    EXPECT_EQ(last_source, sourceC);
    // enable->enable hence no event
    EXPECT_EQ(last_event, default_event);
    // updated because not connector 0
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
}

// tests for cancel_transaction() / authorize() interaction
TEST_F(ChargerTest, DelayedAuthorizeAfterCancelTransactionIsIgnored) {
    auto& ctx = charger->get_shared_context();

    // Simulate an active charging session
    ctx.flag_transaction_active = true;
    ctx.session_active = true;
    ctx.flag_authorized = true;

    // External cancellation (e.g. OCPP RemoteStop)
    types::evse_manager::StopTransactionRequest stop_request;
    stop_request.reason = types::evse_manager::StopTransactionReason::Remote;
    EXPECT_TRUE(charger->cancel_transaction(stop_request));

    EXPECT_FALSE(ctx.flag_authorized);
    EXPECT_TRUE(ctx.flag_externally_cancelled);

    // Delayed authorization response arrives after the cancellation
    types::authorization::ProvidedIdToken token;
    token.id_token.value = "DELAYED_TOKEN";
    token.id_token.type = types::authorization::IdTokenType::Central;
    token.authorization_type = types::authorization::AuthorizationType::OCPP;

    types::authorization::ValidationResult validation_result;
    validation_result.authorization_status = types::authorization::AuthorizationStatus::Accepted;

    EXPECT_FALSE(charger->authorize(true, token, validation_result));

    // The delayed response must not restore authorization
    EXPECT_FALSE(ctx.flag_authorized);
    EXPECT_TRUE(ctx.flag_externally_cancelled);
}

TEST_F(ChargerTest, AuthorizeWhileDisableRequestedIsIgnored) {
    auto& ctx = charger->get_shared_context();
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    ctx.session_active = true;
    ctx.flag_ev_plugged_in = true;
    ctx.flag_disable_requested = true;

    types::authorization::ProvidedIdToken token;
    token.id_token.value = "TOKEN";
    token.id_token.type = types::authorization::IdTokenType::ISO14443;
    token.authorization_type = types::authorization::AuthorizationType::RFID;
    types::authorization::ValidationResult validation_result;
    validation_result.authorization_status = types::authorization::AuthorizationStatus::Accepted;

    EXPECT_FALSE(charger->authorize(true, token, validation_result));
    EXPECT_FALSE(ctx.flag_authorized);
}

TEST_F(ChargerTest, AuthorizeAppliesToken) {
    auto& ctx = charger->get_shared_context();
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    ctx.session_active = true;
    ctx.flag_ev_plugged_in = true;

    types::authorization::ProvidedIdToken token;
    token.id_token.value = "TOKEN";
    token.id_token.type = types::authorization::IdTokenType::ISO14443;
    token.authorization_type = types::authorization::AuthorizationType::RFID;
    types::authorization::ValidationResult validation_result;
    validation_result.authorization_status = types::authorization::AuthorizationStatus::Accepted;

    reset_last_event();
    EXPECT_TRUE(charger->authorize(true, token, validation_result));
    EXPECT_TRUE(ctx.flag_authorized);
    EXPECT_EQ(last_event, SessionEventEnum::Authorized);
}

// Test that disabling while a transaction is active goes through the proper
// StoppingCharging->Finished->Disabled sequence instead of jumping directly.
TEST_F(ChargerTest, DisableDuringActiveTransaction) {
    constexpr EnableDisableSource disable_source{Enable_source::CSMS, Enable_state::Disable, 100};

    auto& ctx = charger->get_shared_context();

    // Simulate an active charging session with contactors closed
    ctx.current_state = Charger::EvseState::Charging;
    ctx.flag_transaction_active = true;
    ctx.session_active = true;
    ctx.flag_authorized = true;
    ctx.contactor_open = false;

    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(1, disable_source));

    // enable_disable calls run_state_machine synchronously: Charging->StoppingCharging.
    // Must NOT immediately jump to Disabled — session needs proper teardown.
    EXPECT_EQ(ctx.current_state, Charger::EvseState::StoppingCharging);
    EXPECT_TRUE(charger->flag_disable_requested());
    EXPECT_EQ(ctx.last_stop_transaction_reason, StopTransactionReason::EVSEDisabled);

    // Simulate relay opening and transaction already stopped
    ctx.contactor_open = true;
    ctx.flag_transaction_active = false;

    // State machine: StoppingCharging->Finished->Disabled (all in one loop)
    reset_last_event();
    charger->run_state_machine();
    EXPECT_EQ(ctx.current_state, Charger::EvseState::Disabled);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
}

// Test that disabling while in WaitingForAuthentication (no transaction yet)
// ends the session and transitions to Disabled without going through StoppingCharging.
TEST_F(ChargerTest, DisableDuringWaitingForAuthentication) {
    constexpr EnableDisableSource disable_source{Enable_source::CSMS, Enable_state::Disable, 100};

    auto& ctx = charger->get_shared_context();

    // Simulate EV plugged in, waiting for auth — no transaction started yet
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    ctx.session_active = true;
    ctx.flag_ev_plugged_in = true;
    ctx.flag_transaction_active = false;
    ctx.flag_authorized = false;

    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(1, disable_source));

    // run_state_machine is called synchronously inside enable_disable, so by
    // the time enable_disable returns the state machine has already driven
    // WaitingForAuthentication -> Finished -> Disabled.
    EXPECT_EQ(ctx.current_state, Charger::EvseState::Disabled);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
}

// Test that disabling while in Idle (no session) immediately transitions to Disabled
TEST_F(ChargerTest, DisableDuringIdle) {
    constexpr EnableDisableSource disable_source{Enable_source::CSMS, Enable_state::Disable, 100};

    auto& ctx = charger->get_shared_context();

    // Simulate EV plugged in, no session active
    ctx.current_state = Charger::EvseState::Idle;
    ctx.session_active = false;
    ctx.flag_ev_plugged_in = true;
    ctx.flag_transaction_active = false;
    ctx.flag_authorized = false;

    reset_last_event();
    EXPECT_FALSE(charger->enable_disable(1, disable_source));

    // Must immediately transition to Disabled
    EXPECT_EQ(ctx.current_state, Charger::EvseState::Disabled);
    EXPECT_EQ(last_event, SessionEventEnum::Disabled);
}

// ----------------------------------------------------------------------------
// tests for dlink_error()
// A D-LINK_ERROR normally restarts SLAC matching according to the ISO 15118-3
// error recovery sequence ([V2G3-M07-05]): CP X1 at once; during the session
// setup the state machine then holds X1 until the node left the logical network
// and applies E/F for T_step_EF (T_step_X1 -> T_step_EF); in the charging loop
// or while stopping it starts the configured reinitialization instead.
// When the session is being stopped for good or is already finished, the
// D-LINK_ERROR is just the consequence of the HLC session shutting down and
// matching must NOT be restarted, so the session can end in
// StoppingCharging -> Finished.

struct ChargerDlinkErrorTest : public ChargerTest {
    // never dereferenced: the IECStateMachine used here is the no-op stub below
    std::unique_ptr<evse_board_supportIntf> bsp_if;

    void SetUp() override {
        charger_bsp = std::make_unique<IECStateMachine>(bsp_if, true, false, false, 0);
        ChargerTest::SetUp();
    }

    void setup_hlc_session_in(Charger::EvseState state) {
        auto& ctx = charger->get_shared_context();
        ctx.current_state = state;
        ctx.pwm_running = true;
        ctx.hlc_charging_active = true;
        ctx.flag_transaction_active = true;
        ctx.flag_authorized = true;
        charger->get_hlc_use_5percent_current_session() = true;
    }
};

TEST_F(ChargerDlinkErrorTest, NoMatchingRestartWhenStoppingDeauthorized) {
    // Transaction stopped on request during cable check: auth is withdrawn and the
    // charger is in StoppingCharging when the dying HLC session reports D-LINK_ERROR
    setup_hlc_session_in(Charger::EvseState::StoppingCharging);
    auto& ctx = charger->get_shared_context();
    ctx.flag_authorized = false;

    charger->dlink_error();

    // Matching must not be restarted; PWM is switched off so the state machine can
    // proceed to Finished
    EXPECT_EQ(charger->current_state(), Charger::EvseState::StoppingCharging);
    EXPECT_FALSE(ctx.pwm_running);
}

TEST_F(ChargerDlinkErrorTest, NoMatchingRestartWhenStoppingWithoutTransaction) {
    setup_hlc_session_in(Charger::EvseState::StoppingCharging);
    auto& ctx = charger->get_shared_context();
    ctx.flag_transaction_active = false;

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::StoppingCharging);
    EXPECT_FALSE(ctx.pwm_running);
}

TEST_F(ChargerDlinkErrorTest, NoMatchingRestartWhenStoppingForDisable) {
    setup_hlc_session_in(Charger::EvseState::StoppingCharging);
    auto& ctx = charger->get_shared_context();
    ctx.flag_disable_requested = true;

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::StoppingCharging);
    EXPECT_FALSE(ctx.pwm_running);
}

TEST_F(ChargerDlinkErrorTest, NoMatchingRestartWhenFinished) {
    // The EV may drop to state B quickly after a requested stop, in which case the
    // charger reaches Finished (with PWM still running) before the dying HLC session
    // reports D-LINK_ERROR. This must not resurrect the finished session.
    setup_hlc_session_in(Charger::EvseState::Finished);
    auto& ctx = charger->get_shared_context();
    ctx.flag_transaction_active = false;
    ctx.flag_authorized = false;

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::Finished);
    EXPECT_FALSE(ctx.pwm_running);
}

TEST_F(ChargerDlinkErrorTest, MatchingRestartDuringActiveSession) {
    // A D-LINK_ERROR during the session setup (e.g. a FAILED response of the HLC
    // stack with the transaction still running) switches CP to X1 at once and runs
    // the ISO 15118-3 error sequence: X1 until the node left the logical network,
    // then E/F for T_step_EF ([V2G3-M07-05]..[V2G3-M07-08]), back to
    // WaitingForAuthentication -- not the configured reinitialization, whose
    // duration is not bound to T_step_EF.
    setup_hlc_session_in(Charger::EvseState::PrepareCharging);
    charger->get_shared_context().flag_ev_plugged_in = true;
    int slac_resets{0};
    charger->signal_slac_reset.connect([&slac_resets] { ++slac_resets; });

    charger->dlink_error();

    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"X1"});
    EXPECT_FALSE(charger->get_shared_context().pwm_running);
    EXPECT_FALSE(charger->get_shared_context().reinit_requested);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    const auto& internal = charger->get_internal_context();
    EXPECT_TRUE(internal.t_step_X1_until_unmatched);
    EXPECT_EQ(internal.t_step_EF_return_state, Charger::EvseState::WaitingForAuthentication);
    EXPECT_EQ(internal.t_step_EF_return_pwm, 0.);

    charger->run_state_machine();

    // X1 is held for the node to leave the network (TP_match_leave) before E/F.
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    EXPECT_EQ(slac_resets, 1);
}

TEST_F(ChargerDlinkErrorTest, MatchingRestartWhileWaitingForAuthentication) {
    // Same sequence when the HLC session fails before the charger left WaitingForAuthentication.
    setup_hlc_session_in(Charger::EvseState::WaitingForAuthentication);

    charger->dlink_error();

    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"X1"});
    EXPECT_FALSE(charger->get_shared_context().reinit_requested);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    EXPECT_TRUE(charger->get_internal_context().t_step_X1_until_unmatched);
    EXPECT_EQ(charger->get_internal_context().t_step_EF_return_state, Charger::EvseState::WaitingForAuthentication);
}

TEST_F(ChargerDlinkErrorTest, MatchingRestartWhenStoppingToPause) {
    // StoppingCharging is also a transit state for EVSE-initiated pause: the session
    // continues afterwards, so the error recovery must still restart matching
    setup_hlc_session_in(Charger::EvseState::StoppingCharging);
    auto& ctx = charger->get_shared_context();
    ctx.flag_paused_by_evse = true;

    charger->dlink_error();

    // A pause continues the session: X1 at once, then the reinit sequence (the
    // energy transfer is stopped first; the T_step_X1/T_step_EF sequence is for
    // the session setup states).
    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"X1"});
    EXPECT_FALSE(ctx.pwm_running);
    EXPECT_TRUE(ctx.reinit_requested);
    EXPECT_EQ(charger->current_state(), Charger::EvseState::StoppingCharging);

    charger->run_state_machine();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::Reinit);
}

// ----------------------------------------------------------------------------
// An EVSE-initiated HLC pause that is lifted resumes in PrepareCharging and restarts SLAC. Not when the
// pause is lifted by the unplug itself: the CP event's facts (flag_ev_plugged_in) are applied before the
// state machine runs, so the pass sees the EV gone and stops instead. Bench-found on MCS, where the
// resume restarted the data link on an empty wire and spent its whole restart budget there.

struct ChargerEvsePauseTest : public ChargerDlinkErrorTest {
    int slac_starts{0};

    /// Settled in ChargingPausedEVSE on an EVSE-initiated pause that is still held by NoEnergy.
    void setup_held_evse_pause() {
        setup_hlc_session_in(Charger::EvseState::ChargingPausedEVSE);
        auto& ctx = charger->get_shared_context();
        ctx.flag_ev_plugged_in = true;
        ctx.hlc_session_paused_by_evse = true;
        ctx.hlc_charging_terminate_pause = Charger::HlcTerminatePause::Terminate;
        charger->signal_slac_start.connect([this] { ++slac_starts; });
        charger->run_state_machine();
        ASSERT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEVSE);
        slac_starts = 0;
    }

    /// Energy is back: the next pass resumes, unless the EV is gone.
    void lift_pause() {
        // The socket caps at the cable rating, which is unset in this fixture.
        charger->get_shared_context().max_current_cable = 32.0f;
        charger->set_max_current(16.0f, std::chrono::steady_clock::now() + std::chrono::hours(1));
    }
};

TEST_F(ChargerEvsePauseTest, LiftedPauseResumesAndRestartsSlacWhilePluggedIn) {
    setup_held_evse_pause();
    lift_pause();

    // PowerOff only records the open contactor; the pass itself does the resuming.
    charger->process_event(CPEvent::PowerOff);

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_GE(slac_starts, 1);
}

TEST_F(ChargerEvsePauseTest, UnplugWhilePausedStopsInsteadOfResuming) {
    setup_held_evse_pause();
    lift_pause();

    charger->process_event(CPEvent::CarUnplugged);

    EXPECT_FALSE(charger->get_shared_context().flag_ev_plugged_in);
    EXPECT_NE(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_EQ(slac_starts, 0) << "no SLAC restart for a car that left";
}

// ----------------------------------------------------------------------------
// EvseManager's option unmated_enable_imd starts the IMD when the Charger enters Idle. EvseManager stops the IMD
// itself on signal_dc_supply_off at the end of a session, so that signal has to come before Idle, or the stop
// would undo the start.

struct ChargerUnplugOrderTest : public ChargerDlinkErrorTest {
    std::vector<std::string> order;

    /// Record the order of the Charger's DC supply-off and Idle announcements in order.
    void SetUp() override {
        ChargerDlinkErrorTest::SetUp();
        // signal_dc_supply_off is only emitted in DC mode
        charger->get_config_context().charge_mode = Charger::ChargeMode::DC;
        charger->signal_dc_supply_off.connect([this] { order.push_back("dc_supply_off"); });
        charger->signal_state.connect([this](Charger::EvseState state) {
            if (state == Charger::EvseState::Idle) {
                order.push_back("idle");
            }
        });
    }

    /// Finished session with the car still plugged in: the Charger waits for the unplug.
    void setup_finished_session() {
        setup_hlc_session_in(Charger::EvseState::Finished);
        auto& ctx = charger->get_shared_context();
        ctx.flag_transaction_active = false;
        ctx.flag_authorized = false;
        ctx.flag_ev_plugged_in = true;
    }
};

TEST_F(ChargerUnplugOrderTest, DcSupplyOffComesBeforeIdleWhenUnpluggedAfterTheSession) {
    // The session is finished and the car is still plugged in; the unplug comes later.
    setup_finished_session();
    charger->run_state_machine();
    ASSERT_EQ(charger->current_state(), Charger::EvseState::Finished);

    charger->process_event(CPEvent::CarUnplugged);

    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_EQ(order, (std::vector<std::string>{"dc_supply_off", "idle"}));
}

TEST_F(ChargerUnplugOrderTest, DcSupplyOffComesBeforeIdleWhenUnpluggedInTheSamePass) {
    // The car is already unplugged when the Charger reaches Finished, so one pass ends in Idle.
    setup_finished_session();
    charger->get_shared_context().flag_ev_plugged_in = false;

    charger->run_state_machine();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::Idle);
    EXPECT_EQ(order, (std::vector<std::string>{"dc_supply_off", "idle"}));
}

// [V2G3-M07-27]: after a wake-up from an EV pause the PLC link has to be detected within T_conn_max_comm,
// else the EVSE applies E/F for T_step_EF and lets a new matching process start. The link lost during the
// sleep (reported by the matched-state link polling, dlink_established cleared) is the bench case:
// ISO 15118-5 PLCLinkStatus_003 / AC_004 re-key the tester's node while the SUT sleeps.
struct ChargerWakeupLinkTest : public ChargerDlinkErrorTest {
    /// Settled in ChargingPausedEV after an EV pause (PWM off), the link state as given.
    void setup_ev_pause(bool link_established) {
        setup_hlc_session_in(Charger::EvseState::ChargingPausedEV);
        auto& ctx = charger->get_shared_context();
        ctx.flag_ev_plugged_in = true;
        ctx.pwm_running = false;
        ctx.hlc_charging_terminate_pause = Charger::HlcTerminatePause::Pause;
        ctx.dlink_established = link_established;
    }

    /// The EV wakes the EVSE (the reconnect path arms the same watchdog as the BCB toggle).
    void wake_up() {
        charger->get_internal_context().hlc_session_restarted_by_ev = true;
        charger->run_state_machine();
        ASSERT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    }

    void expire_watchdog() {
        charger->get_internal_context().wakeup_link_deadline =
            std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
        charger->run_state_machine();
    }
};

TEST_F(ChargerWakeupLinkTest, NoLinkAfterWakeupRunsErrorSequenceWhenTConnMaxCommExpires) {
    setup_ev_pause(/*link_established=*/false);
    wake_up();
    ASSERT_TRUE(charger->get_internal_context().wakeup_link_deadline.has_value());

    // Still inside T_conn_max_comm: waiting for the link in PrepareCharging.
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);

    expire_watchdog();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
    EXPECT_FALSE(charger->get_internal_context().wakeup_link_deadline.has_value());
    EXPECT_EQ(charger->get_internal_context().t_step_EF_return_state, Charger::EvseState::PrepareCharging);
    EXPECT_GT(charger->get_internal_context().t_step_EF_return_pwm, 0.f) << "5 percent again after the E/F step";
}

TEST_F(ChargerWakeupLinkTest, LinkAfterWakeupDisarmsTheWatchdog) {
    setup_ev_pause(/*link_established=*/true);
    wake_up();

    // The link is there: PrepareCharging disarms the watchdog on its first pass and stays.
    EXPECT_FALSE(charger->get_internal_context().wakeup_link_deadline.has_value());
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
}

// [V2G3-M06-07]: with ac_limit_slac_init_retries (default), the 5% matching sequence (E/F, back to 5%) is
// retried C_sequ_retry = 2 times; the next timeout ends in X1 and later error routines are ignored.
TEST_F(ChargerDlinkErrorTest, SlacInitRetriesLimitedThenX1) {
    auto& ctx = charger->get_shared_context();
    charger->get_hlc_use_5percent_current_session() = true;
    int slac_resets{0};
    charger->signal_slac_reset.connect([&slac_resets] { ++slac_resets; });

    for (int retry = 0; retry < 2; retry++) {
        ctx.current_state = Charger::EvseState::WaitingForAuthentication;
        charger->request_error_sequence();
        EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
        EXPECT_TRUE(charger->get_hlc_use_5percent_current_session()) << "retry " << retry << " returns to 5%";
        EXPECT_EQ(slac_resets, retry + 1) << "every E/F resets SLAC";
    }

    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->request_error_sequence();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session()) << "last timeout returns to X1";
    EXPECT_EQ(slac_resets, 3);

    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->request_error_sequence();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::WaitingForAuthentication);
    EXPECT_EQ(slac_resets, 3) << "no SLAC reset once the retries are exhausted";
}

TEST_F(ChargerDlinkErrorTest, NoFivePercentAfterLastSlacRetry) {
    // The final T_step_EF returns to WaitingForAuthentication, whose initialization must keep 5% off
    auto& ctx = charger->get_shared_context();
    auto& cfg = charger->get_config_context();
    cfg.ac_hlc_enabled = true;
    cfg.ac_hlc_use_5percent = true;
    cfg.sleep_before_enabling_pwm_hlc_mode_ms = 0;
    ctx.flag_ev_plugged_in = true;
    charger->get_hlc_use_5percent_current_session() = true;

    for (int timeout = 0; timeout < 3; timeout++) {
        ctx.current_state = Charger::EvseState::WaitingForAuthentication;
        charger->request_error_sequence();
    }
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->run_state_machine();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::WaitingForAuthentication);
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerDlinkErrorTest, FivePercentAfterEarlierSlacRetry) {
    auto& ctx = charger->get_shared_context();
    auto& cfg = charger->get_config_context();
    cfg.ac_hlc_enabled = true;
    cfg.ac_hlc_use_5percent = true;
    cfg.sleep_before_enabling_pwm_hlc_mode_ms = 0;
    ctx.flag_ev_plugged_in = true;
    charger->get_hlc_use_5percent_current_session() = true;

    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->request_error_sequence();
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->run_state_machine();

    EXPECT_TRUE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerDlinkErrorTest, NoSlacRetryWithNominalPwm) {
    // [V2G3-A09-13]: with nominal PWM the EVSE considers that no SLAC will be performed. SLAC is not reset
    // either: it is not relaunched before the next plug-in.
    auto& ctx = charger->get_shared_context();
    charger->get_hlc_use_5percent_current_session() = false;
    ctx.current_state = Charger::EvseState::PrepareCharging;
    ctx.pwm_running = true;
    int slac_resets{0};
    charger->signal_slac_reset.connect([&slac_resets] { ++slac_resets; });

    charger->request_error_sequence();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_TRUE(ctx.pwm_running) << "the PWM stays as it is";
    EXPECT_EQ(slac_resets, 0);
}

// A PnC authorization in a 5% session whose EV never starts the charging loop: the end of the data link switches
// to nominal PWM for basic charging with the authorization kept (ISO 15118-3 6.4.2.1, 7.5.2), instead of a return to
// WaitingForAuthentication.
struct ChargerPncWithoutChargingLoopTest : public ChargerDlinkErrorTest {
    void setup_pnc_authorized_waiting_for_charging_loop() {
        auto& ctx = charger->get_shared_context();
        auto& cfg = charger->get_config_context();
        cfg.charge_mode = Charger::ChargeMode::AC;
        cfg.ac_hlc_enabled = true;
        cfg.ac_hlc_use_5percent = true;
        setup_hlc_session_in(Charger::EvseState::WaitingForAuthentication);
        ctx.authorized_pnc = true;
        ctx.hlc_charging_active = false;
        ctx.flag_ev_plugged_in = true;
    }
};

TEST_F(ChargerPncWithoutChargingLoopTest, DlinkErrorFallsBackToNominalThroughX1AndEF) {
    setup_pnc_authorized_waiting_for_charging_loop();
    auto& ctx = charger->get_shared_context();
    auto& ictx = charger->get_internal_context();

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    EXPECT_TRUE(ictx.t_step_X1_until_unmatched) << "[V2G3-M07-05]: X1 first, E/F once unmatched";
    EXPECT_EQ(ictx.t_step_EF_return_state, Charger::EvseState::PrepareCharging);
    EXPECT_EQ(ictx.t_step_EF_return_pwm, 0.) << "nominal PWM is enabled by PrepareCharging";
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
    EXPECT_TRUE(ctx.flag_authorized) << "the PnC authorization is kept";
    EXPECT_TRUE(ctx.flag_transaction_active);
    EXPECT_FALSE(ctx.reinit_requested) << "SLAC is reset in X1 [V2G3-M07-06], not by the reinit sequence";
}

TEST_F(ChargerPncWithoutChargingLoopTest, TerminateFallsBackToNominalThroughEF) {
    setup_pnc_authorized_waiting_for_charging_loop();
    auto& ctx = charger->get_shared_context();
    auto& ictx = charger->get_internal_context();
    ctx.hlc_charging_terminate_pause = Charger::HlcTerminatePause::Terminate;

    EXPECT_FALSE(charger->dlink_terminate()) << "a clean terminate, not a link loss";

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
    EXPECT_EQ(ictx.t_step_EF_return_state, Charger::EvseState::PrepareCharging);
    EXPECT_EQ(ictx.t_step_EF_return_pwm, 0.) << "nominal PWM is enabled by PrepareCharging";
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
    EXPECT_TRUE(ctx.flag_authorized) << "the PnC authorization is kept";
    EXPECT_FALSE(ctx.reinit_requested);
}

// Outside the PnC fallback, a D-LINK_ERROR on 5 percent during session setup runs the generic -3 sequence and returns
// to WaitingForAuthentication: X2 -> X1 -> E/F -> X1/X2 [V2G3-M07-05], Unmatched within X1 [V2G3-M07-06], E/F once
// Unmatched [V2G3-M07-07] for at least T_step_EF [V2G3-M07-08], then X1/X2 ready for matching [V2G3-M07-09].
TEST_F(ChargerPncWithoutChargingLoopTest, DlinkErrorAfterChargingLoopRunsGenericX1AndEF) {
    setup_pnc_authorized_waiting_for_charging_loop();
    auto& ctx = charger->get_shared_context();
    auto& ictx = charger->get_internal_context();
    ctx.current_state = Charger::EvseState::PrepareCharging;
    ctx.hlc_charging_active = true;

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1) << "[V2G3-M07-05]";
    EXPECT_TRUE(ictx.t_step_X1_until_unmatched) << "[V2G3-M07-06], [V2G3-M07-07]";
    EXPECT_EQ(ictx.t_step_EF_return_state, Charger::EvseState::WaitingForAuthentication)
        << "the charging loop started: no PnC fallback to PrepareCharging";
    EXPECT_FALSE(ctx.reinit_requested) << "the reinit sequence skips Unmatched and T_step_EF";
}

TEST_F(ChargerPncWithoutChargingLoopTest, EimDlinkErrorRunsGenericX1AndEF) {
    setup_pnc_authorized_waiting_for_charging_loop();
    auto& ctx = charger->get_shared_context();
    auto& ictx = charger->get_internal_context();
    ctx.authorized_pnc = false;

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1) << "[V2G3-M07-05]";
    EXPECT_TRUE(ictx.t_step_X1_until_unmatched) << "[V2G3-M07-06], [V2G3-M07-07]";
    EXPECT_EQ(ictx.t_step_EF_return_state, Charger::EvseState::WaitingForAuthentication)
        << "EIM: no PnC fallback to PrepareCharging";
    EXPECT_FALSE(ctx.reinit_requested) << "the reinit sequence skips Unmatched and T_step_EF";
}

// A PnC authorization in WaitingForAuthentication before the EV started the charging loop
struct ChargerPncAuthorizationTest : public ChargerDlinkErrorTest {
    void setup_plugged_in(bool use_5percent) {
        auto& ctx = charger->get_shared_context();
        auto& cfg = charger->get_config_context();
        cfg.charge_mode = Charger::ChargeMode::AC;
        cfg.ac_hlc_enabled = true;
        cfg.ac_hlc_use_5percent = use_5percent;
        cfg.sleep_before_enabling_pwm_hlc_mode_ms = 0;
        ctx.flag_ev_plugged_in = true;
        ctx.max_current_cable = 32.0f;
        ctx.session_active = true;
        ctx.flag_authorized = false;
        ctx.flag_transaction_active = true;
        ctx.hlc_charging_active = false;
    }

    void authorize_pnc() {
        auto& ctx = charger->get_shared_context();
        ctx.current_state = Charger::EvseState::WaitingForAuthentication;
        charger->run_state_machine();
        ctx.flag_authorized = true;
        ctx.authorized_pnc = true;
        charger->run_state_machine();
    }
};

TEST_F(ChargerPncAuthorizationTest, FivePercentKeptUntilDataLinkEnds) {
    setup_plugged_in(true);

    authorize_pnc();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::WaitingForAuthentication);
    EXPECT_TRUE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerPncAuthorizationTest, X1WithoutDataLinkGoesToNominalPwm) {
    setup_plugged_in(false);

    authorize_pnc();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerPncAuthorizationTest, X1StartKeptUntilDataLinkEnds) {
    // Figures 4 and 6 of ISO15118-3, branch a): X1 until V2G setup finished
    setup_plugged_in(false);
    charger->set_dlink_ready(true);

    authorize_pnc();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::WaitingForAuthentication);
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerPncAuthorizationTest, X1StartDlinkErrorFallsBackToNominalThroughEF) {
    setup_plugged_in(false);
    charger->set_dlink_ready(true);
    authorize_pnc();
    auto& ictx = charger->get_internal_context();

    charger->dlink_error();

    // [V2G3-M07-04]: X1 -> E/F -> X1/X2
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    EXPECT_TRUE(ictx.t_step_X1_until_unmatched);
    EXPECT_EQ(ictx.t_step_EF_return_state, Charger::EvseState::PrepareCharging);
    EXPECT_EQ(ictx.t_step_EF_return_pwm, 0.);
    EXPECT_TRUE(charger->get_shared_context().flag_authorized);
    EXPECT_FALSE(charger->get_shared_context().reinit_requested);
}

TEST_F(ChargerPncAuthorizationTest, X1StartTerminateFallsBackToNominal) {
    setup_plugged_in(false);
    charger->set_dlink_ready(true);
    authorize_pnc();

    EXPECT_FALSE(charger->dlink_terminate());

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
    EXPECT_EQ(charger->get_internal_context().t_step_EF_return_state, Charger::EvseState::PrepareCharging);
    EXPECT_TRUE(charger->get_shared_context().flag_authorized);
}

TEST_F(ChargerPncAuthorizationTest, NominalPwmAfterLastSlacRetry) {
    // The last matching retry ends 5 percent for the session ([V2G3-M06-07]): no data link end follows
    setup_plugged_in(true);
    charger->get_hlc_use_5percent_current_session() = true;
    auto& ctx = charger->get_shared_context();
    for (int timeout = 0; timeout < 3; timeout++) {
        ctx.current_state = Charger::EvseState::WaitingForAuthentication;
        charger->request_error_sequence();
    }

    authorize_pnc();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
}

// ac_enforce_hlc keeps 5% on after an EIM, unless the matching retries are exhausted before any HLC session was
// set up: then the session ends in nominal PWM like without the option
struct ChargerEnforceHlcTest : public ChargerDlinkErrorTest {
    void setup_eim_in_waiting_for_authentication() {
        auto& ctx = charger->get_shared_context();
        auto& cfg = charger->get_config_context();
        cfg.ac_hlc_enabled = true;
        cfg.ac_hlc_use_5percent = true;
        cfg.ac_enforce_hlc = true;
        cfg.sleep_before_enabling_pwm_hlc_mode_ms = 0;
        ctx.flag_ev_plugged_in = true;
        ctx.max_current_cable = 32.0f;
        // The initialization starts a session, which clears the authorization, unless one is active
        ctx.session_active = true;
        ctx.flag_authorized = true;
        ctx.authorized_pnc = false;
        ctx.flag_transaction_active = true;
        charger->get_hlc_use_5percent_current_session() = true;
    }

    void slac_init_timeouts(int count) {
        auto& ctx = charger->get_shared_context();
        for (int timeout = 0; timeout < count; timeout++) {
            ctx.current_state = Charger::EvseState::WaitingForAuthentication;
            charger->request_error_sequence();
        }
    }

    void authorize() {
        charger->get_shared_context().current_state = Charger::EvseState::WaitingForAuthentication;
        charger->run_state_machine();
    }
};

TEST_F(ChargerEnforceHlcTest, FivePercentKeptAfterEim) {
    setup_eim_in_waiting_for_authentication();
    slac_init_timeouts(1);

    authorize();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_TRUE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerEnforceHlcTest, NominalPwmAfterLastSlacRetry) {
    setup_eim_in_waiting_for_authentication();
    slac_init_timeouts(3);

    authorize();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_FALSE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerEnforceHlcTest, FivePercentKeptAfterEimWhenMatched) {
    // A match resets the count, so a later EIM in a session that matched keeps 5%
    setup_eim_in_waiting_for_authentication();
    slac_init_timeouts(2);
    charger->set_slac_matched(true);
    charger->set_slac_matched(false);
    slac_init_timeouts(1);

    authorize();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_TRUE(charger->get_hlc_use_5percent_current_session());
}

TEST_F(ChargerDlinkErrorTest, SlacRetryWithNominalPwmWhenNotLimited) {
    auto& ctx = charger->get_shared_context();
    charger->get_config_context().ac_limit_slac_init_retries = false;
    charger->get_hlc_use_5percent_current_session() = false;
    ctx.current_state = Charger::EvseState::PrepareCharging;

    charger->request_error_sequence();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
}

TEST_F(ChargerDlinkErrorTest, SlacInitRetriesUnlimitedWhenNotLimited) {
    auto& ctx = charger->get_shared_context();
    charger->get_config_context().ac_limit_slac_init_retries = false;
    charger->get_hlc_use_5percent_current_session() = true;

    for (int retry = 0; retry < 5; retry++) {
        ctx.current_state = Charger::EvseState::WaitingForAuthentication;
        charger->request_error_sequence();
        EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
        EXPECT_TRUE(charger->get_hlc_use_5percent_current_session());
    }
}

TEST_F(ChargerDlinkErrorTest, SlacMatchResetsRetryCount) {
    auto& ctx = charger->get_shared_context();
    charger->get_hlc_use_5percent_current_session() = true;

    for (int retry = 0; retry < 4; retry++) {
        ctx.current_state = Charger::EvseState::WaitingForAuthentication;
        charger->request_error_sequence();
        charger->set_slac_matched(true);
        charger->set_slac_matched(false);
        EXPECT_TRUE(charger->get_hlc_use_5percent_current_session());
    }
}

TEST_F(ChargerDlinkErrorTest, NoMatchingRestartWithNominalPwm) {
    // [V2G3-M07-12]: in nominal PWM mode (AC with HLC on nominal duty cycle), basic
    // charging continues and matching is not restarted on a D-LINK_ERROR
    setup_hlc_session_in(Charger::EvseState::Charging);
    charger->get_hlc_use_5percent_current_session() = false;

    charger->dlink_error();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::Charging);
}

// request_error_sequence() is the SLAC module's error routine. Without an established data link, e.g. after
// TT_EVSE_SLAC_init expired, the EVSE goes to E/F directly ([V2G3-M06-07]). After a lost data link it goes through X1
// first and leaves the network there ([V2G3-M07-05..07]).
TEST_F(ChargerDlinkErrorTest, ErrorRoutineWithoutDataLinkGoesToStepEF) {
    auto& ctx = charger->get_shared_context();
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->get_hlc_use_5percent_current_session() = true;

    charger->request_error_sequence();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
}

TEST_F(ChargerDlinkErrorTest, ErrorRoutineAfterLostDataLinkGoesThroughX1) {
    auto& ctx = charger->get_shared_context();
    ctx.current_state = Charger::EvseState::WaitingForAuthentication;
    charger->get_hlc_use_5percent_current_session() = true;
    charger->set_dlink_ready(true);
    charger->set_dlink_ready(false);

    charger->request_error_sequence();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    EXPECT_FALSE(ctx.dlink_established);
}

TEST_F(ChargerDlinkErrorTest, ErrorRoutineAfterHlcDlinkErrorGoesToStepEF) {
    // The HLC D-LINK_ERROR runs the X1 / E/F sequence itself: an error routine arriving during it changes
    // nothing, one after it is a new matching failure without a data link and goes to E/F directly.
    setup_hlc_session_in(Charger::EvseState::WaitingForAuthentication);
    charger->set_dlink_ready(true);

    charger->dlink_error();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);
    charger->request_error_sequence();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);

    charger->current_state(Charger::EvseState::WaitingForAuthentication); // the sequence ended
    charger->request_error_sequence();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
}

// A link lost during an EV pause: the error routine meets a state it does not act in and no HLC session is left to
// report the loss. The matching failure on the resume is a new one and goes to E/F directly.
TEST_F(ChargerDlinkErrorTest, ErrorRoutineIgnoredDuringPauseStillClearsDataLink) {
    auto& ctx = charger->get_shared_context();
    ctx.current_state = Charger::EvseState::ChargingPausedEV;
    charger->get_hlc_use_5percent_current_session() = true;
    charger->set_dlink_ready(true);

    charger->request_error_sequence();
    ASSERT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEV);

    ctx.current_state = Charger::EvseState::PrepareCharging;
    charger->request_error_sequence();

    EXPECT_EQ(charger->current_state(), Charger::EvseState::T_step_EF);
}

// The HLC reports the lost link while the EVSE is still in the X1 of the error sequence; the reinit it requests
// replaces T_step_X1 without running its exit. The next ordinary T_step_X1 must not inherit the link-loss handling.
TEST_F(ChargerDlinkErrorTest, LinkLossX1DoesNotOutliveReinit) {
    setup_hlc_session_in(Charger::EvseState::WaitingForAuthentication);
    charger->set_dlink_ready(true);
    charger->request_error_sequence();
    ASSERT_EQ(charger->current_state(), Charger::EvseState::T_step_X1);

    charger->dlink_error();
    charger->run_state_machine();
    ASSERT_EQ(charger->current_state(), Charger::EvseState::Reinit);

    int slac_resets = 0;
    charger->signal_slac_reset.connect([&slac_resets] { ++slac_resets; });
    charger->current_state(Charger::EvseState::T_step_X1);
    charger->run_state_machine();

    EXPECT_EQ(slac_resets, 0);
}

} // namespace

// ----------------------------------------------------------------------------
// the following code is stubs to enable testing Charger in isolation.
// If these stubs are needed elsewhere then they could go into separate files

// ----------------------------------------------------------------------------
// backtrace stub
namespace Everest {
void signal_handler(int signo) {
}
void install_backtrace_handler() {
}
void request_backtrace(pthread_t id) {
}
} // namespace Everest

namespace module {

// ----------------------------------------------------------------------------
// Minimum time in X1 before PWM is enabled again (basic AC charging)
// IEC 61851-1:2017 Table A.6 sequence 9.2: the EVSE may stop the PWM at any
// time, but if it enables it again (sequence 3.1) it shall have waited at
// least 3 s in X1. Energy management toggling around the 6 A minimum must not
// make the PWM flicker faster than that.

struct ChargerMinX1Test : public ChargerTest {
    // never dereferenced: the IECStateMachine used here is the no-op stub below
    std::unique_ptr<evse_board_supportIntf> bsp_if;

    void SetUp() override {
        charger_bsp = std::make_unique<IECStateMachine>(bsp_if, true, false, false, 0);
        ChargerTest::SetUp();
        // basic AC session, EV plugged in and authorized, 32 A cable
        auto& ctx = charger->get_shared_context();
        ctx.flag_transaction_active = true;
        ctx.flag_authorized = true;
        ctx.flag_ev_plugged_in = true;
        ctx.max_current_cable = 32.;
        ctx.hlc_charging_active = false;
        set_power(16.);
    }

    void set_power(float ampere) {
        charger->set_max_current(ampere, std::chrono::steady_clock::now() + std::chrono::hours(1));
    }

    // pretend the PWM has been off for the given time
    void pwm_off_since(std::chrono::milliseconds ago) {
        charger->get_internal_context().pwm_switched_off_at = std::chrono::steady_clock::now() - ago;
    }

    bool pwm_on() {
        auto& ctx = charger->get_shared_context();
        return ctx.pwm_running and charger->get_internal_context().update_pwm_last_duty_cycle < 1.;
    }
};

TEST_F(ChargerMinX1Test, ResumeFromPausedEvseWaitsInX1) {
    // Charging with PWM on; the energy manager drops below 6 A: pause by EVSE
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = true;
    charger->get_internal_context().update_pwm_last_duty_cycle = 0.1;
    set_power(0.);
    charger->current_state(Charger::EvseState::ChargingPausedEVSE);
    charger->run_state_machine();
    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"X1"});
    EXPECT_FALSE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEVSE);

    // Energy is back right away: still no PWM, we have to stay in X1 for 3 s
    set_power(16.);
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEVSE);
    EXPECT_FALSE(pwm_on());

    // Just before the 3 s are over: still waiting
    pwm_off_since(std::chrono::milliseconds(2900));
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEVSE);
    EXPECT_FALSE(pwm_on());

    // 3 s in X1: resume, PrepareCharging enables the PWM
    pwm_off_since(std::chrono::milliseconds(3000));
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_TRUE(pwm_on());
}

TEST_F(ChargerMinX1Test, ResumeFromPausedEvseIsImmediateWhenPwmWasOffLongEnough) {
    // PWM already off for a long time (e.g. pause started minutes ago)
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = false;
    pwm_off_since(std::chrono::minutes(5));
    charger->current_state(Charger::EvseState::ChargingPausedEVSE);
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
    EXPECT_TRUE(pwm_on());
}

TEST_F(ChargerMinX1Test, RepeatedX1DoesNotRestartTheTimer) {
    // The pause switches to X1 once; a later X1 while PWM is already off (as StoppingCharging does on entry)
    // must not restart the 3 s
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = true;
    charger->get_internal_context().update_pwm_last_duty_cycle = 0.1;
    set_power(0.);
    charger->current_state(Charger::EvseState::ChargingPausedEVSE);
    charger->run_state_machine();
    ASSERT_TRUE(charger->get_internal_context().pwm_switched_off_at.has_value());
    EXPECT_FALSE(pwm_on());

    pwm_off_since(std::chrono::milliseconds(2000));
    const auto backdated = charger->get_internal_context().pwm_switched_off_at.value();
    charger->current_state(Charger::EvseState::StoppingCharging);
    charger->run_state_machine();
    EXPECT_GE(observed_cp_state_commands.size(), 2u);
    EXPECT_EQ(charger->get_internal_context().pwm_switched_off_at.value(), backdated);
}

TEST_F(ChargerMinX1Test, PrepareChargingPowerToggleWaitsInX1) {
    // EV in state B waiting in PrepareCharging, PWM on with 16 A
    auto& ctx = charger->get_shared_context();
    charger->current_state(Charger::EvseState::PrepareCharging);
    charger->run_state_machine();
    ASSERT_TRUE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);

    // Energy drops below 6 A: duty cycle goes to 100 % (X1) immediately, that is allowed
    set_power(0.);
    charger->run_state_machine();
    EXPECT_FALSE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);

    // Energy is back 1 s later: PWM must stay off
    pwm_off_since(std::chrono::milliseconds(1000));
    set_power(16.);
    charger->run_state_machine();
    EXPECT_FALSE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);

    // After 3 s in X1 the PWM comes back
    pwm_off_since(std::chrono::milliseconds(3000));
    charger->run_state_machine();
    EXPECT_TRUE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
}

TEST_F(ChargerMinX1Test, ReplugAfterUnplugEnablesPwmImmediately) {
    // Charging with PWM on; the EV is unplugged: Idle switches to X1
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = true;
    charger->get_internal_context().update_pwm_last_duty_cycle = 0.27;
    ctx.flag_ev_plugged_in = false;
    charger->get_internal_context().last_state_detect_state_change = Charger::EvseState::Finished;
    charger->current_state(Charger::EvseState::Idle);
    charger->run_state_machine();
    ASSERT_FALSE(pwm_on());
    EXPECT_FALSE(charger->get_internal_context().pwm_switched_off_at.has_value());

    // Replug within 3 s: the new session enables PWM at once, seq. 9.2 does not apply after state A
    ctx.flag_ev_plugged_in = true;
    ctx.max_current_cable = 32.;
    ctx.flag_authorized = true;
    ctx.flag_transaction_active = true;
    charger->current_state(Charger::EvseState::PrepareCharging);
    charger->run_state_machine();
    EXPECT_TRUE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::PrepareCharging);
}

TEST_F(ChargerMinX1Test, EvPauseKeepsPwmOffForThreeSeconds) {
    // Charging with PWM on and the relays closed
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = true;
    charger->get_internal_context().update_pwm_last_duty_cycle = 0.27;
    ctx.contactor_open = false;
    ctx.iec_allow_close_contactor = true;
    charger->current_state(Charger::EvseState::Charging);
    charger->run_state_machine();
    ASSERT_EQ(charger->current_state(), Charger::EvseState::Charging);
    ASSERT_TRUE(pwm_on());
    // charging has been running for a while, the last duty cycle update is long ago
    charger->get_internal_context().last_pwm_update = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    // EV goes C->B (CPEvent::CarRequestedStopPower): stop via StoppingCharging, which switches to X1
    ctx.iec_allow_close_contactor = false;
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::StoppingCharging);
    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"X1"});
    EXPECT_FALSE(pwm_on());

    // Relays are open (CPEvent::PowerOff): the pause was initiated by the EV
    ctx.contactor_open = true;
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEV);
    EXPECT_FALSE(pwm_on());

    // The next tick must not enable the PWM again: the 5 s update throttle only knows the last duty cycle update
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEV);
    EXPECT_FALSE(pwm_on());

    // Just before the 3 s are over: still waiting
    pwm_off_since(std::chrono::milliseconds(2900));
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEV);
    EXPECT_FALSE(pwm_on());

    // 3 s in X1: nominal PWM comes back so the EV can resume
    pwm_off_since(std::chrono::milliseconds(3000));
    charger->run_state_machine();
    EXPECT_EQ(charger->current_state(), Charger::EvseState::ChargingPausedEV);
    EXPECT_TRUE(pwm_on());
}

// CP states E and F switch the PWM off as well. cp_state_E()/cp_state_F() are private, so they are reached through
// the Reinit state, whose configured method is in shared_context. Reinit ends with cp_state_X1(), which must not
// restart the timer: the time in E/F counts towards the 3 s.
TEST_F(ChargerMinX1Test, CpStateFArmsTheTimer) {
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = true;
    charger->get_internal_context().update_pwm_last_duty_cycle = 0.27;
    ctx.reinit_config = Charger::ReinitConfiguration{"CPStateF", 3000};
    charger->current_state(Charger::EvseState::Reinit);
    charger->run_state_machine();
    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"F"});
    EXPECT_FALSE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Reinit);
    ASSERT_TRUE(charger->get_internal_context().pwm_switched_off_at.has_value());

    // Reinit ends 2 s later with X1: the original switch-off time is kept
    pwm_off_since(std::chrono::milliseconds(2000));
    const auto backdated = charger->get_internal_context().pwm_switched_off_at.value();
    charger->get_internal_context().reinit_deadline = std::chrono::steady_clock::now();
    charger->run_state_machine();
    EXPECT_EQ(observed_cp_state_commands, (std::vector<std::string>{"F", "X1"}));
    EXPECT_NE(charger->current_state(), Charger::EvseState::Reinit);
    EXPECT_EQ(charger->get_internal_context().pwm_switched_off_at.value(), backdated);
    EXPECT_FALSE(pwm_on());
}

TEST_F(ChargerMinX1Test, CpStateEArmsTheTimer) {
    auto& ctx = charger->get_shared_context();
    ctx.pwm_running = true;
    charger->get_internal_context().update_pwm_last_duty_cycle = 0.27;
    charger->set_supports_cp_state_E(true);
    ctx.reinit_config = Charger::ReinitConfiguration{"CPStateE", 3000};
    charger->current_state(Charger::EvseState::Reinit);
    charger->run_state_machine();
    EXPECT_EQ(observed_cp_state_commands, std::vector<std::string>{"E"});
    EXPECT_FALSE(pwm_on());
    EXPECT_EQ(charger->current_state(), Charger::EvseState::Reinit);
    ASSERT_TRUE(charger->get_internal_context().pwm_switched_off_at.has_value());

    // Reinit ends 2 s later with X1: the original switch-off time is kept
    pwm_off_since(std::chrono::milliseconds(2000));
    const auto backdated = charger->get_internal_context().pwm_switched_off_at.value();
    charger->get_internal_context().reinit_deadline = std::chrono::steady_clock::now();
    charger->run_state_machine();
    EXPECT_EQ(observed_cp_state_commands, (std::vector<std::string>{"E", "X1"}));
    EXPECT_NE(charger->current_state(), Charger::EvseState::Reinit);
    EXPECT_EQ(charger->get_internal_context().pwm_switched_off_at.value(), backdated);
    EXPECT_FALSE(pwm_on());
}

// ----------------------------------------------------------------------------
// IECStateMachine stub
IECStateMachine::IECStateMachine(const std::unique_ptr<evse_board_supportIntf>& r_bsp_, bool lock_connector_in_state_b_,
                                 bool use_authorized_, bool keep_cable_locked_, int keep_cable_locked_lock_delay_ms_) :
    r_bsp(r_bsp_) {
}
void IECStateMachine::process_bsp_event(const types::board_support_common::BspEvent& bsp_event) {
}
void IECStateMachine::allow_power_on(bool value, types::evse_board_support::Reason reason) {
}

std::optional<double> IECStateMachine::read_pp_ampacity() {
    return std::nullopt;
}
void IECStateMachine::switch_three_phases_while_charging(bool n) {
}
void IECStateMachine::setup(bool has_ventilation) {
}

void IECStateMachine::set_overcurrent_limit(double amps) {
}

void IECStateMachine::set_pwm(double value) {
}
void IECStateMachine::set_cp_state_X1() {
    observed_cp_state_commands.emplace_back("X1");
}

void IECStateMachine::set_cp_state_E() {
    observed_cp_state_commands.emplace_back("E");
}

void IECStateMachine::set_cp_state_F() {
    observed_cp_state_commands.emplace_back("F");
}

void IECStateMachine::enable(bool en) {
}

void IECStateMachine::connector_force_unlock() {
}

void IECStateMachine::set_authorized(bool a) {
}

const std::string cpevent_to_string(CPEvent e) {
    switch (e) {
    case CPEvent::CarPluggedIn:
        return "CarPluggedIn";
    case CPEvent::CarRequestedPower:
        return "CarRequestedPower";
    case CPEvent::PowerOn:
        return "PowerOn";
    case CPEvent::PowerOff:
        return "PowerOff";
    case CPEvent::CarRequestedStopPower:
        return "CarRequestedStopPower";
    case CPEvent::CarUnplugged:
        return "CarUnplugged";
    case CPEvent::EFtoBCD:
        return "EFtoBCD";
    case CPEvent::BCDtoEF:
        return "BCDtoEF";
    case CPEvent::BCDtoE:
        return "BCDtoE";
    }
    throw std::out_of_range("No known string conversion for provided enum of type CPEvent");
}

// ----------------------------------------------------------------------------
//  ErrorHandling stub
ErrorHandling::ErrorHandling(const std::unique_ptr<evse_board_supportIntf>& r_bsp,
                             const std::vector<std::unique_ptr<ISO15118_chargerIntf>>& r_hlc,
                             const std::vector<std::unique_ptr<connector_lockIntf>>& r_connector_lock,
                             const std::vector<std::unique_ptr<ac_rcdIntf>>& r_ac_rcd,
                             const std::unique_ptr<evse_managerImplBase>& _p_evse,
                             const std::vector<std::unique_ptr<isolation_monitorIntf>>& _r_imd,
                             const std::vector<std::unique_ptr<power_supply_DCIntf>>& _r_powersupply,
                             const std::vector<std::unique_ptr<powermeterIntf>>& _r_powermeter,
                             const std::vector<std::unique_ptr<slacIntf>>& _r_slac,
                             const std::vector<std::unique_ptr<over_voltage_monitorIntf>>& _r_over_voltage_monitor,
                             bool _inoperative_error_use_vendor_id) :
    r_bsp(r_bsp),
    r_hlc(r_hlc),
    r_connector_lock(r_connector_lock),
    r_ac_rcd(r_ac_rcd),
    p_evse(p_evse),
    r_imd(_r_imd),
    r_powersupply(r_powersupply),
    r_powermeter(_r_powermeter),
    r_slac(_r_slac),
    r_over_voltage_monitor(_r_over_voltage_monitor),
    inoperative_error_use_vendor_id(_inoperative_error_use_vendor_id) {
}

void ErrorHandling::raise_overcurrent_error(const std::string& description) {
}
void ErrorHandling::clear_overcurrent_error() {
}

void ErrorHandling::raise_over_voltage_error(Everest::error::Severity severity, const std::string& description) {
}
void ErrorHandling::clear_over_voltage_error() {
}

void ErrorHandling::raise_internal_error(const std::string& description) {
}
void ErrorHandling::clear_internal_error() {
}

void ErrorHandling::raise_authorization_timeout_error(const std::string& description) {
}
void ErrorHandling::clear_authorization_timeout_error() {
}

void ErrorHandling::raise_powermeter_transaction_start_failed_error(const std::string& description) {
}
void ErrorHandling::clear_powermeter_transaction_start_failed_error() {
}

void ErrorHandling::raise_isolation_resistance_fault(const std::string& description, const std::string& sub_type) {
}
void ErrorHandling::clear_isolation_resistance_fault(const std::string& sub_type) {
}

void ErrorHandling::raise_cable_check_fault(const std::string& description) {
}
void ErrorHandling::clear_cable_check_fault() {
}
void ErrorHandling::clear_voltage_plausibility_fault() {
}

// ----------------------------------------------------------------------------
// SessionLog stub
SessionLog::SessionLog() {
}
SessionLog::~SessionLog() {
}

void SessionLog::setPath(const std::string& path) {
}
void SessionLog::setMqtt(const std::function<void(const nlohmann::json& data)>& mqtt_provider) {
}
void SessionLog::enable() {
}
std::optional<std::filesystem::path> SessionLog::startSession(const std::string& suffix_string) {
    return {};
}
void SessionLog::stopSession() {
}

void SessionLog::car(bool iso15118, const std::string& msg) {
}
void SessionLog::car(bool iso15118, const std::string& msg, const std::string& xml, const std::string& xml_hex,
                     const std::string& xml_base64, const std::string& json_str) {
}

void SessionLog::evse(bool iso15118, const std::string& msg) {
}
void SessionLog::evse(bool iso15118, const std::string& msg, const std::string& xml, const std::string& xml_hex,
                      const std::string& xml_base64, const std::string& json_str) {
}

void SessionLog::xmlOutput(bool e) {
}

void SessionLog::sys(const std::string& msg) {
}

SessionLog session_log;

// ----------------------------------------------------------------------------
// PersistentStore stub
PersistentStore::PersistentStore(const std::vector<std::unique_ptr<kvsIntf>>& r_store, const std::string module_id) :
    r_store(r_store) {
}

void PersistentStore::store_session(const std::string& session_uuid) {
}
void PersistentStore::clear_session() {
}
std::string PersistentStore::get_session() {
    return {};
}

} // namespace module
