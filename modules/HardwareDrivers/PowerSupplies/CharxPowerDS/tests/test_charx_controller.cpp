// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "charx_controller.hpp"

using namespace charx;
using namespace std::chrono_literals;

namespace {

bool same(Obj a, Obj b) {
    return a.index == b.index && a.sub == b.sub;
}

std::string name(Obj o) {
    if (same(o, obj::readiness)) {
        return "readiness";
    }
    if (same(o, obj::ds_output)) {
        return "ds_output";
    }
    if (same(o, obj::i_set)) {
        return "i_set";
    }
    if (same(o, obj::v_set)) {
        return "v_set";
    }
    return "other";
}

// Shared event log, so the tests can check the order of bus writes and reported errors.
using Events = std::vector<std::string>;

// A simulated CHARX power 40 kW DS, as seen through SDO.
class FakeModule : public SdoClient {
public:
    explicit FakeModule(Events& events_) : events(events_) {
    }

    // object dictionary
    int32_t v_min_avl{50000};
    int32_t v_max_avl{1000000};
    int32_t i_max_avl{125000};
    int32_t v_set{0};
    int32_t i_set{0};
    int32_t requested_contactor{0};
    int32_t contactor{0};
    int32_t contactor_error{0};
    bool power{false};
    uint32_t extra_flags{0};

    // behaviour
    int close_after_status_reads{2}; // the contactor reports closed this many status reads after the command
    bool follow_contactor{true};
    bool close_on_command{false}; // the contactor closes the moment it is commanded, before the next status read
    bool ignore_power_off{false};
    bool online{true};
    SdoResult::Status offline_status{SdoResult::Status::Timeout};
    int load_current_mA{-1}; // measured current while on; -1 = the current setpoint

    // statistics
    int nmt_starts{0};
    int contactor_error_reads{0};
    std::vector<std::pair<std::string, int32_t>> writes;

    uint32_t flags() const {
        uint32_t f = extra_flags;
        if (power) {
            f |= 1u << flag::PowerOn;
        } else {
            f |= (1u << flag::PfcOff) | (1u << flag::DcOutputSideOff);
        }
        return f;
    }

    int count(const std::string& object, int32_t value) const {
        int n = 0;
        for (const auto& w : writes) {
            n += (w.first == object && w.second == value);
        }
        return n;
    }

    SdoResult read(Obj o) override {
        if (!online) {
            return failed();
        }
        if (same(o, obj::v_min_avl)) {
            return ok(v_min_avl);
        }
        if (same(o, obj::v_max_avl)) {
            return ok(v_max_avl);
        }
        if (same(o, obj::i_max_avl)) {
            return ok(i_max_avl);
        }
        if (same(o, obj::flags1)) {
            return ok(static_cast<int32_t>(flags()));
        }
        if (same(o, obj::ds_output_status)) {
            if (follow_contactor && contactor != requested_contactor &&
                ++status_reads_since_command >= close_after_status_reads) {
                contactor = requested_contactor;
            }
            return ok(contactor);
        }
        if (same(o, obj::v_meas)) {
            return ok(power ? v_set : 0);
        }
        if (same(o, obj::i_meas)) {
            return ok(power ? (load_current_mA >= 0 ? load_current_mA : i_set) : 0);
        }
        if (same(o, obj::contactor_error)) {
            contactor_error_reads++;
            return ok(contactor_error);
        }
        SdoResult r;
        r.status = SdoResult::Status::Abort;
        r.abort_code = 0x06020000; // object does not exist
        return r;
    }

