// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <framework/ModuleAdapter.hpp>

#include "ISO15118_evImpl.hpp"
#include "fake_controller.hpp"
#include "module_driver.hpp"

namespace {

using namespace std::chrono_literals;
using iso15118::ev::config::PausedSession;

using module_driver::deadline;
using module_driver::Driver;
using module_driver::eventually;
using module_driver::TestImpl;

PausedSession paused_session(std::uint8_t id) {
    return PausedSession{{id, 0, 0, 0, 0, 0, 0, 0},
                         iso15118::ProtocolId::ISO15118_20,
                         iso15118::v2gtp::Security::NO_TRANSPORT_SECURITY,
                         {}};
}

// Counts v2g_session_finished publications; can hold the worker inside the one with a given number.
class Finishes {
public:
    void published() {
        std::unique_lock lock(mutex);
        const int number = ++count;
        changed.notify_all();
        changed.wait(lock, [&] { return held != number; });
        if (number == failing) {
            throw std::runtime_error("fake publish failure");
        }
    }

    // The publication with this number throws once counted.
    void fail_publication(int number) {
        std::lock_guard lock(mutex);
        failing = number;
    }

    // Holding a later publication releases the one held before it.
    void hold_publication(int number) {
        std::lock_guard lock(mutex);
        held = number;
        changed.notify_all();
    }

    void hold_first_publication() {
        hold_publication(1);
    }

    void release() {
        hold_publication(0);
    }

    int seen() {
        std::lock_guard lock(mutex);
        return count;
    }

    bool wait_for(int expected) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, deadline, [&] { return count >= expected; });
    }

    // The count once a duplicate publication would have had time to land.
    int settled() {
        std::this_thread::sleep_for(settle_time);
        return seen();
    }

private:
    static constexpr auto settle_time = 200ms;
    std::mutex mutex;
    std::condition_variable changed;
    int count{0};
    int held{0};
    int failing{0};
};

class SessionHandoff : public ::testing::Test {
protected:
    SessionHandoff() {
        fake_controller::reset();
        conf.device = "lo";
        conf.evcc_id = "EVTESTID01";
        conf.control_mode = "Dynamic";
        conf.supported_protocols = "ISO15118_20";
        conf.cpd_rounds = 1;

        adapter.publish = [this](const std::string&, const std::string& variable, const Value&) {
            if (variable == "v2g_session_finished") {
                finishes.published();
            }
        };
        module_driver::stub_lookups(adapter);

        auto owned = std::make_unique<TestImpl>(&adapter, mod, impl_conf);
        impl = owned.get();
        module = std::make_unique<module::Ev15118>(info, std::move(owned), conf);
        mod.set(module.get());
        Driver{info}.ready(*impl);
        module_driver::set_sane_dc_params(*impl);
    }

    ~SessionHandoff() override {
        finishes.release();
        module.reset();
    }

    bool start(double departure_time = 0, double e_amount = 0) {
        auto mode = types::iso15118::EnergyTransferMode::DC;
        types::iso15118::SelectedPaymentOption payment{};
        return impl->handle_start_charging(mode, payment, departure_time, e_amount);
    }

    void report_cp_state(types::iso15118::CpState cp_state) {
        impl->handle_cp_state_changed(cp_state);
    }

    static bool constructed(std::size_t count) {
        return eventually([count] { return fake_controller::constructions().size() >= count; });
    }

    Finishes finishes;
    module::Conf conf{};
    module::ev::Conf impl_conf{};
    ModuleInfo info{};
    Everest::ModuleAdapter adapter{};
    Everest::PtrContainer<module::Ev15118> mod;
    TestImpl* impl{nullptr};
    std::unique_ptr<module::Ev15118> module;
};

