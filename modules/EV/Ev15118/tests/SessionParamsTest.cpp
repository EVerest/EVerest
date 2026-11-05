// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

#include <framework/ModuleAdapter.hpp>

#include "ISO15118_evImpl.hpp"
#include "fake_controller.hpp"
#include "module_driver.hpp"

namespace {

using module_driver::Driver;
using module_driver::TestImpl;
using Mode = types::iso15118::EnergyTransferMode;

// What the module hands the Controller it builds, and what it pushes to a running one.
class SessionParams : public ::testing::Test {
protected:
    SessionParams() {
        fake_controller::reset();
        conf.device = "lo";
        conf.evcc_id = "EVTESTID01";
        conf.control_mode = "Dynamic";
        conf.supported_protocols = "ISO15118_20";
        conf.cpd_rounds = 1;
        conf.ac_phase_count = 3;
        conf.ac_max_charge_power_w = 11040;
        conf.ac_min_charge_power_w = 4140;
        conf.dc_max_discharge_power_w = 100000;
        conf.dc_max_discharge_current_a = 200;

        adapter.publish = [this](const std::string&, const std::string& variable, const Value&) {
            if (variable == "v2g_session_finished") {
                std::lock_guard lock(mutex);
                ++finished;
                changed.notify_all();
            }
        };
        module_driver::stub_lookups(adapter);

        auto owned = std::make_unique<TestImpl>(&adapter, mod, impl_conf);
        impl = owned.get();
        module = std::make_unique<module::Ev15118>(info, std::move(owned), conf);
        mod.set(module.get());
        Driver{info}.ready(*impl);
    }

    ~SessionParams() override {
        module.reset();
    }

    bool start(Mode mode) {
        types::iso15118::SelectedPaymentOption payment{};
        double departure_time = 0;
        double e_amount = 0;
        return impl->handle_start_charging(mode, payment, departure_time, e_amount);
    }

    // Runs one session with `body` inside its loop; the Construction it was built with.
    fake_controller::Construction run_session(Mode mode, fake_controller::LoopBody body = nullptr) {
        int before = 0;
        {
            std::lock_guard lock(mutex);
            before = finished;
        }
        fake_controller::set_loop_body(std::move(body));
        EXPECT_TRUE(start(mode));
        std::unique_lock lock(mutex);
        EXPECT_TRUE(changed.wait_for(lock, module_driver::deadline, [&] { return finished > before; }));
        const auto constructions = fake_controller::constructions();
        return constructions.empty() ? fake_controller::Construction{} : constructions.back();
    }

    void set_dc_params(types::iso15118::DcEvParameters params) {
        impl->handle_set_dc_params(params);
    }

    void set_bpt_dc_params(float max_power, float max_current) {
        types::iso15118::DcEvBPTParameters params;
        params.discharge_max_power_limit = max_power;
        params.discharge_max_current_limit = max_current;
        impl->handle_set_bpt_dc_params(params);
    }

    void report_present_values(types::iso15118::EvPresentValues values) {
        impl->handle_update_present_values(values);
    }

    std::mutex mutex;
    std::condition_variable changed;
    int finished{0};

