// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include <everest/exceptions.hpp>
#include <framework/ModuleAdapter.hpp>

#include "ISO15118_evImpl.hpp"
#include "fake_controller.hpp"
#include "module_driver.hpp"

namespace {

using module_driver::Driver;
using module_driver::TestImpl;

// The message of the EverestConfigError `step` throws; empty when it throws none.
std::string config_error(const std::function<void()>& step) {
    try {
        step();
    } catch (const Everest::EverestConfigError& e) {
        return e.what();
    }
    return {};
}

class SessionPublications : public ::testing::Test {
protected:
    SessionPublications() {
        fake_controller::reset();
        conf.device = "lo";
        conf.evcc_id = "EVTESTID01";
        conf.supported_protocols = "ISO15118_2";
        conf.cpd_rounds = 1;
        conf.ac_phase_count = 3;

        adapter.publish = [this](const std::string&, const std::string& variable, const Value& value) {
            std::lock_guard lock(mutex);
            if (variable == "ac_evse_max_current") {
                max_currents.push_back(value.get<double>());
            } else if (variable == "welding_detection_failed") {
                welding_failures.push_back(value);
            } else if (variable == "v2g_session_finished") {
                finished.notify_all();
                session_finished = true;
                ++finishes;
                session_ends.push_back(variable);
            } else if (variable == "ev_power_ready") {
                session_ends.push_back(variable + (value.get<bool>() ? " true" : " false"));
            } else if (variable == "der_control_received") {
                der_controls.push_back(value);
            }
        };
        module_driver::stub_lookups(adapter);

        auto owned = std::make_unique<TestImpl>(&adapter, mod, impl_conf);
        impl = owned.get();
        module = std::make_unique<module::Ev15118>(info, std::move(owned), conf);
        mod.set(module.get());
    }

    ~SessionPublications() override {
        module.reset();
    }

    // The module reads conf by reference, so a test adjusts it before these.
    void init() {
        Driver{info}.init(*impl);
    }

    void ready() {
        Driver{info}.ready(*impl);
    }

    bool start(types::iso15118::EnergyTransferMode mode) {
        types::iso15118::SelectedPaymentOption payment{};
        double departure_time = 0;
        double e_amount = 0;
        return impl->handle_start_charging(mode, payment, departure_time, e_amount);
    }

    // Runs one AC session with `body` inside its loop; the ac_evse_max_current values it published.
    std::vector<double> run_ac_session(fake_controller::LoopBody body) {
        fake_controller::set_loop_body(std::move(body));
        EXPECT_TRUE(start(types::iso15118::EnergyTransferMode::AC_three_phase_core));
        std::unique_lock lock(mutex);
        EXPECT_TRUE(finished.wait_for(lock, std::chrono::seconds(5), [this] { return session_finished; }));
        return max_currents;
    }

    // Runs one session of `mode` with `body` inside its loop.
    void run_session(types::iso15118::EnergyTransferMode mode, fake_controller::LoopBody body) {
        int before = 0;
        {
            std::lock_guard lock(mutex);
            before = finishes;
        }
        fake_controller::set_loop_body(std::move(body));
        EXPECT_TRUE(start(mode));
        std::unique_lock lock(mutex);
        EXPECT_TRUE(finished.wait_for(lock, std::chrono::seconds(5), [&] { return finishes > before; }));
    }

    std::mutex mutex;
    std::condition_variable finished;
    bool session_finished{false};
    int finishes{0};
    std::vector<double> max_currents;
    std::vector<Value> welding_failures;
    // ev_power_ready values and v2g_session_finished, in publication order.
    std::vector<std::string> session_ends;
    std::vector<Value> der_controls;