// The session's loop has already ended with SessionStop(Pause) when the stop lands, so the
// Controller can no longer act on it; only the module can discard the pause.
TEST_F(SessionHandoff, StopLandingAfterTheLoopEndedDiscardsThePause) {
    fake_controller::set_paused_session(paused_session(1));
    fake_controller::set_loop_body([this] { impl->handle_stop_charging(); });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    fake_controller::set_loop_body(nullptr);
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(2));
    EXPECT_FALSE(fake_controller::constructions().at(1).resume.has_value());
}

TEST_F(SessionHandoff, AbortLandingAfterTheLoopEndedDiscardsThePause) {
    fake_controller::set_paused_session(paused_session(1));
    fake_controller::set_loop_body([this] { impl->handle_abort_charging(); });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    fake_controller::set_loop_body(nullptr);
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(2));
    EXPECT_FALSE(fake_controller::constructions().at(1).resume.has_value());
}

TEST_F(SessionHandoff, PausedSessionIsResumedByTheNextStart) {
    fake_controller::set_loop_body([this] {
        impl->handle_pause_charging();
        fake_controller::set_paused_session(paused_session(7));
    });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));
    EXPECT_EQ(fake_controller::pause_requests(), 1U);

    fake_controller::set_loop_body(nullptr);
    fake_controller::set_paused_session(std::nullopt);
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(2));
    const auto resume = fake_controller::constructions().at(1).resume;
    ASSERT_TRUE(resume.has_value());
    EXPECT_EQ(resume->session_id, paused_session(7).session_id);
}

// The worker is held inside the first session's finish publication, so it cannot wake between
// the cancellation and the next accepted start.
TEST_F(SessionHandoff, StartCancelledBeforeTheWorkerWakesGetsItsOwnFinish) {
    finishes.hold_first_publication();
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    ASSERT_TRUE(start());
    impl->handle_stop_charging();
    int seen_by_next_session = -1;
    fake_controller::set_loop_body([this, &seen_by_next_session] { seen_by_next_session = finishes.seen(); });
    ASSERT_TRUE(start());
    finishes.release();

    ASSERT_TRUE(constructed(2));
    finishes.wait_for(3);
    EXPECT_EQ(seen_by_next_session, 2);
    EXPECT_EQ(finishes.seen(), 3);
    EXPECT_EQ(fake_controller::constructions().size(), 2U);
}

TEST_F(SessionHandoff, EveryCancelledStartGetsItsOwnFinish) {
    finishes.hold_first_publication();
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    ASSERT_TRUE(start());
    impl->handle_stop_charging();
    ASSERT_TRUE(start());
    impl->handle_abort_charging();
    ASSERT_TRUE(start());
    impl->handle_pause_charging();
    int seen_by_next_session = -1;
    fake_controller::set_loop_body([this, &seen_by_next_session] { seen_by_next_session = finishes.seen(); });
    ASSERT_TRUE(start());
    finishes.release();

    ASSERT_TRUE(constructed(2));
    finishes.wait_for(5);
    EXPECT_EQ(seen_by_next_session, 4);
    EXPECT_EQ(finishes.seen(), 5);
}

// The worker has taken the pending start together with an earlier debt and is held publishing
// that debt when the start is cancelled and another one accepted.
TEST_F(SessionHandoff, StartCancelledAfterTheWorkerTookItIsFinishedBeforeTheNextSession) {
    finishes.hold_first_publication();
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));
    ASSERT_TRUE(start());
    impl->handle_stop_charging();
    ASSERT_TRUE(start());
    finishes.hold_publication(2);
    ASSERT_TRUE(finishes.wait_for(2));

    impl->handle_stop_charging();
    int seen_by_next_session = -1;
    fake_controller::set_loop_body([this, &seen_by_next_session] { seen_by_next_session = finishes.seen(); });
    ASSERT_TRUE(start());
    finishes.release();

    ASSERT_TRUE(constructed(2));
    ASSERT_TRUE(finishes.wait_for(4));
    EXPECT_EQ(seen_by_next_session, 3);
    EXPECT_EQ(finishes.settled(), 4);
    EXPECT_EQ(fake_controller::constructions().size(), 2U);
}