    module::Conf conf{};
    module::ev::Conf impl_conf{};
    ModuleInfo info{};
    Everest::ModuleAdapter adapter{};
    Everest::PtrContainer<module::Ev15118> mod;
    TestImpl* impl{nullptr};
    std::unique_ptr<module::Ev15118> module;
};

types::iso15118::EvPresentValues all_present_values() {
    types::iso15118::EvPresentValues values;
    values.present_voltage = 230;
    values.present_active_power = 7000;
    values.present_frequency = 50;
    values.der_alarm_status = 3;
    return values;
}

// A measurement belongs to the grid it was taken on; a new session starts without one.
TEST_F(SessionParams, PresentValuesDoNotCarryIntoTheNextSession) {
    run_session(Mode::AC_three_phase_core, [this] { report_present_values(all_present_values()); });
    const auto calls = fake_controller::channel_calls();
    EXPECT_EQ(calls.present_active_power, std::vector<float>{7000});

    report_present_values(all_present_values());
    const auto next = run_session(Mode::AC_three_phase_core).ac_params;
    EXPECT_FALSE(next.present_voltage.has_value());
    EXPECT_FALSE(next.present_frequency.has_value());
    EXPECT_EQ(next.der_alarm_status, 0U);
    EXPECT_EQ(next.present_active_power, 0.0f);
}

TEST_F(SessionParams, PartialDcParamsKeepTheOtherValues) {
    set_dc_params(module_driver::sane_dc_params());
    types::iso15118::DcEvParameters partial;
    partial.target_current = 50;
    set_dc_params(partial);

    const auto params = run_session(Mode::DC).dc_params;
    EXPECT_EQ(params.max_charge_power, 150000.0f);
    EXPECT_EQ(params.max_charge_current, 300.0f);
    EXPECT_EQ(params.max_voltage, 900.0f);
    EXPECT_EQ(params.min_voltage, 200.0f);
    EXPECT_EQ(params.energy_capacity, 60000.0f);
    EXPECT_EQ(params.target_voltage, 400.0f);
    EXPECT_EQ(params.target_current, 50.0f);
}

TEST_F(SessionParams, PartialDcParamsAreMergedBeforeTheLivePush) {
    set_dc_params(module_driver::sane_dc_params());
    run_session(Mode::DC, [this] {
        types::iso15118::DcEvParameters partial;
        partial.target_current = 20;
        set_dc_params(partial);
    });

    const auto pushed = fake_controller::channel_calls().dc_params;
    ASSERT_EQ(pushed.size(), 1U);
    EXPECT_EQ(pushed.front().max_charge_power, 150000.0f);
    EXPECT_EQ(pushed.front().max_voltage, 900.0f);
    EXPECT_EQ(pushed.front().target_current, 20.0f);
}

// A minimum voltage above the stored maximum: nothing of the update is kept or pushed.
TEST_F(SessionParams, InvalidDcParamsAreRejectedWhole) {
    set_dc_params(module_driver::sane_dc_params());
    types::iso15118::DcEvParameters invalid;
    invalid.min_voltage_limit = 1000;
    invalid.target_current = 5;
    run_session(Mode::DC, [this, invalid] { set_dc_params(invalid); });
    EXPECT_TRUE(fake_controller::channel_calls().dc_params.empty());

    set_dc_params(invalid);
    const auto params = run_session(Mode::DC).dc_params;
    EXPECT_EQ(params.min_voltage, 200.0f);
    EXPECT_EQ(params.target_current, 100.0f);
}

TEST_F(SessionParams, DcStartWithoutDcParamsIsRejected) {
    EXPECT_FALSE(start(Mode::DC));
    EXPECT_FALSE(start(Mode::DC_BPT));
    EXPECT_TRUE(fake_controller::constructions().empty());
}

TEST_F(SessionParams, BptParamsArePushedToARunningBptSession) {
    set_dc_params(module_driver::sane_dc_params());
    run_session(Mode::DC_BPT, [this] { set_bpt_dc_params(5000, 20); });

    const auto pushed = fake_controller::channel_calls().dc_params;
    ASSERT_EQ(pushed.size(), 1U);
    EXPECT_EQ(pushed.front().max_discharge_power, 5000.0f);
    EXPECT_EQ(pushed.front().max_discharge_current, 20.0f);
    EXPECT_EQ(pushed.front().max_charge_power, 150000.0f);
}

TEST_F(SessionParams, BptParamsDoNotReachARunningUnidirectionalSession) {
    set_dc_params(module_driver::sane_dc_params());
    run_session(Mode::DC, [this] { set_bpt_dc_params(5000, 20); });
    EXPECT_TRUE(fake_controller::channel_calls().dc_params.empty());
}

// A maximum below the configured minimum discharge power: nothing of the update is kept or pushed.
TEST_F(SessionParams, InvalidBptParamsAreRejectedWhole) {
    conf.dc_min_discharge_power_w = 5000;
    set_dc_params(module_driver::sane_dc_params());
    run_session(Mode::DC_BPT, [this] {
        set_bpt_dc_params(1000, 20);
        types::iso15118::DcEvParameters partial;
        partial.target_current = 20;
        set_dc_params(partial);
    });

    const auto pushed = fake_controller::channel_calls().dc_params;
    ASSERT_EQ(pushed.size(), 1U);
    EXPECT_EQ(pushed.front().target_current, 20.0f);
    EXPECT_EQ(pushed.front().max_discharge_power, 100000.0f);
    EXPECT_EQ(pushed.front().max_discharge_current, 200.0f);
    EXPECT_EQ(run_session(Mode::DC_BPT).dc_params.max_discharge_power, 100000.0f);
}

// Checked on arrival with no session running too: a rejected update is not left to fail the next start.
TEST_F(SessionParams, InvalidBptParamsWhileIdleAreRejectedWhole) {
    conf.dc_min_discharge_power_w = 5000;
    set_dc_params(module_driver::sane_dc_params());
    set_bpt_dc_params(1000, 20);

    const auto params = run_session(Mode::DC_BPT).dc_params;
    EXPECT_EQ(params.max_discharge_power, 100000.0f);
    EXPECT_EQ(params.max_discharge_current, 200.0f);
}

// While idle only the discharge limits it carries are checked, so it may come before set_dc_params.
TEST_F(SessionParams, BptParamsWhileIdleBeforeDcParamsAreKept) {
    set_bpt_dc_params(5000, 20);
    set_dc_params(module_driver::sane_dc_params());

    const auto params = run_session(Mode::DC_BPT).dc_params;
    EXPECT_EQ(params.max_discharge_power, 5000.0f);
    EXPECT_EQ(params.max_discharge_current, 20.0f);
}

// set_bpt_dc_params states vehicle limits, like set_dc_params: later BPT sessions keep them.
TEST_F(SessionParams, BptParamsCarryIntoLaterBptSessions) {
    set_dc_params(module_driver::sane_dc_params());
    const auto first = run_session(Mode::DC_BPT).dc_params;
    EXPECT_EQ(first.max_discharge_power, 100000.0f);
    EXPECT_EQ(first.max_discharge_current, 200.0f);

    set_bpt_dc_params(5000, 20);
    run_session(Mode::DC);
    const auto later = run_session(Mode::DC_BPT).dc_params;
    EXPECT_EQ(later.max_discharge_power, 5000.0f);
    EXPECT_EQ(later.max_discharge_current, 20.0f);
}

// ac_phase_count 3 states the totals across three lines; a single-phase session draws one line's share.
TEST_F(SessionParams, SinglePhaseSessionScalesTheThreePhaseTotals) {
    const auto single = run_session(Mode::AC_single_phase_core).ac_params;
    EXPECT_EQ(single.phase_count, 1U);
    EXPECT_FLOAT_EQ(single.max_charge_power, 3680.0f);
    EXPECT_FLOAT_EQ(single.min_charge_power, 1380.0f);

    const auto three = run_session(Mode::AC_three_phase_core).ac_params;
    EXPECT_EQ(three.phase_count, 3U);
    EXPECT_FLOAT_EQ(three.max_charge_power, 11040.0f);
    EXPECT_FLOAT_EQ(three.min_charge_power, 4140.0f);
}

TEST_F(SessionParams, SinglePhaseConfigIsKeptForASinglePhaseSession) {
    conf.ac_phase_count = 1;
    conf.ac_max_charge_power_w = 7360;
    const auto single = run_session(Mode::AC_single_phase_core).ac_params;
    EXPECT_EQ(single.phase_count, 1U);
    EXPECT_FLOAT_EQ(single.max_charge_power, 7360.0f);
}

// Read per session, so a rotated key password reaches the next session without a restart.
TEST_F(SessionParams, KeyPasswordIsReadForEachSession) {
    const auto path = std::filesystem::temp_directory_path() / ("ev15118-key-password-" + std::to_string(::getpid()));
    const auto write = [&path](const char* content) { std::ofstream(path, std::ios::trunc) << content; };
    conf.device_key_password_path = path.string();

    write("first\n");
    EXPECT_EQ(run_session(Mode::AC_three_phase_core).config.tls.client_key_password, "first");
    write("second");
    EXPECT_EQ(run_session(Mode::AC_three_phase_core).config.tls.client_key_password, "second");
    std::filesystem::remove(path);
}

// The CP report is latched and handed to every Controller built after it.
TEST_F(SessionParams, CpReportBeforeTheSessionIsReplayedToIt) {
    auto cp_state = types::iso15118::CpState::C;
    impl->handle_cp_state_changed(cp_state);
    run_session(Mode::AC_three_phase_core);
    run_session(Mode::AC_three_phase_core);
    EXPECT_EQ(fake_controller::channel_calls().cp_states, (std::vector<bool>{true, true}));
}

} // namespace
