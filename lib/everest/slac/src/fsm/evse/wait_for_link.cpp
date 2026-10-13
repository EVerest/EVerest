// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// WaitForLink sub-machine: after CM_SLAC_MATCH.CNF, poll the modem until it reports the AVLN as
// linked (or the link-status timeout elapses). Re-sends the cached CM_SLAC_MATCH.CNF on request.
//
//   Init --completion / query link status--> Lumissil | Qualcomm;  no link status query --> Failed (exit)
//   Lumissil|Qualcomm --update/poll due / query again--> (internal)
//   Lumissil|Qualcomm --message(CM_SLAC_MATCH.REQ) / resend cached CM_SLAC_MATCH.CNF--> (internal)
//   Lumissil|Qualcomm --message(link status: linked)--> Matched (exit)
//   outer:  update/link-status timeout --> Failed; reset, leave_bcd --> Reset;
//           Failed exit --> Failed;  Matched exit --> Matched

#include "../misc.hpp"
#include "link_status.hpp"
#include "machine.hpp"

#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/telemetry.hpp>

namespace everest::lib::slac::sm::evse {

namespace {

bool is_match_req(fsm::evse::Context& ctx, Event const& e) {
    if (not e.is(EventId::message)) {
        return false;
    }
    if (e.payload->get_mmtype() != (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ)) {
        return false;
    }
    auto const msg = e.payload->payload_as<messages::cm_slac_match_req>();
    if (not msg.has_value()) {
        return false;
    }
    if (not ctx.match_confirm_cache.valid) {
        return false;
    }
    auto const source_mac = e.payload->get_src_mac();
    if (source_mac == nullptr) {
        return false;
    }
    if (not wire_pointer_equal(source_mac, ctx.match_confirm_cache.ev_mac)) {
        return false;
    }
    fsm::evse::MatchingSessionData data(ctx.match_confirm_cache.ev_mac, ctx.match_confirm_cache.run_id,
                                        ctx.match_confirm_cache.evse_mac);
    return data.validate_message(*msg);
}

void send_match_cnf(fsm::evse::Context& ctx) {
    if (not ctx.match_confirm_cache.valid) {
        return;
    }
    if (not ctx.send_slac_message(ctx.match_confirm_cache.ev_mac, ctx.match_confirm_cache.message)) {
        ctx.log_warn("Failed to send cached CM_SLAC_MATCH.CNF");
    }
}

template <class Vendor, StateId Id> struct CheckLink : LinkCheckState<Vendor, Id> {
    explicit CheckLink(SlacMachine& m) : LinkCheckState<Vendor, Id>(m, m.wait_for_link_data.link_check_to) {
    }
    typename CheckLink::Result feed(Event const& e) override {
        if (e.is(EventId::update)) {
            if (this->poll_due()) {
                this->poll();
                return this->handled();
            }
            return this->unhandled();
        }
        if (this->link_status_cnf(e)) {
            return this->transition(std::make_unique<ExitPoint<StateId::WaitForLinkExitMatched>>(
                this->machine, "Matched", EventId::message, e));
        }
        if (is_match_req(this->ctx, e)) {
            send_match_cnf(this->ctx);
            return this->handled();
        }
        return this->unhandled();
    }
};
using Lumissil = CheckLink<LumissilLinkStatus, StateId::WaitForLinkLumissil>;
using Qualcomm = CheckLink<QualcommLinkStatus, StateId::WaitForLinkQualcomm>;

struct WaitForLinkInit : InnerLevelState<StateId::WaitForLinkInit> {
    explicit WaitForLinkInit(SlacMachine& m) : InnerLevelState(m, "Init") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::completion;
    }
    template <class Target> Result start_polling() {
        auto target = std::make_unique<Target>(machine);
        target->poll();
        return transition(std::move(target));
    }
    Result feed(Event const& e) override {
        if (not e.is(EventId::completion)) {
            return unhandled();
        }
        if (ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
            return start_polling<Qualcomm>();
        }
        if (ctx.modem_vendor == defs::ModemVendor::Lumissil) {
            return start_polling<Lumissil>();
        }
        // A vendor without a link status query fails at once.
        return transition(
            std::make_unique<ExitPoint<StateId::WaitForLinkExitFailed>>(machine, "Failed", EventId::completion, e));
    }
};

struct WaitForLink : TopLevelState<StateId::WaitForLink> {
    explicit WaitForLink(SlacMachine& m) : TopLevelState(m, "WaitForLink_def") {
    }
    void enter() override {
        auto& data = machine.wait_for_link_data;
        // Still in the matching phase (post CM_SLAC_MATCH.CNF, awaiting link) -> published as MATCHING.
        ctx.enter_state(SlacState::WaitForLink, D3State::Matching, "Waiting for Link to be ready...");
        data.link_check_to = ctx.slac_config.link_status.retry;
        data.to.arm(ctx.current_time, ctx.slac_config.link_status.timeout);
        region.start(std::make_unique<WaitForLinkInit>(machine));
        region.complete();
    }
    void leave() override {
        region.stop();
    }
    Result feed(Event const& e) override {
        if (region.feed(e)) {
            region.complete();
            return handled();
        }
        auto const exit = region.current_id();
        switch (e.id) {
        case EventId::update:
            if (machine.wait_for_link_data.to.expired(ctx.current_time)) {
                return transition(make_failed(machine));
            }
            break;
        case EventId::reset:
        case EventId::leave_bcd:
            return transition(make_reset(machine));
        case EventId::completion:
            if (exit == static_cast<int>(StateId::WaitForLinkExitFailed)) {
                return transition(make_failed(machine));
            }
            break;
        case EventId::message:
            if (exit == static_cast<int>(StateId::WaitForLinkExitMatched)) {
                return transition(make_matched(machine));
            }
            break;
        default:
            break;
        }
        return unhandled();
    }
    void deadlines(earliest_deadline& next) const override {
        next.offer(machine.wait_for_link_data.to);
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::reset, EventId::leave_bcd}) or region.current().reacts_to(e);
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

TopStatePtr make_wait_for_link(SlacMachine& machine) {
    return std::make_unique<WaitForLink>(machine);
}

} // namespace everest::lib::slac::sm::evse