// The worker takes a debt together with a pending start; the debt's publication throws.
TEST_F(SessionHandoff, ThrowingDebtPublicationStillRunsThePendingStart) {
    finishes.hold_first_publication();
    finishes.fail_publication(2);
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    ASSERT_TRUE(start());
    impl->handle_stop_charging();
    ASSERT_TRUE(start());
    finishes.release();

    ASSERT_TRUE(constructed(2));
    ASSERT_TRUE(finishes.wait_for(3));
}

TEST_F(SessionHandoff, ThrowingLoopEndsTheSessionWithoutPauseAndFreesTheModule) {
    fake_controller::set_paused_session(paused_session(1));
    fake_controller::set_loop_body([this] {
        impl->handle_stop_charging();
        throw std::runtime_error("fake loop failure");
    });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    // Each command takes the session lock; the failed session must not have left it held.
    auto cp_state = types::iso15118::CpState::C;
    impl->handle_cp_state_changed(cp_state);

    // A pause after the failure is kept: the failed session's stop does not reach past it.
    fake_controller::set_loop_body(nullptr);
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(2));
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(3));
    EXPECT_FALSE(fake_controller::constructions().at(1).resume.has_value());
    EXPECT_TRUE(fake_controller::constructions().at(2).resume.has_value());
}

// start_charging rejects the construction failures it can check; this one comes after it accepted,
// as when the HLC interface goes away in between.
TEST_F(SessionHandoff, ThrowingConstructionStillFinishesAndAcceptsTheNextStart) {
    fake_controller::set_throw_on_construct(true);
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    fake_controller::set_throw_on_construct(false);
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(2));
    EXPECT_EQ(finishes.settled(), 2);
    EXPECT_EQ(fake_controller::constructions().size(), 1U);
}

// The stop lands after the failed construction released the session and before the worker
// reports the failure.
TEST_F(SessionHandoff, StopWhileConstructionFailsFinishesTheStartOnce) {
    const auto stop_landed = std::make_shared<std::atomic_bool>(false);
    fake_controller::set_throw_on_construct(true, [this, stop_landed] {
        *stop_landed = true;
        impl->handle_stop_charging();
    });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));
    EXPECT_TRUE(*stop_landed);
    EXPECT_EQ(finishes.settled(), 1);

    fake_controller::set_throw_on_construct(false);
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(1));
    ASSERT_TRUE(finishes.wait_for(2));
    EXPECT_EQ(finishes.settled(), 2);
}

TEST_F(SessionHandoff, StartChargingStatesItsEnergyAndDepartureTime) {
    const auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(start(3600, 20));
    ASSERT_TRUE(constructed(1));
    const auto request = fake_controller::constructions().at(0).energy_request;
    EXPECT_FLOAT_EQ(request.energy_wh, 20000.0f);
    ASSERT_TRUE(request.departure.has_value());
    EXPECT_GE(*request.departure, before + 3600s);
    EXPECT_LE(*request.departure, std::chrono::steady_clock::now() + 3600s);
}

TEST_F(SessionHandoff, ZeroEnergyAndDepartureTimeStateNothing) {
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(1));
    const auto request = fake_controller::constructions().at(0).energy_request;
    EXPECT_EQ(request.energy_wh, 0.0f);
    EXPECT_FALSE(request.departure.has_value());
}

TEST_F(SessionHandoff, NegativeOrNotFiniteEnergyAndDepartureTimeAreRejected) {
    EXPECT_FALSE(start(-1, 0));
    EXPECT_FALSE(start(0, -1));
    EXPECT_FALSE(start(std::numeric_limits<double>::quiet_NaN(), 0));
    EXPECT_FALSE(start(0, std::numeric_limits<double>::infinity()));
    // Finite, but no float holds it in Wh.
    EXPECT_FALSE(start(0, 1e36));
    EXPECT_TRUE(fake_controller::constructions().empty());
}

