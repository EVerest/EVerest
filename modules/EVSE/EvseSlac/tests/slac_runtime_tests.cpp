// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// SlacRuntime is the EvseSlac module without the framework: PLC I/O, event loop, state machine
// controller and the init/ready/shutdown/command lifecycle. These tests drive it exactly the way the
// framework does (init() on one thread, ready() blocking on another, commands and shutdown() from a
// third) with a fake PLC link and a sink that records every output.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

#include "slac_runtime.hpp"

namespace {

using namespace std::chrono_literals;
using everest::lib::slac::D3State;
using module::main::LogLevel;
using module::main::SlacIo;
using module::main::SlacRuntime;
using module::main::SlacRuntimeConfig;
using module::main::SlacSink;
namespace defs = everest::lib::slac::defs;
namespace messages = everest::lib::slac::messages;

constexpr std::uint16_t SET_KEY_REQ = defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ;
constexpr std::uint16_t AMP_MAP_REQ = defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_REQ;
constexpr std::uint16_t SLAC_PARM_CNF = defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF;
constexpr std::uint16_t ATTEN_CHAR_IND = defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_IND;

using EvMac = messages::HomeplugMessage::MacAddress;
using RunId = std::array<std::uint8_t, defs::RUN_ID_LEN>;

messages::HomeplugMessage create_cm_set_key_cnf() {
    messages::cm_set_key_cnf cnf{};
    cnf.result = defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS;
    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

// The EV side of one matching session, as the library's matching test builds it.
template <typename PayloadT>
messages::HomeplugMessage frame_from(EvMac const& source, PayloadT const& payload, std::uint16_t mmtype) {
    messages::HomeplugMessage message;
    message.set_source(source);
    message.setup_payload(&payload, sizeof(payload), mmtype, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_slac_parm_req(EvMac const& ev_mac, RunId const& run_id) {
    messages::cm_slac_parm_req req{};
    req.application_type = defs::COMMON_APPLICATION_TYPE;
    req.security_type = defs::COMMON_SECURITY_TYPE;
    std::copy(run_id.begin(), run_id.end(), req.run_id);
    return frame_from(ev_mac, req, defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ);
}

messages::HomeplugMessage create_cm_start_atten_char_ind(EvMac const& ev_mac, RunId const& run_id) {
    messages::cm_start_atten_char_ind msg{};
    msg.application_type = defs::COMMON_APPLICATION_TYPE;
    msg.security_type = defs::COMMON_SECURITY_TYPE;
    msg.num_sounds = defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
    msg.timeout = defs::CM_SLAC_PARM_CNF_TIMEOUT;
    msg.resp_type = defs::CM_SLAC_PARM_CNF_RESP_TYPE;
    std::copy(ev_mac.begin(), ev_mac.end(), msg.forwarding_sta);
    std::copy(run_id.begin(), run_id.end(), msg.run_id);
    return frame_from(ev_mac, msg, defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND);
}

messages::HomeplugMessage create_cm_atten_profile_ind(EvMac const& ev_mac, std::uint8_t seed) {
    messages::cm_atten_profile_ind msg{};
    std::copy(ev_mac.begin(), ev_mac.end(), msg.pev_mac);
    msg.num_groups = defs::AAG_LIST_LEN;
    for (int i = 0; i < defs::AAG_LIST_LEN; ++i) {
        msg.aag[i] = static_cast<std::uint8_t>(seed + i);
    }
    return frame_from(ev_mac, msg, defs::MMTYPE_CM_ATTEN_PROFILE | defs::MMTYPE_MODE_IND);
}

messages::HomeplugMessage create_cm_atten_char_rsp(EvMac const& ev_mac, RunId const& run_id) {
    messages::cm_atten_char_rsp msg{};
    msg.application_type = defs::COMMON_APPLICATION_TYPE;
    msg.security_type = defs::COMMON_SECURITY_TYPE;
    msg.result = defs::CM_ATTEN_CHAR_RSP_RESULT;
    std::copy(ev_mac.begin(), ev_mac.end(), msg.source_address);
    std::copy(run_id.begin(), run_id.end(), msg.run_id);
    return frame_from(ev_mac, msg, defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_RSP);
}

messages::HomeplugMessage create_cm_slac_match_req(EvMac const& ev_mac, RunId const& run_id, EvMac const& evse_mac) {
    messages::cm_slac_match_req msg{};
    msg.application_type = defs::COMMON_APPLICATION_TYPE;
    msg.security_type = defs::COMMON_SECURITY_TYPE;
    msg.mvf_length = defs::CM_SLAC_MATCH_REQ_MVF_LENGTH;
    std::copy(ev_mac.begin(), ev_mac.end(), msg.pev_mac);
    std::copy(evse_mac.begin(), evse_mac.end(), msg.evse_mac);
    std::copy(run_id.begin(), run_id.end(), msg.run_id);
    return frame_from(ev_mac, msg, defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ);
}

// The PLC link as a fake: records what the runtime sends, and lets the test raise ready / error /
// received-frame events. Like the real socket client, the events are delivered on the loop thread
// (queued as an action and the loop woken through an eventfd).
class FakeIo final : public SlacIo {
public:
    struct Control {
        std::mutex mutex;
        std::atomic<bool> accept_sends{true};
        std::atomic<bool> ready_on_register{true};
        everest::lib::slac::MacAddress mac{{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
        std::vector<Frame> sent;
        std::atomic<FakeIo*> instance{nullptr};

        std::size_t count(std::uint16_t mmtype) {
            std::lock_guard<std::mutex> guard(mutex);
            std::size_t n = 0;
            for (auto const& f : sent) {
                if (f.get_mmtype() == mmtype) {
                    ++n;
                }
            }
            return n;
        }
    };

    explicit FakeIo(Control& control) : control(control) {
        control.instance.store(this);
    }
    ~FakeIo() override {
        control.instance.store(nullptr);
    }

    bool send(Frame& frame) override {
        if (not control.accept_sends.load()) {
            return false;
        }
        std::lock_guard<std::mutex> guard(control.mutex);
        control.sent.push_back(frame);
        return true;
    }
    const std::uint8_t* mac_address() override {
        return control.mac.data();
    }
    void set_rx_handler(RxHandler handler) override {
        rx = std::move(handler);
    }
    void set_error_handler(ErrorHandler handler) override {
        error = std::move(handler);
    }
    void set_ready_handler(ReadyHandler handler) override {
        ready = std::move(handler);
    }
    bool register_events(everest::lib::io::event::fd_event_handler& handler) override {
        if (not handler.register_event_handler(&wake, [](auto&) {})) {
            return false;
        }
        loop.store(&handler);
        if (control.ready_on_register.load()) {
            fire_ready();
        }
        return true;
    }
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override {
        loop.store(nullptr);
        return handler.unregister_event_handler(&wake);
    }

    // Test-side drivers, any thread.
    bool fire_ready() {
        return post([this] {
            if (ready) {
                ready();
            }
        });
    }
    bool fire_error(bool on_error, std::string detail) {
        return post([this, on_error, detail = std::move(detail)] {
            if (error) {
                error(on_error, detail);
            }
        });
    }
    bool inject(Frame frame) {
        return post([this, frame = std::move(frame)] {
            if (rx) {
                rx(frame);
            }
        });
    }

private:
    bool post(std::function<void()> fn) {
        auto* handler = loop.load();
        if (handler == nullptr) {
            return false;
        }
        handler->add_action(std::move(fn));
        wake.notify();
        return true;
    }

    Control& control;
    std::atomic<everest::lib::io::event::fd_event_handler*> loop{nullptr};
    everest::lib::io::event::event_fd wake;
    RxHandler rx;
    ErrorHandler error;
    ReadyHandler ready;
};

struct Fault {
    std::string type;
    std::string sub_type;
    std::string message;
};

class RecordingSink final : public SlacSink {
public:
    // Optional hooks, may throw. A hook that throws fails the publication before it is recorded as
    // delivered: `states` and `dlink` hold what the consumer received, `attempted_states` every state
    // the runtime tried to hand over.
    std::function<void(D3State)> on_state;
    std::function<void()> on_telemetry;

    void publish_state(D3State state) override {
        record([&] { attempted_states.push_back(state); });
        if (on_state) {
            on_state(state);
        }
        record([&] { states.push_back(state); });
    }
    void publish_dlink_ready(bool ready) override {
        record([&] { dlink.push_back(ready); });
    }
    void publish_ev_mac_address(std::string const& mac) override {
        record([&] { macs.push_back(mac); });
    }
    void request_error_routine() override {
        record([&] { ++error_routines; });
    }
    void raise_fault(std::string const& type, std::string const& sub_type, std::string const& message) override {
        record([&] { faults.push_back({type, sub_type, message}); });
    }
    void clear_fault(std::string const& type) override {
        record([&] { cleared.push_back(type); });
    }
    void publish_telemetry(std::string const&, std::string const&, std::string const&) override {
        if (on_telemetry) {
            on_telemetry();
        }
        record([&] { ++telemetry; });
    }
    void log(LogLevel level, std::string const& text) override {
        record([&] { logs.emplace_back(level, text); });
    }

    template <typename Predicate> bool wait_for(std::chrono::milliseconds timeout, Predicate&& predicate) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return predicate(); });
    }
    // Predicates for wait_for (called with the mutex held).
    bool has_fault(std::string const& type, std::string const& needle = "") const {
        for (auto const& f : faults) {
            if (f.type == type and f.message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    bool has_cleared(std::string const& type) const {
        for (auto const& c : cleared) {
            if (c == type) {
                return true;
            }
        }
        return false;
    }
    bool has_state(D3State state) const {
        for (auto const& s : states) {
            if (s == state) {
                return true;
            }
        }
        return false;
    }
    bool has_log(LogLevel level, std::string const& needle) const {
        for (auto const& [l, text] : logs) {
            if (l == level and text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    std::optional<D3State> last_state() const {
        return states.empty() ? std::nullopt : std::optional<D3State>{states.back()};
    }
    std::size_t count_state(D3State state) const {
        return static_cast<std::size_t>(std::count(states.begin(), states.end(), state));
    }
    std::size_t count_attempted(D3State state) const {
        return static_cast<std::size_t>(std::count(attempted_states.begin(), attempted_states.end(), state));
    }

    std::mutex mutex;
    std::vector<D3State> states;
    std::vector<D3State> attempted_states;
    std::vector<bool> dlink;
    std::vector<std::string> macs;
    std::vector<Fault> faults;
    std::vector<std::string> cleared;
    std::vector<std::pair<LogLevel, std::string>> logs;
    int error_routines{0};
    int telemetry{0};

private:
    template <typename Fn> void record(Fn&& fn) {
        {
            std::lock_guard<std::mutex> guard(mutex);
            fn();
        }
        cv.notify_all();
    }
    std::condition_variable cv;
};

constexpr auto WAIT = 2000ms;
constexpr char const* COMMUNICATION_FAULT = "generic/CommunicationFault";
constexpr char const* VENDOR_ERROR = "generic/VendorError";

// The framework's three threads: init() and shutdown() on the test thread, ready() on its own.
struct Harness {
    FakeIo::Control io;
    RecordingSink sink;
    SlacRuntimeConfig config;
    std::unique_ptr<SlacRuntime> runtime;
    std::thread ready_thread;

    Harness() {
        config.device = "fake0";
        config.io_bring_up_timeout = 150ms;
        config.loop_exit_timeout = 1000ms;
        // Fast machine: Init leaves in a few ms, CM_SET_KEY retries every 20 ms.
        config.slac.request_info_delay = 1ms;
        config.slac.set_key_timeout = 20ms;
        config.slac.set_key_max_attempts = 3;
        config.slac.slac_init_timeout = 5000ms;
        config.slac.chip_reset.enabled = false;
        config.slac.reset_instead_of_fail = false;
        config.slac.ac_mode_five_percent = false;
    }
    ~Harness() {
        if (runtime) {
            runtime->shutdown();
        }
        if (ready_thread.joinable()) {
            ready_thread.join();
        }
    }
    void create() {
        runtime = std::make_unique<SlacRuntime>(
            config, [this](std::string const&) { return std::make_unique<FakeIo>(io); }, sink);
    }
    void start_ready_thread() {
        ready_thread = std::thread([this] { runtime->ready(); });
    }
    bool wait_for_frames(std::uint16_t mmtype, std::size_t n, std::chrono::milliseconds budget = WAIT) {
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            if (io.count(mmtype) >= n) {
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }
        return io.count(mmtype) >= n;
    }
    bool wait_for_set_key_requests(std::size_t n, std::chrono::milliseconds budget = WAIT) {
        return wait_for_frames(SET_KEY_REQ, n, budget);
    }
    // Machine running and in Idle: CM_SET_KEY.CNF consumed. Idle is published as UNMATCHED already
    // at Reset, so wait one retry interval for the CNF instead of a state.
    bool reach_idle() {
        if (not wait_for_set_key_requests(1)) {
            return false;
        }
        if (not io.instance.load()->inject(create_cm_set_key_cnf())) {
            return false;
        }
        std::this_thread::sleep_for(60ms);
        return true;
    }
    // Idle -> Matching -> one full matching session with a fake EV -> Matched, link up. Link
    // detection is off in the harness config, so Matched raises dlink_ready(true) at once.
    bool reach_matched() {
        EvMac const ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
        RunId run_id{};
        for (std::size_t i = 0; i < run_id.size(); ++i) {
            run_id[i] = static_cast<std::uint8_t>(0x11 + i);
        }
        auto* link = io.instance.load();
        if (not runtime->enter_bcd() or not sink.wait_for(WAIT, [&] { return sink.has_state(D3State::Matching); })) {
            return false;
        }
        if (not link->inject(create_cm_slac_parm_req(ev_mac, run_id)) or not wait_for_frames(SLAC_PARM_CNF, 1)) {
            return false;
        }
        if (not link->inject(create_cm_start_atten_char_ind(ev_mac, run_id))) {
            return false;
        }
        for (std::size_t i = 0; i < defs::CM_SLAC_PARM_CNF_NUM_SOUNDS; ++i) {
            if (not link->inject(create_cm_atten_profile_ind(ev_mac, static_cast<std::uint8_t>(0xA0 + i)))) {
                return false;
            }
        }
        // Sounding may run to its TT_EVSE_match_MNBC timeout on the wall clock before the IND goes out.
        if (not wait_for_frames(ATTEN_CHAR_IND, 1, 3000ms)) {
            return false;
        }
        if (not link->inject(create_cm_atten_char_rsp(ev_mac, run_id)) or
            not link->inject(create_cm_slac_match_req(ev_mac, run_id, io.mac))) {
            return false;
        }
        return sink.wait_for(WAIT, [&] { return sink.has_state(D3State::Matched) and not sink.dlink.empty(); }) and
               sink.dlink.back();
    }
    bool ready_thread_finished(std::chrono::milliseconds budget) {
        // join with a bound: the thread must have returned from ready()
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            if (finished.load()) {
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }
        return finished.load();
    }
    void start_ready_thread_tracked() {
        ready_thread = std::thread([this] {
            runtime->ready();
            finished.store(true);
        });
    }
    std::atomic<bool> finished{false};
};

TEST(SlacRuntime, IoReadyDuringBringUpThenGlobalReadyStartsTheMachineOnce) {
    Harness h;
    h.create();
    h.runtime->init(); // fake fires ready as soon as it is registered: bring-up settles at once
    EXPECT_FALSE(h.sink.has_log(LogLevel::Info, "Starting the SLAC state machine"))
        << "the machine must not start before global ready";
    h.start_ready_thread();
    ASSERT_TRUE(h.wait_for_set_key_requests(1)) << "the machine did not start after global ready";
    EXPECT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_state(D3State::Unmatched); }));
    EXPECT_TRUE(h.sink.faults.empty());
}

TEST(SlacRuntime, BringUpTimeoutRaisesFaultAndALateIoReadyClearsItAndStartsTheMachine) {
    Harness h;
    h.io.ready_on_register.store(false);
    h.create();
    auto const t0 = std::chrono::steady_clock::now();
    h.runtime->init(); // returns after io_bring_up_timeout with a fault
    EXPECT_GE(std::chrono::steady_clock::now() - t0, h.config.io_bring_up_timeout);
    ASSERT_TRUE(h.sink.has_fault(COMMUNICATION_FAULT, "neither ready nor an error"));
    h.start_ready_thread();
    EXPECT_FALSE(h.wait_for_set_key_requests(1, 200ms)) << "machine started although the PLC I/O never came up";
    ASSERT_TRUE(h.io.instance.load()->fire_ready());
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_cleared(COMMUNICATION_FAULT); }));
    EXPECT_TRUE(h.wait_for_set_key_requests(1)) << "machine did not start once the PLC I/O came up";
}

TEST(SlacRuntime, CommandsBeforeTheMachineRunsAreRefusedAndLogged) {
    Harness h;
    h.create();
    h.runtime->init();
    // PLC I/O is up, but global ready has not happened: the command must not vanish silently.
    EXPECT_FALSE(h.runtime->enter_bcd());
    EXPECT_TRUE(h.sink.has_log(LogLevel::Warning, "enter_bcd"));
    h.start_ready_thread();
    ASSERT_TRUE(h.wait_for_set_key_requests(1));
    EXPECT_TRUE(h.runtime->enter_bcd());
}

// A command whose publication throws is fatal for the loop. The consumer had just been told
// MATCHING (or the publication of it failed, which is the same doubt); it must be told UNMATCHED
// before the machine goes away, through the machine's own reset path when that still works.
TEST(SlacRuntime, AThrowingPublisherEndsTheLoopWithAFaultAndUnmatched) {
    Harness h;
    h.sink.on_state = [](D3State state) {
        if (state == D3State::Matching) {
            throw std::runtime_error("publisher exploded");
        }
    };
    h.create();
    h.runtime->init();
    h.start_ready_thread_tracked();
    ASSERT_TRUE(h.reach_idle());
    auto const unmatched_before = h.sink.count_state(D3State::Unmatched);
    ASSERT_TRUE(h.runtime->enter_bcd());
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_fault(COMMUNICATION_FAULT, "enter_bcd"); }));
    EXPECT_TRUE(h.sink.has_fault(COMMUNICATION_FAULT, "publisher exploded"));
    EXPECT_TRUE(h.ready_thread_finished(WAIT)) << "the loop did not end after the fatal command";
    ASSERT_EQ(h.sink.count_attempted(D3State::Matching), 1U) << "the throwing publication was not the MATCHING one";
    EXPECT_EQ(h.sink.count_state(D3State::Unmatched), unmatched_before + 1)
        << "the consumer was not told UNMATCHED after the failed MATCHING";
    EXPECT_EQ(h.sink.last_state(), D3State::Unmatched);
    EXPECT_FALSE(h.sink.has_log(LogLevel::Error, "could not publish")) << "the reset path should have done it";
    EXPECT_FALSE(h.runtime->enter_bcd()) << "commands must be refused after the fault";
}