    module::Conf conf{};
    module::ev::Conf impl_conf{};
    ModuleInfo info{};
    Everest::ModuleAdapter adapter{};
    Everest::PtrContainer<module::Ev15118> mod;
    TestImpl* impl{nullptr};
    std::unique_ptr<module::Ev15118> module;
};

TEST_F(SessionPublications, AcSessionPublishesTheOfferedMaxCurrent) {
    ready();
    const auto published = run_ac_session([] { fake_controller::offer_ac_evse_max_current(16.0f); });
    EXPECT_EQ(published, std::vector<double>{16.0});
}

TEST_F(SessionPublications, FailedWeldingDetectionPublishesTheLastVoltageAndCycles) {
    ready();
    run_ac_session([] { fake_controller::fail_welding_detection(42.5f, 3); });
    EXPECT_EQ(welding_failures, std::vector<Value>{Value({{"cycles", 3}, {"last_voltage_V", 42.5}})});
}

// The voltage is NaN when welding fails before any accepted response; JSON has no NaN.
TEST_F(SessionPublications, FailedWeldingDetectionWithoutAVoltageOmitsIt) {
    ready();
    run_ac_session([] { fake_controller::fail_welding_detection(std::numeric_limits<float>::quiet_NaN(), 0); });
    EXPECT_EQ(welding_failures, std::vector<Value>{Value({{"cycles", 0}})});
}

TEST_F(SessionPublications, SessionEndWithdrawsPowerReadinessBeforeTheFinish) {
    ready();
    run_ac_session(nullptr);
    std::lock_guard lock(mutex);
    EXPECT_EQ(session_ends, (std::vector<std::string>{"ev_power_ready false", "v2g_session_finished"}));
}

iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode
iec_dynamic_control(std::optional<std::int16_t> q, std::optional<std::int16_t> cos_phi_percent) {
    namespace dt = iso15118::message_20::datatypes;
    dt::DER_Dynamic_AC_CLResControlMode control{};
    if (q) {
        control.dso_q_setpoint = dt::DsoQSetpoint{};
        control.dso_q_setpoint->dso_q_setpoint_value = {*q, 0};
    }
    if (cos_phi_percent) {
        control.dso_cos_phi_setpoint = dt::DsoCosPhiSetpoint{};
        control.dso_cos_phi_setpoint->dso_cos_phi_setpoint_value = {*cos_phi_percent, -2};
    }
    return control;
}

// Every Dynamic response carries the control block; only a change of its setpoints is news, and
// the first block of each session.
TEST_F(SessionPublications, IecDynamicControlIsPublishedWhenItsSetpointsChange) {
    conf.supported_protocols = "ISO15118_20";
    ready();
    const auto blocks = [] {
        for (const auto& [q, cos_phi] :
             std::vector<std::pair<std::optional<std::int16_t>, std::optional<std::int16_t>>>{
                 {500, std::nullopt},
                 {500, std::nullopt},
                 {600, std::nullopt},
                 {600, 95},
                 {600, 95},
                 {std::nullopt, 95},
             }) {
            fake_controller::receive_der_control(iec_dynamic_control(q, cos_phi));
        }
    };
    run_session(types::iso15118::EnergyTransferMode::AC_DER_IEC, blocks);
    run_session(types::iso15118::EnergyTransferMode::AC_DER_IEC,
                [] { fake_controller::receive_der_control(iec_dynamic_control(std::nullopt, 95)); });

    std::lock_guard lock(mutex);
    std::vector<std::pair<std::optional<double>, std::optional<double>>> published;
    for (const auto& value : der_controls) {
        const auto setpoint = [&value](const char* key) {
            return value.contains(key) ? std::make_optional(value.at(key).get<double>()) : std::nullopt;
        };
        EXPECT_EQ(value.at("source"), "ChargeLoop");
        published.emplace_back(setpoint("dso_q_setpoint"), setpoint("dso_cos_phi_setpoint"));
    }
    const std::vector<std::pair<std::optional<double>, std::optional<double>>> expected{
        {500.0, std::nullopt}, {600.0, std::nullopt}, {600.0, 0.95f}, {std::nullopt, 0.95f}, {std::nullopt, 0.95f}};
    EXPECT_EQ(published, expected);
}

// DIN SPEC 70121 is DC only, so an AC start leaves nothing to offer in SupportedAppProtocolReq.
TEST_F(SessionPublications, StartWithNothingToOfferIsRejected) {
    conf.supported_protocols = "DIN70121";
    ready();
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::AC_three_phase_core));
    EXPECT_TRUE(fake_controller::constructions().empty());
}

// A named device that does not resolve at startup is kept, since it may come up later; a start
// while it is still missing is rejected.
TEST_F(SessionPublications, StartWithoutTheHlcInterfaceIsRejected) {
    ready();
    fake_controller::set_interface_present(false);
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::AC_three_phase_core));
    EXPECT_TRUE(fake_controller::constructions().empty());
}