    SdoResult write(Obj o, int32_t value) override {
        if (!online) {
            return failed();
        }
        events.push_back("write " + name(o) + "=" + std::to_string(value));
        writes.emplace_back(name(o), value);
        if (same(o, obj::readiness)) {
            if (value == 1) {
                if (contactor == 0 || contactor != requested_contactor) {
                    SdoResult r;
                    r.status = SdoResult::Status::Abort;
                    r.abort_code = ABORT_DEVICE_STATE;
                    return r;
                }
                power = true;
            } else if (!ignore_power_off) {
                power = false;
            }
        } else if (same(o, obj::ds_output)) {
            requested_contactor = value;
            status_reads_since_command = 0;
            if (close_on_command && value != 0) {
                contactor = value;
            }
            if (value == 0 && !ignore_power_off) {
                contactor = 0;
            }
        } else if (same(o, obj::i_set)) {
            i_set = value;
        } else if (same(o, obj::v_set)) {
            v_set = value;
        }
        return ok(0);
    }

    bool start_remote_node() override {
        nmt_starts++;
        return online;
    }

private:
    static SdoResult ok(int32_t v) {
        SdoResult r;
        r.status = SdoResult::Status::Ok;
        r.value = v;
        return r;
    }
    SdoResult failed() const {
        SdoResult r;
        r.status = offline_status;
        return r;
    }

    Events& events;
    int status_reads_since_command{0};
};

class Recorder : public ControllerOutputs {
public:
    explicit Recorder(Events& events_) : events(events_) {
    }

    void on_capabilities(const Capabilities& c) override {
        caps.push_back(c);
    }
    void on_mode(bool exporting) override {
        modes.push_back(exporting);
        events.push_back(exporting ? "mode Export" : "mode Off");
    }
    void on_measurement(double voltage_V, double current_A) override {
        last_voltage = voltage_V;
        last_current = current_A;
        measurements++;
    }
    void on_error(ErrorType type, bool active, const std::string& message) override {
        errors[type] = active;
        messages[type] = message;
        events.push_back(std::string(active ? "raise " : "clear ") + std::to_string(static_cast<int>(type)));
    }

    bool error(ErrorType type) const {
        const auto it = errors.find(type);
        return it != errors.end() && it->second;
    }

    std::vector<Capabilities> caps;
    std::vector<bool> modes;
    std::map<ErrorType, bool> errors;
    std::map<ErrorType, std::string> messages;
    double last_voltage{0};
    double last_current{0};
    int measurements{0};

private:
    Events& events;
};

class ControllerTest : public ::testing::Test {
protected:
    ControllerTest() : module(events), outputs(events) {
        config.max_export_current_A = 30;
        config.max_export_power_W = 15000;
        config.power_on_timeout = 5s;
        config.power_off_timeout = 5s;
        controller = std::make_unique<Controller>(config, outputs);
    }

    // one poll cycle, 250 ms later than the last one
    bool step() {
        now += 250ms;
        return controller->step(module, now);
    }
    void steps(int n) {
        for (int k = 0; k < n; k++) {
            step();
        }
    }
    // runs cycles until the reported mode is the wanted one; false if it did not happen within max cycles
    bool step_until_mode(bool exporting, int max = 40) {
        for (int k = 0; k < max; k++) {
            step();
            if (!outputs.modes.empty() && outputs.modes.back() == exporting) {
                return true;
            }
        }
        return false;
    }
    void switch_on(double v = 444, double i = 5) {
        controller->set_export_setpoint(v, i);
        controller->set_mode(true);
        ASSERT_TRUE(step_until_mode(true));
    }
    int index_of(const std::string& event, int from = 0) const {
        for (int k = from; k < static_cast<int>(events.size()); k++) {
            if (events[k] == event) {
                return k;
            }
        }
        return -1;
    }

    Events events;
    FakeModule module;
    Recorder outputs;
    ControllerConfig config;
    std::unique_ptr<Controller> controller;
    Controller::Clock::time_point now{Controller::Clock::time_point{} + 1h};
};

} // namespace

TEST_F(ControllerTest, CapabilitiesComeFromTheModuleCappedByConfig) {
    steps(5);
    ASSERT_EQ(outputs.caps.size(), 1u);
    EXPECT_DOUBLE_EQ(outputs.caps[0].min_export_voltage_V, 50);
    EXPECT_DOUBLE_EQ(outputs.caps[0].max_export_voltage_V, 1000);
    EXPECT_DOUBLE_EQ(outputs.caps[0].max_export_current_A, 30);
    EXPECT_DOUBLE_EQ(outputs.caps[0].max_export_power_W, 15000);
    EXPECT_DOUBLE_EQ(outputs.caps[0].current_regulation_tolerance_A, 0.3);
}