// The start itself publishes UNMATCHED (Reset). From ready() that runs on the framework's ready
// thread outside any loop catch handler; a throw there must end in a fault, not terminate the
// process.
TEST(SlacRuntime, AThrowingPublisherAtTheMachineStartEndsInAFault) {
    Harness h;
    h.sink.on_state = [](D3State) { throw std::runtime_error("publisher exploded"); };
    h.create();
    h.runtime->init();
    h.start_ready_thread_tracked();
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_fault(COMMUNICATION_FAULT, "start failed"); }));
    EXPECT_TRUE(h.sink.has_fault(COMMUNICATION_FAULT, "publisher exploded"));
    EXPECT_TRUE(h.ready_thread_finished(WAIT)) << "ready() did not return after the failed start";
    EXPECT_FALSE(h.runtime->enter_bcd());
}

// The consumer has a link. A publication on the way out of Matched fails once; the consumer must
// still end up with the link down and UNMATCHED, by hand if the machine's reset has nothing left
// to publish.
TEST(SlacRuntime, AFatalCommandInMatchedTakesTheLinkDownAndPublishesUnmatched) {
    Harness h;
    // Armed only once Matched is reached: Reset and Idle publish UNMATCHED as well.
    std::atomic<int> explosions_left{0};
    h.sink.on_state = [&explosions_left](D3State state) {
        if (state == D3State::Unmatched and explosions_left.load() > 0 and explosions_left.fetch_sub(1) > 0) {
            throw std::runtime_error("publisher exploded");
        }
    };
    h.create();
    h.runtime->init();
    h.start_ready_thread_tracked();
    ASSERT_TRUE(h.reach_idle());
    ASSERT_TRUE(h.reach_matched());
    explosions_left.store(1);
    ASSERT_TRUE(h.runtime->dlink_terminate()); // a reset for the machine; the fault names that command
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_fault(COMMUNICATION_FAULT, "publisher exploded"); }));
    EXPECT_TRUE(h.sink.has_fault(COMMUNICATION_FAULT, "handling reset"));
    EXPECT_TRUE(h.ready_thread_finished(WAIT)) << "the loop did not end after the fatal command";
    EXPECT_EQ(h.sink.last_state(), D3State::Unmatched) << "the consumer was left believing MATCHED";
    ASSERT_FALSE(h.sink.dlink.empty());
    EXPECT_FALSE(h.sink.dlink.back()) << "the link was left up";
    EXPECT_FALSE(h.runtime->enter_bcd());
}

