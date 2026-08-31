// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/init.hpp>
#include <everest/slac/evse/state/reset.hpp>

#include <chrono>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/utils.hpp>

namespace everest::slac::evse::state {

namespace {

template <typename MsgT> void send_probe(Context& ctx) {
    MsgT msg{};
    if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, msg)) {
        ctx.log_warn("Failed to send default SLAC message");
    }
}

std::string step_name(bool before_op_attr, bool before_get_version) {
    if (before_op_attr) {
        return "Init";
    }
    if (before_get_version) {
        return "OpAttr";
    }
    return "GetVersion";
}

} // namespace

void Init::enter() {
    m_ctx.status.match_state = SlacState::Init;
    m_ctx.status.d3_state = D3State::Unmatched;
    arm_step_timer();
}

void Init::arm_step_timer() {
    m_step_timer.arm(m_ctx.current_time, std::chrono::milliseconds(m_ctx.slac_config.request_info_delay_ms));
}

void Init::latch_modem_vendor(messages::HomeplugMessage const& frame) {
    if (m_vendor_latched or not frame.is_valid()) {
        return;
    }
    auto const mmtype = frame.get_mmtype();

    if (mmtype == defs::lumissil::MMTYPE_NSCM_GET_VERSION_CNF) {
        auto const msg = frame.payload_as<messages::lumissil::nscm_get_version_cnf>();
        m_ctx.modem_vendor = defs::ModemVendor::Lumissil;
        m_ctx.log_info(msg ? utils::device_info(*msg) : std::string{});
        m_vendor_latched = true;
        return;
    }

    if (mmtype == defs::qualcomm::MMTYPE_OP_ATTR_CNF) {
        auto const msg = frame.payload_as<messages::qualcomm::op_attr_cnf>();
        m_ctx.modem_vendor = defs::ModemVendor::Qualcomm;
        m_ctx.log_info(msg ? utils::device_info(*msg) : std::string{});
        m_vendor_latched = true;
    }
}

Result Init::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (not m_step_timer.expired(m_ctx.current_time)) {
            return {};
        }
        switch (m_step) {
        case Step::BeforeOpAttr:
            send_probe<messages::qualcomm::op_attr_req>(m_ctx);
            m_step = Step::BeforeGetVersion;
            arm_step_timer();
            return handled();
        case Step::BeforeGetVersion:
            send_probe<messages::lumissil::nscm_get_version_req>(m_ctx);
            m_step = Step::AwaitingAnswer;
            arm_step_timer();
            return handled();
        case Step::AwaitingAnswer:
            return m_ctx.create_state<Reset>();
        }
        return {};
    }

    if (auto const* message = get_if_message(ev)) {
        latch_modem_vendor(*message);
        return handled();
    }

    return {};
}

void Init::describe(StateTree& out) const {
    StateBase::describe(out);
    StateTree child;
    child.name = step_name(m_step == Step::BeforeOpAttr, m_step == Step::BeforeGetVersion);
    out.children.push_back(std::move(child));
}

void Init::signature(std::string& out) const {
    StateBase::signature(out);
    out += 's';
    out += std::to_string(static_cast<int>(m_step));
    out += ';';
}

} // namespace everest::slac::evse::state