TEST_F(ControllerTest, ModuleLimitsWinOverWiderConfig) {
    module.i_max_avl = 20000;
    module.v_min_avl = 150000;
    step();
    ASSERT_EQ(outputs.caps.size(), 1u);
    EXPECT_DOUBLE_EQ(outputs.caps[0].min_export_voltage_V, 150);
    EXPECT_DOUBLE_EQ(outputs.caps[0].max_export_current_A, 20);
}

TEST_F(ControllerTest, ReportsOffOnceAtStart) {
    steps(5);
    EXPECT_EQ(outputs.modes, std::vector<bool>{false});
    EXPECT_EQ(outputs.measurements, 5);
}

TEST_F(ControllerTest, SwitchOnFollowsTheModulesOrder) {
    switch_on(444, 5);
    const int v = index_of("write v_set=444000");
    const int i0 = index_of("write i_set=0");
    const int close = index_of("write ds_output=1");
    const int on = index_of("write readiness=1", close);
    const int i = index_of("write i_set=5000");
    ASSERT_GE(v, 0);
    ASSERT_GE(i0, 0);
    ASSERT_GE(close, 0);
    ASSERT_GE(on, 0);
    ASSERT_GE(i, 0);
    EXPECT_LT(i0, close);
    EXPECT_LT(close, on);
    EXPECT_LT(on, i);
    // PowerOn is refused while the contactor still reports open, and repeated until accepted
    EXPECT_GE(module.count("readiness", 1), 2);
    EXPECT_TRUE(module.power);
    EXPECT_EQ(module.contactor, 1);
    EXPECT_DOUBLE_EQ(outputs.last_voltage, 444);
    EXPECT_DOUBLE_EQ(outputs.last_current, 5);
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, ExportIsReportedOnlyWhenTheModuleConfirmsIt) {
    controller->set_export_setpoint(444, 5);
    controller->set_mode(true);
    step(); // commands sent, contactor not closed yet
    EXPECT_EQ(outputs.modes, std::vector<bool>{false});
    ASSERT_TRUE(step_until_mode(true));
    EXPECT_TRUE(module.power);
    EXPECT_EQ(module.contactor, 1);
}

TEST_F(ControllerTest, ContactorBIsUsedWhenConfigured) {
    config.contactor = 2;
    controller = std::make_unique<Controller>(config, outputs);
    switch_on();
    EXPECT_GE(module.count("ds_output", 2), 1);
    EXPECT_EQ(module.count("ds_output", 1), 0);
}

TEST_F(ControllerTest, SwitchOff) {
    switch_on();
    const int mark = static_cast<int>(events.size());
    controller->set_mode(false);
    ASSERT_TRUE(step_until_mode(false));
    steps(2);
    EXPECT_FALSE(module.power);
    EXPECT_EQ(module.contactor, 0);
    EXPECT_TRUE(controller->wait_until_off_safe(10ms));
    // current 0 and PowerOff first, the module's contactor opens last
    const int i0 = index_of("write i_set=0", mark);
    const int off = index_of("write readiness=0", mark);
    const int open = index_of("write ds_output=0", mark);
    ASSERT_GE(i0, 0);
    ASSERT_GE(off, 0);
    ASSERT_GE(open, 0);
    EXPECT_LT(i0, off);
    EXPECT_LT(off, open);
}

TEST_F(ControllerTest, ContactorOpensOnlyOnceTheStageIsOffOrTheCurrentIsLow) {
    module.load_current_mA = 20000;
    switch_on(444, 20);
    module.ignore_power_off = true; // the stage keeps running and keeps 20 A flowing
    controller->set_mode(false);
    steps(4); // 1 s
    EXPECT_EQ(module.count("ds_output", 0), 0);
    module.load_current_mA = 500;
    steps(2);
    EXPECT_EQ(module.count("ds_output", 0), 1);
}