TEST_F(SessionPublications, NoProtocolEnabledFailsStartup) {
    for (const auto* list : {"", "  "}) {
        conf.supported_protocols = list;
        const auto error = config_error([this] { ready(); });
        EXPECT_NE(error.find("no protocol enabled"), std::string::npos) << "'" << list << "': " << error;
        EXPECT_NE(error.find("supported_protocols"), std::string::npos) << "'" << list << "': " << error;
    }
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::DC));
}

TEST_F(SessionPublications, RepeatedProtocolFailsStartup) {
    conf.supported_protocols = "ISO15118_2,DIN70121,ISO15118_2";
    const auto error = config_error([this] { ready(); });
    EXPECT_NE(error.find("supported_protocols"), std::string::npos) << error;
    EXPECT_NE(error.find("ISO15118_2 more than once"), std::string::npos) << error;
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::DC));
}

TEST_F(SessionPublications, UnknownProtocolFailsStartup) {
    for (const auto* list : {"ISO15118_2,iso15118_20", "ISO15118_2,,DIN70121", "ISO15118_2,", "ISO15118-2"}) {
        conf.supported_protocols = list;
        const auto error = config_error([this] { ready(); });
        EXPECT_NE(error.find("supported_protocols"), std::string::npos) << list << ": " << error;
        EXPECT_NE(error.find("unknown"), std::string::npos) << list << ": " << error;
    }
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::DC));
}

TEST_F(SessionPublications, UnresolvableAutoDeviceFailsStartup) {
    conf.device = "auto";
    fake_controller::set_interface_present(false);
    EXPECT_NE(config_error([this] { ready(); }).find("set device"), std::string::npos);
}

TEST_F(SessionPublications, InvalidSaeProfileFailsStartup) {
    conf.sae_inverter_profile_path = "/nonexistent/sae_inverter_profile.json";
    EXPECT_NE(config_error([this] { init(); }).find("sae_inverter_profile_path"), std::string::npos);
}

TEST_F(SessionPublications, SaeProfileValueOutOfRangeFailsStartupNamingTheField) {
    const auto path = std::filesystem::temp_directory_path() / ("ev15118-sae-profile-" + std::to_string(::getpid()));
    std::ofstream(path) << R"({"nominal_frequency_hz": 0})";
    conf.sae_inverter_profile_path = path.string();
    const auto error = config_error([this] { init(); });
    std::filesystem::remove(path);
    EXPECT_NE(error.find("nominal_frequency_hz"), std::string::npos) << error;
}

// The library rejects a power factor of 0, which the parser's shape checks let through.
TEST_F(SessionPublications, SaeProfilePowerFactorZeroFailsStartup) {
    const auto path = std::filesystem::temp_directory_path() / ("ev15118-sae-profile-" + std::to_string(::getpid()));
    std::ofstream(path) << R"({"under_excited_power_factor": 0})";
    conf.sae_inverter_profile_path = path.string();
    const auto error = config_error([this] { init(); });
    std::filesystem::remove(path);
    EXPECT_NE(error.find("under_excited_power_factor"), std::string::npos) << error;
}

TEST_F(SessionPublications, StartBeforeReadyIsRejected) {
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::DC));
}

TEST_F(SessionPublications, StartWithAnUnsupportedTransferModeIsRejected) {
    ready();
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::WPT));
    EXPECT_TRUE(fake_controller::constructions().empty());
}

TEST_F(SessionPublications, StartWithInvalidSessionParametersIsRejected) {
    conf.ac_phase_count = 2;
    ready();
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::AC_three_phase_core));
    EXPECT_TRUE(fake_controller::constructions().empty());
}

TEST_F(SessionPublications, StartWhileASessionIsActiveIsRejected) {
    ready();
    module_driver::set_sane_dc_params(*impl);
    std::promise<void> release;
    fake_controller::set_loop_body([ended = release.get_future().share()] { ended.wait(); });
    ASSERT_TRUE(start(types::iso15118::EnergyTransferMode::DC));
    EXPECT_FALSE(start(types::iso15118::EnergyTransferMode::DC));
    release.set_value();
}

} // namespace
