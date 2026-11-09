// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../FsmContext.hpp"
#include "../StateBase.hpp"

#include <chrono>

namespace module {

class Paused final : public StateBase {
public:
    // charger_pause: the charger paused an ISO 15118-20 session. The link stays
    // matched, so the charger's PWM wake-up resumes it with a new session.
    explicit Paused(FsmContext& ctx_, bool charger_pause = false) : StateBase(ctx_), m_charger_pause(charger_pause) {
    }
    // A resume deferred while the paused session tears down wakes the charger
    // best-effort if the session never reports its end.
    static constexpr std::chrono::seconds kDeferredResumeFallback{15};
    // The charger goes to X1 1.5 s after SessionStopRes ([V2G-DC-968]); this
    // bounds the wait for it, counted from the end of the paused session.
    static constexpr std::chrono::seconds kChargerX1Wait{15};
    void enter() override;
    API_types::ev_controller::FsmState get_id() const override {
        return API_types::ev_controller::FsmState::Paused;
    }

protected:
    Result on_event(EventType ev) override;

private:
    bool m_charger_pause;
    // Charger pause only: the steps of EvManager's resume, which waits for the
    // paused session to end, settles 2 s, then waits for running PWM.
    enum class ChargerResume {
        AwaitSessionEnd,
        Settle,
        AwaitPwm,
    };
    ChargerResume m_charger_resume{ChargerResume::AwaitSessionEnd};
    // EV pause only: a resume and the BCB edges it carries. AwaitSessionEnd
    // defers it while the paused session tears down; AwaitX1 holds it while the
    // charger may still run its retain PWM.
    enum class EvResume {
        None,
        AwaitSessionEnd,
        AwaitX1,
    };
    EvResume m_ev_resume{EvResume::None};
    int32_t m_edges{0};
    // EV pause of a live ISO session: the charger answers SessionStop(Pause)
    // with its retain PWM, then X1. SessionLive until the session ends, then
    // Bound until X1 is seen or the bound counted from the session end passes.
    enum class RetainPwm {
        None,
        SessionLive,
        Bound,
    };
    RetainPwm m_retain{RetainPwm::None};

    Result request_resume(int32_t edges);
    Result release_resume();
    void start_x1_bound();
    Result wake_charger();
};

} // namespace module