TEST_F(ControllerTest, ContactorOpensAfterTheGraceTimeAtTheLatest) {
    module.load_current_mA = 20000;
    switch_on(444, 20);
    module.ignore_power_off = true;
    controller->set_mode(false);
    steps(13); // exactly 3 s after PowerOff
    EXPECT_EQ(module.count("ds_output", 0), 0);
    step();
    EXPECT_EQ(module.count("ds_output", 0), 1);
}

TEST_F(ControllerTest, SetpointsAreClampedToTheCapabilities) {
    switch_on(1200, 200);
    EXPECT_GE(module.count("v_set", 1000000), 1);
    EXPECT_GE(module.count("i_set", 30000), 1);
    controller->set_export_setpoint(10, -5);
    step();
    EXPECT_GE(module.count("v_set", 50000), 1);
    EXPECT_GE(module.count("i_set", 0), 1);
}

TEST_F(ControllerTest, NewSetpointsAreSentWhileExporting) {
    switch_on(444, 5);
    controller->set_export_setpoint(450, 20);
    step();
    EXPECT_EQ(module.count("v_set", 450000), 1);
    EXPECT_EQ(module.count("i_set", 20000), 1);
    // without a change nothing is written again
    const auto n = module.writes.size();
    steps(10);
    EXPECT_EQ(module.writes.size(), n);
}

TEST_F(ControllerTest, CommandsAreRepeatedWhenTheModuleDoesNotFollow) {
    switch_on();
    // the module opens its contactor by itself and does not close it again on its own
    module.follow_contactor = false;
    module.contactor = 0;
    const int closes = module.count("ds_output", 1);
    steps(13); // the deviation lasts 3 s: not yet
    EXPECT_EQ(module.count("ds_output", 1), closes);
    steps(2); // past 3 s: decided, and sent in the next cycle
    EXPECT_GT(module.count("ds_output", 1), closes);
    // a module that leaves Export on its own is a fault
    EXPECT_TRUE(outputs.error(ErrorType::VendorError));
    EXPECT_NE(outputs.messages[ErrorType::VendorError].find("on its own"), std::string::npos);
    module.follow_contactor = true;
    ASSERT_TRUE(step_until_mode(true));
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, ASinglePollGlitchNeverInterruptsTheCurrent) {
    switch_on(444, 20);
    steps(2400); // ten minutes of charging
    const int zeros = module.count("i_set", 0);
    const int closes = module.count("ds_output", 1);
    module.follow_contactor = false;
    module.contactor = 0; // one poll sees the contactor open
    step();
    module.contactor = 1;
    module.follow_contactor = true;
    steps(40);
    EXPECT_EQ(module.count("i_set", 0), zeros);
    EXPECT_EQ(module.count("ds_output", 1), closes);
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, DcStageSwitchingKeepsExportAndOffNeedsLowCurrent) {
    module.load_current_mA = 20000;
    switch_on(444, 20);
    const auto modes = outputs.modes.size();
    module.extra_flags = 1u << flag::DcOutputSideOff; // the module switches its DC stages for ~1.3 s
    steps(5);
    EXPECT_EQ(outputs.modes.size(), modes); // no Off/Export flicker
    // the flags look "off", but 20 A still flow through the closed contactor
    EXPECT_FALSE(controller->wait_until_off_safe(20ms));
    module.extra_flags = 0;
    steps(2);
    EXPECT_EQ(outputs.modes.size(), modes);
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, OffIsRepeatedWhenTheModuleStaysOn) {
    switch_on();
    module.ignore_power_off = true;
    controller->set_mode(false);
    step();
    const int offs = module.count("readiness", 0);
    EXPECT_EQ(offs, 1);
    steps(20);
    EXPECT_GT(module.count("readiness", 0), offs);
}