// When the publisher keeps throwing, the machine's reset cannot get UNMATCHED out either. The fatal
// path then tries by hand, survives that throw as well, and says so.
TEST(SlacRuntime, APersistentlyThrowingPublisherIsRetriedByHandAndLogged) {
    Harness h;
    std::atomic<bool> exploding{false};
    h.sink.on_state = [&exploding](D3State state) {
        if (state == D3State::Matching) {
            exploding.store(true);
        }
        if (exploding.load()) {
            throw std::runtime_error("publisher exploded");
        }
    };
    h.create();
    h.runtime->init();
    h.start_ready_thread_tracked();
    ASSERT_TRUE(h.reach_idle());
    auto const unmatched_attempts_before = h.sink.count_attempted(D3State::Unmatched);
    ASSERT_TRUE(h.runtime->enter_bcd());
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_fault(COMMUNICATION_FAULT, "enter_bcd"); }));
    EXPECT_TRUE(h.ready_thread_finished(WAIT)) << "the loop did not end after the fatal command";
    EXPECT_TRUE(h.sink.has_log(LogLevel::Error, "teardown failed"));
    EXPECT_TRUE(h.sink.has_log(LogLevel::Error, "could not publish UNMATCHED"));
    // Once through the machine's reset, once by hand.
    EXPECT_EQ(h.sink.count_attempted(D3State::Unmatched), unmatched_attempts_before + 2);
    EXPECT_TRUE(h.sink.has_fault(COMMUNICATION_FAULT, "publisher exploded"));
}

