// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Init sub-machine: identifies the modem vendor by querying Qualcomm OP_ATTR and Lumissil
// NSCM_GET_VERSION, then exits to Reset.
//
//   region 0:  Query --update/timeout--> OpAttr --update/timeout--> GetVersion --update/timeout--> Done (exit)
//   region 1:  Other --message(OP_ATTR.CNF)--> Qualcomm
//              Other --message(NSCM_GET_VERSION.CNF)--> Lumissil
//   outer:     Done --update--> Reset

#include "machine.hpp"

#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_utils.hpp>
#include <everest/slac/telemetry.hpp>

#include <string>

namespace everest::lib::slac::sm::evse {

namespace {

template <class MsgT> void send_default_msg(fsm::evse::Context& ctx) {
    MsgT msg{};
    if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, msg)) {
        ctx.log_warn("Failed to send default SLAC message");
    }
}

// Each query state waits request_info_delay for the modem's answer before moving on.
template <StateId Id> struct QueryState : InnerLevelState<Id> {
    using InnerLevelState<Id>::InnerLevelState;
    void enter() override {
        state_timer.duration = this->ctx.slac_config.request_info_delay;
        state_timer.arm(this->ctx.current_time);
    }
    void deadlines(earliest_deadline& next) const override {
        state_timer.deadlines(next);
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::update;
    }
    bool timed_out() const {
        return state_timer.state_timeout(this->ctx.current_time);
    }

private:
    StateTimer state_timer;
};

struct Query : QueryState<StateId::InitQuery> {
    explicit Query(SlacMachine& m) : QueryState(m, "Init") {
    }
    Result feed(Event const& e) override;
};

struct OpAttr : QueryState<StateId::InitOpAttr> {
    explicit OpAttr(SlacMachine& m) : QueryState(m, "OpAttr") {
    }
    Result feed(Event const& e) override;
};

struct GetVersion : QueryState<StateId::InitGetVersion> {
    explicit GetVersion(SlacMachine& m) : QueryState(m, "GetVersion") {
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update) and timed_out()) {
            return transition(std::make_unique<ExitPoint<StateId::InitDone>>(machine, "Done", EventId::update, e));
        }
        return unhandled();
    }
};

Query::Result Query::feed(Event const& e) {
    if (e.is(EventId::update) and timed_out()) {
        send_default_msg<messages::qualcomm::op_attr_req>(ctx);
        return transition(std::make_unique<OpAttr>(machine));
    }
    return unhandled();
}

OpAttr::Result OpAttr::feed(Event const& e) {
    if (e.is(EventId::update) and timed_out()) {
        send_default_msg<messages::lumissil::nscm_get_version_req>(ctx);
        return transition(std::make_unique<GetVersion>(machine));
    }
    return unhandled();
}

struct Lumissil : InnerLevelState<StateId::InitLumissil> {
    explicit Lumissil(SlacMachine& m) : InnerLevelState(m, "Lumissil") {
    }
};

struct Qualcomm : InnerLevelState<StateId::InitQualcomm> {
    explicit Qualcomm(SlacMachine& m) : InnerLevelState(m, "Qualcomm") {
    }
};

struct Other : InnerLevelState<StateId::InitOther> {
    explicit Other(SlacMachine& m) : InnerLevelState(m, "Other") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::message;
    }
    Result feed(Event const& e) override {
        if (is_message_of_type(e, defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_CNF)) {
            ctx.modem_vendor = defs::ModemVendor::Qualcomm;
            auto msg = e.payload->payload_as<messages::qualcomm::op_attr_cnf>();
            ctx.log_info(msg ? utils::device_info(*msg) : std::string{});
            return transition(std::make_unique<Qualcomm>(machine));
        }
        if (is_message_of_type(e, defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_CNF)) {
            ctx.modem_vendor = defs::ModemVendor::Lumissil;
            auto msg = e.payload->payload_as<messages::lumissil::nscm_get_version_cnf>();
            ctx.log_info(msg ? utils::device_info(*msg) : std::string{});
            return transition(std::make_unique<Lumissil>(machine));
        }
        return unhandled();
    }
};

struct Init : TopLevelState<StateId::Init> {
    explicit Init(SlacMachine& m) : TopLevelState(m, "Init_def") {
    }
    void enter() override {
        ctx.enter_state(SlacState::Init, D3State::Unmatched);
        query.start(std::make_unique<Query>(machine));
        vendor.start(std::make_unique<Other>(machine));
    }
    void leave() override {
        query.stop();
        vendor.stop();
    }
    Result feed(Event const& e) override {
        auto handled_inside = query.feed(e);
        handled_inside = vendor.feed(e) or handled_inside;
        if (handled_inside) {
            return handled();
        }
        if (e.is(EventId::update) and query.current_id() == static_cast<int>(StateId::InitDone)) {
            return transition(make_reset(machine));
        }
        return unhandled();
    }
    bool reacts_to(EventId e) const override {
        return query.current().reacts_to(e) or vendor.current().reacts_to(e);
    }
    bool is_submachine() const override {
        return true;
    }
    void traverse_children(Visitor& visitor, std::size_t depth) const override {
        query.traverse(visitor, depth);
        vendor.traverse(visitor, depth);
    }

private:
    InnerRegion query;
    InnerRegion vendor;
};

} // namespace

TopStatePtr make_init(SlacMachine& machine) {
    return std::make_unique<Init>(machine);
}

} // namespace everest::lib::slac::sm::evse