TEST_F(ControllerTest, VendorErrorWhenSwitchOnIsNotConfirmedInTime) {
    module.follow_contactor = false;
    controller->set_export_setpoint(444, 5);
    controller->set_mode(true);
    steps(21); // switch-on requested in the first cycle: exactly 5 s now
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
    step(); // past 5 s
    EXPECT_TRUE(outputs.error(ErrorType::VendorError));
    EXPECT_NE(outputs.messages[ErrorType::VendorError].find("switch-on"), std::string::npos);
    module.follow_contactor = true;
    ASSERT_TRUE(step_until_mode(true));
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, VendorErrorWhenSwitchOffIsNotConfirmedInTime) {
    switch_on();
    module.ignore_power_off = true;
    controller->set_mode(false);
    steps(21); // exactly 5 s after the switch-off request
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
    step();
    EXPECT_TRUE(outputs.error(ErrorType::VendorError));
    EXPECT_NE(outputs.messages[ErrorType::VendorError].find("switch-off"), std::string::npos);
    module.ignore_power_off = false;
    ASSERT_TRUE(step_until_mode(false));
    steps(16); // the repeated Off reaches the module
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, CommunicationFaultAfterThreeFailedPolls) {
    steps(2);
    module.online = false;
    EXPECT_FALSE(step());
    EXPECT_FALSE(step());
    EXPECT_FALSE(outputs.error(ErrorType::CommunicationFault));
    EXPECT_FALSE(step());
    EXPECT_TRUE(outputs.error(ErrorType::CommunicationFault));
    module.online = true;
    EXPECT_TRUE(step());
    EXPECT_FALSE(outputs.error(ErrorType::CommunicationFault));
}

TEST_F(ControllerTest, SetpointsAreSentAgainBeforeTheCommunicationFaultClears) {
    switch_on(444, 5);
    module.online = false;
    steps(3);
    ASSERT_TRUE(outputs.error(ErrorType::CommunicationFault));
    const int mark = static_cast<int>(events.size());
    module.online = true;
    ASSERT_TRUE(step());
    const int v = index_of("write v_set=444000", mark);
    const int i = index_of("write i_set=5000", mark);
    const int clear = index_of("clear " + std::to_string(static_cast<int>(ErrorType::CommunicationFault)), mark);
    ASSERT_GE(v, 0);
    ASSERT_GE(i, 0);
    ASSERT_GE(clear, 0);
    EXPECT_LT(v, clear);
    EXPECT_LT(i, clear);
    // the module kept running: no switch-on sequence, so the current never drops to 0
    EXPECT_EQ(index_of("write i_set=0", mark), -1);
}

TEST_F(ControllerTest, OffIsSentAgainAfterACommunicationLoss) {
    steps(2); // Off sent once at start
    const int offs = module.count("readiness", 0);
    module.online = false;
    steps(3);
    module.online = true;
    step();
    EXPECT_EQ(module.count("readiness", 0), offs + 1);
}

TEST_F(ControllerTest, IoErrorsAreFailedPollsAndTheNodeIsRestarted) {
    module.online = false;
    module.offline_status = SdoResult::Status::IoError;
    steps(2);
    EXPECT_EQ(module.nmt_starts, 0);
    step(); // CommunicationFault raised: NMT start once
    EXPECT_EQ(module.nmt_starts, 1);
    steps(16);
    EXPECT_EQ(module.nmt_starts, 1);
    step(); // and every 20 failed polls
    EXPECT_EQ(module.nmt_starts, 2);
    EXPECT_EQ(controller->failed_polls_in_row(), 20);
}

TEST_F(ControllerTest, NodeIsStartedWhenTheModuleAnswersAgain) {
    steps(2);
    module.online = false;
    step(); // a short outage, e.g. the module rebooting
    module.online = true;
    step();
    EXPECT_EQ(module.nmt_starts, 1);
}