TEST(SlacRuntime, IoErrorTearsDownAndRecoveryRestartsTheMachine) {
    Harness h;
    h.create();
    h.runtime->init();
    h.start_ready_thread();
    ASSERT_TRUE(h.wait_for_set_key_requests(1));
    ASSERT_TRUE(h.io.instance.load()->fire_error(true, "cable unplugged"));
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_fault(COMMUNICATION_FAULT, "cable unplugged"); }));
    EXPECT_FALSE(h.runtime->enter_bcd()) << "commands must be refused while the PLC I/O is in error";
    auto const requests_before = h.io.count(SET_KEY_REQ);
    ASSERT_TRUE(h.io.instance.load()->fire_error(false, ""));
    ASSERT_TRUE(h.io.instance.load()->fire_ready());
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_cleared(COMMUNICATION_FAULT); }));
    EXPECT_TRUE(h.wait_for_set_key_requests(requests_before + 1)) << "machine did not restart after recovery";
}

// A failing transmit path is reported by the link like a socket error, but its recovery comes as a
// ready callback alone: the link reopens the socket, there is no error-cleared notification first.
// Ready by itself must clear the fault and restart the machine.
TEST(SlacRuntime, ATransmitFaultIsTornDownAndReadyAloneRestartsTheMachine) {
    Harness h;
    h.create();
    h.runtime->init();
    h.start_ready_thread();
    ASSERT_TRUE(h.wait_for_set_key_requests(1));
    ASSERT_TRUE(h.io.instance.load()->fire_error(true, "PLC frame transmission failing"));
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_fault(COMMUNICATION_FAULT, "transmission failing"); }));
    EXPECT_FALSE(h.runtime->enter_bcd()) << "commands must be refused while the transmit path is down";
    auto const requests_before = h.io.count(SET_KEY_REQ);
    ASSERT_TRUE(h.io.instance.load()->fire_ready());
    ASSERT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_cleared(COMMUNICATION_FAULT); }));
    EXPECT_TRUE(h.wait_for_set_key_requests(requests_before + 1)) << "machine did not restart on the new connection";
    EXPECT_TRUE(h.runtime->enter_bcd());
}

