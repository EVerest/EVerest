// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EV_SLAC_STATES_OTHERS_HPP
#define EV_SLAC_STATES_OTHERS_HPP

#include <chrono>

#include <everest/slac/fsm/ev/fsm.hpp>

namespace slac::fsm::ev {

struct ResetState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

private:
    /// Leave the EVSE's AVLN by resetting the NMK to a fresh random key, per
    /// [V2G3-A09-121] / [HPGP] "Leaving an AVLN". No-op unless we joined one.
    void leave_logical_network();

    /// Ask the local modem who made it, so link detection knows which vendor
    /// MME to use later. Sent once per FSM lifetime.
    void probe_modem_vendor();

    /// Pick up the answer to probe_modem_vendor().
    void handle_vendor_probe_cnf();
};

struct InitSlacState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

    // only returns true, if valid
    bool check_for_valid_parm_conf();

    // sends out CM_SLAC_PARM.REQ, increases num of tries and returns the timeout until a response is expected
    int send_parm_req();

    std::chrono::time_point<std::chrono::steady_clock> next_timeout;

    int num_of_tries{0};

    uint8_t run_id[8];
};

struct MatchRequestState : public FSMSimpleState {
    MatchRequestState(Context& ctx, SessionParamaters session_parameters);

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

    // return the pointer to the NMK, if valid
    const uint8_t* check_for_valid_match_req_conf();

    // sends out CM_SLAC_PARM.REQ, increases num of tries and returns the timeout until a response is expected
    int send_match_req();

    std::chrono::time_point<std::chrono::steady_clock> next_timeout;

    int num_of_tries{0};

    SessionParamaters session_parameters;
};

struct JoinNetworkState : public FSMSimpleState {
    static constexpr auto SET_KEY_TIMEOUT_MS = 500;
    JoinNetworkState(Context& ctx, const uint8_t* nmk);

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

    // only returns true, if valid
    bool check_for_valid_set_key_conf();

    std::chrono::time_point<std::chrono::steady_clock> timeout;

    uint8_t nmk[slac::defs::NMK_LEN];
};

struct MatchedState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    void enter() final;

    CallbackReturnType callback() final;

    /// Poll state for the vendor LINK_STATUS request while matched.
    bool link_status_req_sent{false};
    /// The data link takes a moment to come up after the match; only a link
    /// that was up and then went away is a loss of communication.
    bool link_was_up{false};
    std::chrono::time_point<std::chrono::steady_clock> link_up_deadline;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;
};

struct FailedState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    void enter() final;

    CallbackReturnType callback() final;
    HandleEventReturnType handle_event(AllocatorType&, Event) final;

private:
    // [V2G3-A09-124]: when to restart the matching process, TT_matching_rate
    // after this state was entered.
    std::chrono::steady_clock::time_point retry_at{};
};

} // namespace slac::fsm::ev

#endif // EV_SLAC_STATES_OTHERS_HPP
