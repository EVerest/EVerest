// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// ResetChip sub-machine: optional modem reset after the NMK was programmed (Reset -> ResetChip -> Idle).
//
//   Delay --update/timeout / send reset request--> Sent
//   Sent --message(CM_RESET_DEVICE.CNF)--> Received
//   Sent --update / Lumissil (no reply), unsupported modem, or timeout--> Done (exit)
//   Received --update--> Done (exit)
//   outer:  reset --> Reset;  Done exit --> Idle

#include "machine.hpp"

#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/telemetry.hpp>

namespace everest::lib::slac::sm::evse {

namespace {

using DoneExit = ExitPoint<StateId::ResetChipDone>;

std::unique_ptr<InnerState> make_done(SlacMachine& m, Event const& e) {
    return std::make_unique<DoneExit>(m, "Done", EventId::update, e);
}

void send_reset_request(fsm::evse::Context& ctx) {
    if (ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
        messages::qualcomm::cm_reset_device_req reset_req{};
        ctx.log_info("Resetting HW chip using CM_RESET_DEVICE.REQ");
        if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req)) {
            ctx.log_warn("Failed to send CM_RESET_DEVICE.REQ");
        }
    } else if (ctx.modem_vendor == defs::ModemVendor::Lumissil) {
        messages::lumissil::nscm_reset_device_req reset_req{};
        if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req)) {
            ctx.log_warn("Failed to send NSCM_RESET_DEVICE.REQ");
        }
    } else {
        ctx.log_warn("Chip reset is not supported on this modem, skipping it");
    }
}

struct Received : InnerLevelState<StateId::ResetChipReceived> {
    explicit Received(SlacMachine& m) : InnerLevelState(m, "Received") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::update;
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update)) {
            return transition(make_done(machine, e));
        }
        return unhandled();
    }
};

struct Sent : InnerLevelState<StateId::ResetChipSent> {
    explicit Sent(SlacMachine& m) : InnerLevelState(m, "Sent") {
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::message});
    }
    void enter() override {
        state_timer.duration = ctx.slac_config.chip_reset.timeout;
        state_timer.arm(ctx.current_time);
    }
    void deadlines(earliest_deadline& next) const override {
        state_timer.deadlines(next);
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::message)) {
            if (e.payload->get_mmtype() == (defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_CNF)) {
                return transition(std::make_unique<Received>(machine));
            }
            return unhandled();
        }
        if (not e.is(EventId::update)) {
            return unhandled();
        }
        if (state_timer.state_timeout(ctx.current_time)) {
            ctx.log_error("Reset timeout, no CM_RESET_DEVICE.CNF received - failed to confirm the chip reset");
            return transition(make_done(machine, e));
        }
        // Only Qualcomm answers the reset (handled via Received): the Lumissil CG5317 does not reply
        // to the reset packet, and an unsupported chip was not reset at all.
        if (ctx.modem_vendor != defs::ModemVendor::Qualcomm) {
            return transition(make_done(machine, e));
        }
        return unhandled();
    }

private:
    StateTimer state_timer;
};

struct Delay : InnerLevelState<StateId::ResetChipDelay> {
    explicit Delay(SlacMachine& m) : InnerLevelState(m, "Delay") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::update;
    }
    void enter() override {
        state_timer.duration = ctx.slac_config.chip_reset.delay;
        state_timer.arm(ctx.current_time);
    }
    void deadlines(earliest_deadline& next) const override {
        state_timer.deadlines(next);
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update) and state_timer.state_timeout(ctx.current_time)) {
            send_reset_request(ctx);
            return transition(std::make_unique<Sent>(machine));
        }
        return unhandled();
    }

private:
    StateTimer state_timer;
};

struct ResetChip : TopLevelState<StateId::ResetChip> {
    explicit ResetChip(SlacMachine& m) : TopLevelState(m, "ResetChip_def") {
    }
    void enter() override {
        ctx.enter_state(SlacState::ResetChip, D3State::Unmatched);
        region.start(std::make_unique<Delay>(machine));
    }
    void leave() override {
        region.stop();
    }
    Result feed(Event const& e) override {
        if (region.feed(e)) {
            return handled();
        }
        if (e.is(EventId::reset)) {
            return transition(make_reset(machine));
        }
        if (e.is(EventId::update) and region.current_id() == static_cast<int>(StateId::ResetChipDone)) {
            return transition(make_idle(machine));
        }
        return unhandled();
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::reset or region.current().reacts_to(e);
    }
    bool is_submachine() const override {
        return true;
    }
    void traverse_children(Visitor& visitor, std::size_t depth) const override {
        region.traverse(visitor, depth);
    }

private:
    InnerRegion region;
};

} // namespace

TopStatePtr make_reset_chip(SlacMachine& machine) {
    return std::make_unique<ResetChip>(machine);
}

} // namespace everest::lib::slac::sm::evse