TEST(SlacRuntime, ShutdownDuringTheLoopStopsItAndIsIdempotent) {
    Harness h;
    h.create();
    h.runtime->init();
    h.start_ready_thread_tracked();
    ASSERT_TRUE(h.wait_for_set_key_requests(1));
    h.runtime->shutdown();
    EXPECT_TRUE(h.ready_thread_finished(WAIT)) << "ready() did not return after shutdown()";
    h.runtime->shutdown(); // second call: nothing left to do, must not block or throw
    EXPECT_FALSE(h.runtime->enter_bcd());
    EXPECT_FALSE(h.sink.has_log(LogLevel::Error, "did not stop within"));
}

TEST(SlacRuntime, ShutdownBeforeGlobalReadyKeepsTheLoopFromStarting) {
    Harness h;
    h.create();
    h.runtime->init();
    h.runtime->shutdown();
    h.runtime->ready(); // must return at once
    EXPECT_TRUE(h.sink.has_log(LogLevel::Info, "not starting the event loop"));
    EXPECT_EQ(h.io.count(SET_KEY_REQ), 0U);
}

TEST(SlacRuntime, TheMacIsTakenFromTheLinkWhenItComesUp) {
    Harness h;
    h.io.ready_on_register.store(false);
    h.io.mac = {{0, 0, 0, 0, 0, 0}}; // not readable at construction
    h.create();
    h.runtime->init();
    h.start_ready_thread();
    h.io.mac = {{0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE}};
    ASSERT_TRUE(h.io.instance.load()->fire_ready());
    ASSERT_TRUE(h.wait_for_set_key_requests(1));
    EXPECT_EQ(h.runtime->evse_mac(), h.io.mac);
}