// [V2G2-737]: CP E or F ends the session without SessionStop, and like an abort leaves nothing to
// resume.
TEST_F(SessionHandoff, CpStateEDuringASessionTerminatesIt) {
    fake_controller::set_paused_session(paused_session(1));
    fake_controller::set_loop_body([this] { report_cp_state(types::iso15118::CpState::E); });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));
    EXPECT_EQ(fake_controller::terminations(), 1U);
    EXPECT_EQ(fake_controller::stop_requests(), 0U);

    fake_controller::set_loop_body(nullptr);
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(2));
    EXPECT_FALSE(fake_controller::constructions().at(1).resume.has_value());
}

TEST_F(SessionHandoff, CpStateFDuringASessionTerminatesIt) {
    fake_controller::set_loop_body([this] { report_cp_state(types::iso15118::CpState::F); });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));
    EXPECT_EQ(fake_controller::terminations(), 1U);
    EXPECT_EQ(fake_controller::stop_requests(), 0U);
}

TEST_F(SessionHandoff, CpStatesBToDLeaveTheSessionRunning) {
    fake_controller::set_paused_session(paused_session(1));
    fake_controller::set_loop_body([this] {
        report_cp_state(types::iso15118::CpState::B);
        report_cp_state(types::iso15118::CpState::C);
        report_cp_state(types::iso15118::CpState::D);
        report_cp_state(types::iso15118::CpState::B);
    });
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));
    EXPECT_EQ(fake_controller::terminations(), 0U);

    fake_controller::set_loop_body(nullptr);
    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(2));
    EXPECT_TRUE(fake_controller::constructions().at(1).resume.has_value());
}

// The worker is held inside the first session's finish publication, so the next start is still
// requested, its Controller not yet built, when E arrives.
TEST_F(SessionHandoff, CpStateEBeforeTheControllerIsBuiltCancelsTheStart) {
    finishes.hold_first_publication();
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    ASSERT_TRUE(start());
    report_cp_state(types::iso15118::CpState::E);
    finishes.release();

    ASSERT_TRUE(finishes.wait_for(2));
    EXPECT_EQ(finishes.settled(), 2);
    EXPECT_EQ(fake_controller::constructions().size(), 1U);
}

// An accepted BPT start whose Controller is not built yet is built with a later set_bpt_dc_params.
TEST_F(SessionHandoff, BptParamsBeforeTheControllerIsBuiltReachIt) {
    finishes.hold_first_publication();
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    auto mode = types::iso15118::EnergyTransferMode::DC_BPT;
    types::iso15118::SelectedPaymentOption payment{};
    double departure_time = 0;
    double e_amount = 0;
    ASSERT_TRUE(impl->handle_start_charging(mode, payment, departure_time, e_amount));
    types::iso15118::DcEvBPTParameters bpt;
    bpt.discharge_max_power_limit = 5000;
    bpt.discharge_max_current_limit = 20;
    impl->handle_set_bpt_dc_params(bpt);
    finishes.release();

    ASSERT_TRUE(constructed(2));
    const auto params = fake_controller::constructions().at(1).dc_params;
    EXPECT_EQ(params.max_discharge_power, 5000.0f);
    EXPECT_EQ(params.max_discharge_current, 20.0f);
}

TEST_F(SessionHandoff, CpStateEWithoutASessionKeepsThePause) {
    fake_controller::set_paused_session(paused_session(1));
    ASSERT_TRUE(start());
    ASSERT_TRUE(finishes.wait_for(1));

    report_cp_state(types::iso15118::CpState::E);
    EXPECT_EQ(fake_controller::terminations(), 0U);

    ASSERT_TRUE(start());
    ASSERT_TRUE(constructed(2));
    EXPECT_TRUE(fake_controller::constructions().at(1).resume.has_value());
}

} // namespace
