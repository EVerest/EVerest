// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Matched sub-machine: the AVLN is up. Polls the modem link status (vendor specific), debounces
// link loss and runs the CM_AMP_MAP exchange. Leaves through the Failed exit when the link is lost;
// the top-level machine takes that to Reset.
//
//   Init --completion / query link status--> Lumissil | Qualcomm (link detection on), NoDetect (off)
//   Lumissil|Qualcomm --update / link status poll and CM_AMP_MAP retransmission when due--> (internal)
//   Lumissil|Qualcomm --message(link status: not linked), debounce count reached--> Failed (exit)
//   Lumissil|Qualcomm --message(link status: not linked | linked) / count | clear--> (internal)
//   Lumissil|Qualcomm|NoDetect|Init --message(CM_AMP_MAP.REQ) / answer--> (internal)
//   Lumissil|Qualcomm|NoDetect --message(awaited CM_AMP_MAP.CNF) / acknowledge--> (internal)
//   NoDetect --update / CM_AMP_MAP retransmission when due--> (internal)
//   outer:  reset, leave_bcd --> Reset;  Failed exit --> Reset (connection lost)

#include "../misc.hpp"
#include "link_status.hpp"
#include "machine.hpp"

#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/telemetry.hpp>

#include <cstdio>
#include <string>

namespace everest::lib::slac::sm::evse {

namespace {

// A valid CM_AMP_MAP.REQ (am_len != 0) received while matched. am_len == 0 is
// invalid and must be left unanswered (ISO 15118-5 CmAmpMap_005).
bool is_amp_map_req(Event const& e) {
    if (not e.is(EventId::message)) {
        return false;
    }
    if (e.payload->get_mmtype() != (defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_REQ)) {
        return false;
    }
    auto const req = e.payload->payload_as<messages::cm_amp_map_req>();
    return req.has_value() and req->am_len != 0;
}

// Answer a valid CM_AMP_MAP.REQ with CM_AMP_MAP.CNF(result=0x00). On real HW
// the requested transmit-power reduction is applied to the modem; that RF
// effect is out of scope for the SLAC stack.
void send_amp_map_cnf(fsm::evse::Context& ctx, Event const& e) {
    messages::cm_amp_map_cnf reply{};
    reply.result = defs::CM_AMP_MAP_CNF_RESULT_SUCCESS;
    if (not ctx.send_slac_message(e.payload->get_src_mac(), reply)) {
        ctx.log_warn("Failed to send CM_AMP_MAP.CNF");
    }
}

// The SECC-initiated CM_AMP_MAP exchange (ISO 15118-3 A.9.6) lives in AmpMapHandler, owned by the
// Matched data as `amp_map`; these states only wire it to the update tick and to the incoming CNF.
bool is_amp_map_cnf_ok(MatchedData const& data, Event const& e) {
    return e.is(EventId::message) and data.amp_map.is_awaited_cnf(*e.payload);
}

template <class Vendor, StateId Id> struct CheckLink : LinkCheckState<Vendor, Id> {
    explicit CheckLink(SlacMachine& m) : LinkCheckState<Vendor, Id>(m, m.matched_data.link_check_to) {
    }
    typename CheckLink::Result feed(Event const& e) override {
        auto& ctx = this->ctx;
        auto& data = this->machine.matched_data;
        auto const now = ctx.current_time;
        if (e.is(EventId::update)) {
            // The update tick serves two timers, the link-status poll and the CM_AMP_MAP
            // retransmission, in one internal transition so that both run when both are due.
            auto const poll_due = this->poll_due();
            auto const retransmit_due = data.amp_map.retransmit_due(now);
            if (not(poll_due or retransmit_due)) {
                return this->unhandled();
            }
            if (poll_due) {
                this->poll();
            }
            if (data.amp_map.retransmit_due(now)) {
                data.amp_map.retransmit(ctx);
            }
            return this->handled();
        }
        if (is_amp_map_cnf_ok(data, e)) {
            data.amp_map.acknowledge_cnf();
            return this->handled();
        }
        if (is_amp_map_req(e)) {
            send_amp_map_cnf(ctx, e);
            return this->handled();
        }
        if (this->link_status_cnf(e)) {
            if (data.consecutive_neg_link_status != 0) {
                data.consecutive_neg_link_status = 0;
                ctx.log_info("Positive LINK_STATUS, link recovered; debounce count cleared");
            }
            return this->handled();
        }
        if (this->link_status_neg(e)) {
            if (data.consecutive_neg_link_status + 1 >= data.neg_link_status_threshold) {
                return this->transition(std::make_unique<ExitPoint<StateId::MatchedExitFailed>>(this->machine, "Failed",
                                                                                                EventId::message, e));
            }
            data.consecutive_neg_link_status++;
            ctx.log_warn("Negative LINK_STATUS while matched (" + std::to_string(data.consecutive_neg_link_status) +
                         "/" + std::to_string(data.neg_link_status_threshold) +
                         "), keeping the link up while debouncing");
            return this->handled();
        }
        return this->unhandled();
    }
};
using Lumissil = CheckLink<LumissilLinkStatus, StateId::MatchedLumissil>;
using Qualcomm = CheckLink<QualcommLinkStatus, StateId::MatchedQualcomm>;

struct NoDetect : InnerLevelState<StateId::MatchedNoDetect> {
    explicit NoDetect(SlacMachine& m) : InnerLevelState(m, "NoDetect") {
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::message});
    }
    Result feed(Event const& e) override {
        auto& data = machine.matched_data;
        if (e.is(EventId::update)) {
            if (data.amp_map.retransmit_due(ctx.current_time)) {
                data.amp_map.retransmit(ctx);
                return handled();
            }
            return unhandled();
        }
        if (is_amp_map_cnf_ok(data, e)) {
            data.amp_map.acknowledge_cnf();
            return handled();
        }
        if (is_amp_map_req(e)) {
            send_amp_map_cnf(ctx, e);
            return handled();
        }
        return unhandled();
    }
};

