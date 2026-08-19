// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_STATES_OTHERS_HPP
#define EVSE_SLAC_STATES_OTHERS_HPP

#include <chrono>
#include <memory>

#include "../fsm.hpp"

namespace slac::fsm::evse {

struct ResetState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

    // returns true if a CM_SET_KEY_CNF reporting success is received
    bool handle_slac_message(slac::messages::HomeplugMessage&);

    // number of CM_SET_KEY.REQ messages sent so far in this reset cycle
    int set_key_attempts{0};
    // time the last CM_SET_KEY.REQ was sent, used to space out retries
    std::chrono::steady_clock::time_point last_attempt_time{};

    // Candidate NMK currently being programmed into the modem.
    // Promoted to slac_config.session_nmk when CM_SET_KEY.CNF succeeds, or else we continue with the old key.
    uint8_t pending_nmk[slac::defs::NMK_LEN]{};
};

struct ResetChipState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

    // for now returns true if CM_RESET_CNF is received
    bool handle_slac_message(slac::messages::HomeplugMessage&);

    bool reset_delay_done{false};
    bool chip_reset_has_been_sent{false};

    enum class SubState {
        DELAY,
        SEND_RESET,
        DONE,
    } sub_state{SubState::DELAY};
};

struct IdleState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
};

struct MatchedState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    void leave() final;

    CallbackReturnType callback() final;

    bool link_status_req_sent{false};
};

struct FailedState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
};

struct WaitForLinkState : public FSMSimpleState {
    WaitForLinkState(Context& ctx, std::unique_ptr<slac::messages::cm_slac_match_cnf> sent_match_cnf_message);

    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    void enter() final;
    CallbackReturnType callback() final;

    // for now returns true if link up detected is received
    bool handle_slac_message(slac::messages::HomeplugMessage&);

    bool link_status_req_sent{false};
    std::chrono::steady_clock::time_point start_time;
    std::unique_ptr<slac::messages::cm_slac_match_cnf> match_cnf_message;
};

struct InitState : public FSMSimpleState {
    using FSMSimpleState::FSMSimpleState;

    void enter() final;
    HandleEventReturnType handle_event(AllocatorType&, Event) final;

    CallbackReturnType callback() final;

    void handle_slac_message(slac::messages::HomeplugMessage&);

    // For now we are requesting only one version info packet, but probably there will be more in the future.
    enum class SubState {
        QUALCOMM_OP_ATTR,
        LUMISSIL_GET_VERSION,
        DONE,
    } sub_state{SubState::QUALCOMM_OP_ATTR};
};

} // namespace slac::fsm::evse

#endif // EVSE_SLAC_STATES_OTHERS_HPP
