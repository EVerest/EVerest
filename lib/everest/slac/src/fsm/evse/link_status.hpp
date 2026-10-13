// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Vendor-specific modem link status query, used by WaitForLink and Matched.

#pragma once

#include "machine.hpp"

#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

namespace everest::lib::slac::sm::evse {

struct LumissilLinkStatus {
    static constexpr char const* name = "Lumissil";

    static bool is_link_status_message(Event const& e) {
        return e.is(EventId::message) and
               e.payload->get_mmtype() == (defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_CNF);
    }
    // Whether the frame is a link status answer at all, and if so whether it reports the link as up.
    static bool linked(Event const& e) {
        auto const msg = e.payload->payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        return msg.has_value() and msg->link_status == defs::D_LINK_STATUS_LINKED;
    }
    static bool not_linked(Event const& e) {
        auto const msg = e.payload->payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        return msg.has_value() and msg->link_status != defs::D_LINK_STATUS_LINKED;
    }
    static void request(fsm::evse::Context& ctx) {
        messages::lumissil::nscm_get_d_link_status_req link_status_req{};
        if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, link_status_req)) {
            ctx.log_warn("Failed to send CM_GET_D_LINK_STATUS.REQ to SLAC peer");
        }
    }
};

struct QualcommLinkStatus {
    static constexpr char const* name = "Qualcomm";

    static bool is_link_status_message(Event const& e) {
        return e.is(EventId::message) and
               e.payload->get_mmtype() == (defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_CNF);
    }
    static bool linked(Event const& e) {
        auto const msg = e.payload->payload_as<messages::qualcomm::link_status_cnf>();
        return msg.has_value() and msg->link_status == defs::D_LINK_STATUS_LINKED;
    }
    static bool not_linked(Event const& e) {
        auto const msg = e.payload->payload_as<messages::qualcomm::link_status_cnf>();
        return msg.has_value() and msg->link_status != defs::D_LINK_STATUS_LINKED;
    }
    static void request(fsm::evse::Context& ctx) {
        messages::qualcomm::link_status_req link_status_req{};
        if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, link_status_req)) {
            ctx.log_warn("Failed to send LINK_STATUS.REQ to SLAC peer");
        }
    }
};

// A state polling the link status every `period`: entered once per link-check phase; everything
// that happens inside the state is an internal transition, so the poll cadence is not disturbed.
template <class Vendor, StateId Id> struct LinkCheckState : InnerLevelState<Id> {
    LinkCheckState(SlacMachine& m, std::chrono::milliseconds period) : InnerLevelState<Id>(m, Vendor::name) {
        state_timer.duration = period;
    }
    void enter() override {
        state_timer.arm(this->ctx.current_time);
    }
    void deadlines(earliest_deadline& next) const override {
        state_timer.deadlines(next);
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::message});
    }
    bool poll_due() const {
        return state_timer.state_timeout(this->ctx.current_time);
    }
    // The poll cadence counts from the last request actually sent.
    void poll() {
        state_timer.to.reset(this->ctx.current_time);
        Vendor::request(this->ctx);
    }
    bool link_status_cnf(Event const& e) const {
        return Vendor::is_link_status_message(e) and Vendor::linked(e);
    }
    bool link_status_neg(Event const& e) const {
        return Vendor::is_link_status_message(e) and Vendor::not_linked(e);
    }

private:
    StateTimer state_timer;
};

} // namespace everest::lib::slac::sm::evse