TEST(SlacRuntime, ARejectedAmplitudeMapRaisesVendorErrorAndDisablesInitiation) {
    auto const path = std::filesystem::temp_directory_path() / "slac_runtime_test_amp_map.yaml";
    {
        std::ofstream file(path);
        file << "carriers: 0\n";
    }
    Harness h;
    h.config.initiate_amp_map = true;
    h.config.amp_map_file = path.string();
    h.create();
    h.runtime->init();
    std::filesystem::remove(path);
    EXPECT_TRUE(h.sink.has_fault(VENDOR_ERROR, "amp_map_file"));
    EXPECT_FALSE(h.runtime->amp_map_initiation_enabled());
    EXPECT_TRUE(h.sink.has_log(LogLevel::Error, "rejected"));
}

TEST(SlacRuntime, TheBuiltInAmplitudeMapIsUsedWhenNoFileIsConfigured) {
    Harness h;
    h.config.initiate_amp_map = true;
    h.create();
    h.runtime->init();
    EXPECT_TRUE(h.runtime->amp_map_initiation_enabled());
    EXPECT_TRUE(h.sink.faults.empty());
    EXPECT_TRUE(h.sink.has_log(LogLevel::Info, "no reduction"));
}

TEST(SlacRuntime, ATelemetryFailureDoesNotStopMatching) {
    Harness h;
    h.config.telemetry_enabled = true;
    h.sink.on_telemetry = [] { throw std::runtime_error("codec exploded"); };
    h.create();
    h.runtime->init();
    h.start_ready_thread();
    ASSERT_TRUE(h.reach_idle());
    ASSERT_TRUE(h.runtime->enter_bcd());
    EXPECT_TRUE(h.sink.wait_for(WAIT, [&] { return h.sink.has_state(D3State::Matching); }))
        << "matching did not start although only telemetry failed";
    EXPECT_TRUE(h.sink.has_log(LogLevel::Warning, "not published"));
    EXPECT_TRUE(h.sink.faults.empty());
}

} // namespace