TEST_F(ControllerTest, AModuleThatLostItsStateIsSwitchedOnBeforeTheFaultClears) {
    switch_on(444, 5);
    module.online = false;
    steps(3);
    ASSERT_TRUE(outputs.error(ErrorType::CommunicationFault));
    // the module rebooted meanwhile
    module.power = false;
    module.contactor = 0;
    module.requested_contactor = 0;
    const int mark = static_cast<int>(events.size());
    module.online = true;
    ASSERT_TRUE(step());
    const int close = index_of("write ds_output=1", mark);
    const int clear = index_of("clear " + std::to_string(static_cast<int>(ErrorType::CommunicationFault)), mark);
    ASSERT_GE(close, 0);
    ASSERT_GE(clear, 0);
    EXPECT_LT(close, clear);
    ASSERT_TRUE(step_until_mode(true));
}

TEST_F(ControllerTest, OffReturnsAtOnceWithoutCommunication) {
    switch_on();
    module.online = false;
    steps(3);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(controller->wait_until_off_safe(5s));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST_F(ControllerTest, UnreachableInterfaceCountsAsFailedPoll) {
    controller->report_unreachable("CAN interface charx_can: No such device");
    controller->report_unreachable("CAN interface charx_can: No such device");
    EXPECT_FALSE(outputs.error(ErrorType::CommunicationFault));
    controller->report_unreachable("CAN interface charx_can: No such device");
    EXPECT_TRUE(outputs.error(ErrorType::CommunicationFault));
    EXPECT_EQ(outputs.messages[ErrorType::CommunicationFault], "CAN interface charx_can: No such device");
    step();
    EXPECT_FALSE(outputs.error(ErrorType::CommunicationFault));
}

TEST_F(ControllerTest, FlagsMapToErrors) {
    step();
    module.extra_flags = 1u << flag::Otp;
    step();
    EXPECT_TRUE(outputs.error(ErrorType::OverTemperature));
    module.extra_flags = (1u << flag::AcPhaseLoss) | (1u << flag::OvpOutput) | (1u << flag::OverPower) |
                         (1u << flag::AcOverload) | (1u << flag::Utp) | (1u << flag::OvpInput);
    step();
    EXPECT_FALSE(outputs.error(ErrorType::OverTemperature));
    EXPECT_TRUE(outputs.error(ErrorType::UnderVoltageAC));
    EXPECT_TRUE(outputs.error(ErrorType::OverVoltageDC));
    EXPECT_TRUE(outputs.error(ErrorType::OverCurrentDC));
    EXPECT_TRUE(outputs.error(ErrorType::OverCurrentAC));
    EXPECT_TRUE(outputs.error(ErrorType::UnderTemperature));
    EXPECT_TRUE(outputs.error(ErrorType::OverVoltageAC));
    EXPECT_FALSE(outputs.error(ErrorType::HardwareFault));
    module.extra_flags = 1u << flag::EmergencyStop;
    step();
    EXPECT_TRUE(outputs.error(ErrorType::HardwareFault));
    EXPECT_FALSE(outputs.error(ErrorType::UnderVoltageAC));
    module.extra_flags = 0;
    step();
    EXPECT_FALSE(outputs.error(ErrorType::HardwareFault));
}

TEST_F(ControllerTest, ContactorErrorIsAHardwareFault) {
    step();
    module.contactor_error = 3;
    steps(8); // read on every 8th poll
    EXPECT_TRUE(outputs.error(ErrorType::HardwareFault));
    EXPECT_NE(outputs.messages[ErrorType::HardwareFault].find("contactor error 3"), std::string::npos);
}

TEST_F(ControllerTest, ErrorsAreReportedOnlyOnChange) {
    module.extra_flags = 1u << flag::Otp;
    steps(10);
    EXPECT_EQ(std::count(events.begin(), events.end(),
                         "raise " + std::to_string(static_cast<int>(ErrorType::OverTemperature))),
              1);
}

TEST_F(ControllerTest, WaitUntilOffSafeWaitsForLowCurrent) {
    module.load_current_mA = 10000;
    switch_on(444, 10);
    module.ignore_power_off = true;
    controller->set_mode(false);
    step();
    EXPECT_FALSE(controller->wait_until_off_safe(20ms)); // still on with 10 A
    module.load_current_mA = 1000;                       // below the 2 A threshold
    step();
    EXPECT_TRUE(controller->wait_until_off_safe(20ms));
}

TEST_F(ControllerTest, InstancesDoNotShareState) {
    Events other_events;
    FakeModule other(other_events);
    Recorder other_outputs(other_events);
    Controller second(config, other_outputs);
    steps(3); // the first instance polled three times
    second.step(other, now);
    // every instance reads the contactor error on its own first poll
    EXPECT_EQ(other.contactor_error_reads, 1);
    switch_on();
    EXPECT_FALSE(other.power);
    EXPECT_EQ(other_outputs.modes, std::vector<bool>{false});
}

TEST_F(ControllerTest, WaitForTargetChangeReturnsEarly) {
    std::thread t([this] {
        std::this_thread::sleep_for(20ms);
        controller->set_mode(true);
    });
    const auto start = std::chrono::steady_clock::now();
    controller->wait_for_target_change(5s);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
    t.join();
}

TEST_F(ControllerTest, ContactorStaysClosedWhenOffArrivesDuringADcStageSwitch) {
    module.load_current_mA = 20000;
    switch_on(444, 20);
    module.extra_flags = 1u << flag::DcOutputSideOff; // flags look "off" while 20 A flow
    step();
    controller->set_mode(false);
    step(); // PowerOff cycle: the reading in hand predates PowerOff
    EXPECT_EQ(module.count("ds_output", 0), 0);
    module.extra_flags = 0;
    step(); // polled after PowerOff: the stage is off, no current
    EXPECT_EQ(module.count("ds_output", 0), 1);
}

TEST_F(ControllerTest, ALastingDcSideOffEndsExport) {
    switch_on();
    module.extra_flags = 1u << flag::DcOutputSideOff; // the DC side stays off, PFC and contactor stay on
    steps(12);                                        // 3 s: still taken for a DC stage switch
    EXPECT_TRUE(outputs.modes.back());
    steps(2);
    EXPECT_FALSE(outputs.modes.back());
    steps(14); // the deviation lasts another 3 s
    EXPECT_TRUE(outputs.error(ErrorType::VendorError));
    module.extra_flags = 0;
    ASSERT_TRUE(step_until_mode(true));
    EXPECT_FALSE(outputs.error(ErrorType::VendorError));
}

TEST_F(ControllerTest, OffIsNotSafeOnAReadingFromBeforeTheLastSetpoint) {
    switch_on(444, 1); // 1 A flows, below the 2 A threshold
    controller->set_export_setpoint(444, 20);
    step(); // 20 A written; this cycle's reading still showed 1 A
    controller->set_mode(false);
    EXPECT_FALSE(controller->wait_until_off_safe(20ms));
    step(); // PowerOff written, the reading in hand shows 20 A
    EXPECT_FALSE(controller->wait_until_off_safe(20ms));
    step(); // polled after PowerOff
    EXPECT_TRUE(controller->wait_until_off_safe(20ms));
}

TEST_F(ControllerTest, SwitchOnTimeoutRunsThroughAFlakyBus) {
    module.follow_contactor = false; // the contactor never closes
    controller->set_export_setpoint(444, 5);
    controller->set_mode(true);
    for (int k = 0; k < 40 && !outputs.error(ErrorType::VendorError); k++) {
        module.online = (k % 4 != 3); // one lost poll every second
        step();
    }
    EXPECT_TRUE(outputs.error(ErrorType::VendorError));
    EXPECT_NE(outputs.messages[ErrorType::VendorError].find("switch-on"), std::string::npos);
}

TEST_F(ControllerTest, AnOpenContactorReadBeforeTheCloseCommandIsNotSafe) {
    module.close_on_command = true; // PowerOn is accepted in the very cycle the contactor was commanded
    step();
    controller->set_export_setpoint(444, 20);
    controller->set_mode(true);
    step(); // read "open", then close, PowerOn accepted, 20 A set
    ASSERT_TRUE(module.power);
    controller->set_mode(false);
    EXPECT_FALSE(controller->wait_until_off_safe(20ms)); // the stored "open" predates the close command
}