struct MatchedInit : InnerLevelState<StateId::MatchedInit> {
    explicit MatchedInit(SlacMachine& m) : InnerLevelState(m, "Init") {
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::completion, EventId::message});
    }
    template <class Target> Result start_polling() {
        auto target = std::make_unique<Target>(machine);
        target->poll();
        return transition(std::move(target));
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::completion)) {
            auto const detect_link = ctx.slac_config.link_status.do_detect;
            if (not detect_link) {
                return transition(std::make_unique<NoDetect>(machine));
            }
            if (ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
                return start_polling<Qualcomm>();
            }
            if (ctx.modem_vendor == defs::ModemVendor::Lumissil) {
                return start_polling<Lumissil>();
            }
            return unhandled();
        }
        if (is_amp_map_req(e)) {
            send_amp_map_cnf(ctx, e);
            return handled();
        }
        return unhandled();
    }
};

struct Matched : TopLevelState<StateId::Matched> {
    explicit Matched(SlacMachine& m) : TopLevelState(m, "Matched_def") {
    }
    void enter() override {
        auto& data = machine.matched_data;
        {
            char attenuation[24];
            std::snprintf(attenuation, sizeof(attenuation), "%.1f",
                          static_cast<double>(ctx.status.average_attenuation));
            ctx.enter_state(SlacState::Matched, D3State::Matched,
                            "Entered Matched state (EV " + format_mac_addr(ctx.status.ev_mac) + ", avg. attenuation " +
                                attenuation + " dB)");
        }
        ctx.clear_match_confirm_cache();
        ctx.signal_dlink_ready(true);
        data.link_check_to = ctx.slac_config.link_status.poll_in_matched_state;
        data.consecutive_neg_link_status = 0;
        data.neg_link_status_threshold =
            ctx.slac_config.link_status.debounce_count < 1 ? 1 : ctx.slac_config.link_status.debounce_count;
        data.leaving_on_link_loss = false;
        ctx.status.modem_link_ready = true;
        data.amp_map.start(ctx);
        region.start(std::make_unique<MatchedInit>(machine));
        region.complete();
    }
    void leave() override {
        region.stop();
        if (machine.shutting_down()) {
            return;
        }
        ctx.signal_dlink_ready(false);
        ctx.status.ev_mac.fill(0);
        ctx.status.average_attenuation = 0.f;
        ctx.status.modem_link_ready = false;
        ctx.clear_match_confirm_cache();
        if (machine.matched_data.leaving_on_link_loss) {
            machine.matched_data.leaving_on_link_loss = false;
            ctx.log_error("Connection lost in matched state");
            ctx.signal_error_routine_request();
        }
    }
    Result feed(Event const& e) override {
        if (region.feed(e)) {
            region.complete();
            return handled();
        }
        if (e.is(EventId::reset) or e.is(EventId::leave_bcd)) {
            return transition(make_reset(machine));
        }
        if (e.is(EventId::message) and region.current_id() == static_cast<int>(StateId::MatchedExitFailed)) {
            machine.matched_data.leaving_on_link_loss = true;
            return transition(make_reset(machine));
        }
        return unhandled();
    }
    void deadlines(earliest_deadline& next) const override {
        machine.matched_data.amp_map.deadlines(next);
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::reset, EventId::leave_bcd}) or region.current().reacts_to(e);
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

TopStatePtr make_matched(SlacMachine& machine) {
    return std::make_unique<Matched>(machine);
}

} // namespace everest::lib::slac::sm::evse
