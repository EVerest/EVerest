// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/reset_chip.hpp>

#include <chrono>

#include <everest/slac/evse/state/idle.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::evse::state {

void ResetChip::enter() {
    m_ctx.status.match_state = SlacState::ResetChip;
    m_ctx.status.d3_state = D3State::Unmatched;
    m_delay.arm(m_ctx.current_time, std::chrono::milliseconds(m_ctx.slac_config.chip_reset.delay_ms));
}

Result ResetChip::feed(SlacEvent const& ev) {

    if (std::get_if<event::Update>(&ev)) {
        if (not m_request_sent) {
            if (not m_delay.expired(m_ctx.current_time)) {
                return {};
            }
            if (m_ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
                messages::qualcomm::cm_reset_device_req reset_req{};
                if (not m_ctx.send_slac_message(m_ctx.slac_config.plc_peer_mac, reset_req)) {
                    m_ctx.log_warn("Failed to send CM_RESET_DEVICE.REQ");
                }
            } else if (m_ctx.modem_vendor == defs::ModemVendor::Lumissil) {
                messages::lumissil::nscm_reset_device_req reset_req{};
                if (not m_ctx.send_slac_message(m_ctx.slac_config.plc_peer_mac, reset_req)) {
                    m_ctx.log_warn("Failed to send NSCM_RESET_DEVICE.REQ");
                }
            }
            m_request_sent = true;
            m_timeout.arm(m_ctx.current_time, std::chrono::milliseconds(m_ctx.slac_config.chip_reset.timeout_ms));
            return handled();
        }

        // The CG5317 does not answer the reset, so there is nothing to wait for.
        if (m_ctx.modem_vendor == defs::ModemVendor::Lumissil) {
            return m_ctx.create_state<Idle>();
        }
        // Qualcomm answers, and is handled below. A modem of any other vendor was sent nothing and
        // will answer nothing, so without this the state never completed at all. Either way the
        // wait is what the manifest calls "Timeout for RS_DEV.REQ (waiting for RS_DEV.CNF)".
        if (m_timeout.expired(m_ctx.current_time)) {
            m_ctx.log_warn("No reset confirmation within chip_reset.timeout_ms, continuing without it");
            return m_ctx.create_state<Idle>();
        }
        return {};
    }

    if (auto const* message = get_if_message(ev)) {
        if (m_request_sent and message->get_mmtype() == defs::mmtype::qualcomm::RESET_DEVICE_CNF) {
            return m_ctx.create_state<Idle>();
        }
    }

    return {};
}

} // namespace everest::slac::evse::state
