// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <cerrno>
#include <charge_bridge/bridge_failure.hpp>
#include <charge_bridge/plc_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/udp/udp_payload.hpp>
#include <iostream>

namespace {

// A carrier request that fails with one of these did not reach a TUNSETCARRIER implementation at
// all: the ioctl was added in Linux v5.0. Anything else is a genuine error.
bool is_carrier_unsupported(int error) {
    return error == EINVAL or error == ENOTTY;
}

char const* to_string(charge_bridge::carrier_mode mode) {
    return mode == charge_bridge::carrier_mode::firmware ? "firmware" : "none";
}

char const* to_string(charge_bridge::carrier_fallback fallback) {
    return fallback == charge_bridge::carrier_fallback::warn ? "warn" : "fail";
}

} // namespace

namespace charge_bridge {

plc_bridge::plc_bridge(plc_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    // In firmware mode the device is born carrier-off: the kernel creates a fresh tap carrier-on and
    // bringing it up emits an RTM_NEWLINK carrying the flags of that moment, which no later ioctl can
    // retract. Dropping the carrier inside open() is what keeps a watcher from ever seeing a spurious
    // carrier-up. fd_event_client replays these arguments on every internal reset, so this covers the
    // re-created device too.
    m_tap(config.plc_tap, config.plc_ip, config.plc_netmaks, config.plc_mtu, config.carrier == carrier_mode::none),
    m_udp_port(config.cb_port),
    m_udp_remote(config.cb_remote),
    m_ready_notify(ready_notify),
    m_carrier_mode(config.carrier),
    m_carrier_fallback(config.carrier_fallback_policy),
    m_carrier_gate(config.gate),
    m_carrier_applied(config.carrier == carrier_mode::none) {

    using namespace std::chrono_literals;
    m_timer.set_timeout(5s);

    auto identifier = config.cb + "/" + config.item;
    m_identifier = identifier;

    if (m_carrier_mode == carrier_mode::firmware) {
        // Detection point for the unsupported-kernel policy. open(..., false) succeeds even when the
        // kernel has no TUNSETCARRIER, and it deliberately does NOT leave that errno in get_error():
        // fd_event_client reads the policy's error right after a successful open and would mark the
        // fresh connection failed, tearing the device down and driving the retry below into an endless
        // create-destroy cycle. carrier_setup_error() is the separate channel for it.
        auto const& handle = m_tap.get_raw_handler();
        if (handle and is_carrier_unsupported(handle->carrier_setup_error())) {
            m_carrier_unsupported = true;
            // The request never landed, so the kernel default stands: the device is carrier-on.
            m_carrier_applied = true;
        }
        if (m_carrier_unsupported) {
            if (decide_carrier(current_carrier_inputs()).refuse_bridge) {
                // Policy 'fail' (the default): refuse the bridge rather than bridge traffic behind a
                // carrier that is permanently up. A running kernel does not grow the ioctl, so this is
                // permanent - reported once, then the bridge stays disabled instead of recreating the
                // tap device on every retry cadence for the rest of the session.
                throw permanent_bridge_failure("plc.carrier: firmware requires TUNSETCARRIER (Linux 5.0+), "
                                               "which this kernel does not implement; "
                                               "set plc.carrier_fallback: warn to bridge anyway");
            }
            report_carrier_unsupported();
        }
    }
    m_tap.set_rx_handler([this](auto const& data, auto&) {
        everest::lib::io::udp::udp_payload pl;
        pl.buffer = data;
        if (m_udp) {
            m_udp->tx(pl);
        }
    });

    create_udp_client(config.cb_remote, config.cb_port, identifier);

    m_tap.set_error_handler([this, identifier](auto id, auto const& msg) {
        utilities::print_error(identifier, "PLC/TAP", id) << msg << std::endl;
        m_tap_on_error = id not_eq 0;
        m_tap_ready = id == 0;
        if (id == 0) {
            // Up-edge: the tap device was (re-)created, which replayed open()'s carrier argument, so
            // the applied state is whatever open() established - not what we pushed before the reset.
            // Re-seed it, otherwise the no-op guard in apply_carrier() would suppress the re-assert.
            m_carrier_applied = m_carrier_mode == carrier_mode::none or m_carrier_unsupported;
            apply_carrier();
        }
        handle_ready();
    });
    m_ready.setCallback([this](auto&, auto&) { m_ready_notify.notify(); });
    m_cb_is_connected.setCallback([this](bool last, bool current) {
        if (not last and current) {
            if (m_udp) {
                m_udp->reset();
            }
        }
        handle_ready();
    });
}

void plc_bridge::create_udp_client(std::string const& remote, uint16_t remote_port, std::string const& identifier) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port);
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp->set_rx_handler([this](auto const& data, auto&) { m_tap.tx(data.buffer); });
    m_udp->set_error_handler([this, identifier](auto id, auto const& msg) {
        utilities::print_error(identifier, "PLC/UDP", id) << msg << std::endl;
        m_udp_on_error = id not_eq 0;
        m_udp_ready = id == 0;
        handle_ready();
    });
}

void plc_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp.reset();
    handle_ready();
}

void plc_bridge::connect_cb_endpoint(std::string const& remote) {
    m_udp_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_udp_remote, m_udp_port, m_identifier);
    handle_ready();
}

void plc_bridge::handle_timer_event() {
    if (m_udp_on_error) {
        if (m_udp) {
            m_udp->reset();
        }
    }
    if (m_tap_on_error) {
        m_tap.reset();
    }
}

void plc_bridge::handle_ready() {
    m_ready.set(m_udp_ready and m_tap_ready and m_cb_is_connected);
}

bool plc_bridge::available() const {
    return m_ready;
}

void plc_bridge::set_cb_connection_status(bool connected) {
    m_cb_is_connected.set(connected);
    // Losing the ChargeBridge drops the carrier immediately. Together with the link report this is
    // what makes a stale carrier-on structurally impossible: carrier-up can only be sustained by a
    // live heartbeat-verified connection AND a fresh report of an operational PHY.
    if (not connected) {
        // The mate gate follows the same rule: whatever comes back (a rebooted MCU, a retargeted
        // endpoint, a different board) must re-prove the mate with a fresh BSP report before the
        // gate re-opens.
        m_ce_mated = false;
    }
    apply_carrier();
}

void plc_bridge::set_ce_state(std::uint8_t ce_state) {
    const bool mated = ce_state_mated(ce_state);
    if (mated == m_ce_mated) {
        return;
    }
    m_ce_mated = mated;
    apply_carrier();
}

void plc_bridge::set_link_status(CbLinkStatusPacket const& status) {
    // The technology is latched: the MCU latches it sticky at plc_init and never reports UNKNOWN
    // again, so the only UNKNOWN that can arrive here after a real report is the all-zero packet the
    // reboot path synthesizes. Letting that clear the latch would leave the PLC fail-open branch for
    // up to a heartbeat and drop the carrier on a HomePlug board - transiently strangling SLAC, which
    // is exactly what the fail-open exists to prevent. The flags are recorded further down and the
    // zeroed ones do take effect, which is what makes the carrier drop on a genuine MCS link.
    if (status.technology not_eq CB_LINK_TECH_UNKNOWN) {
        m_technology = status.technology;
    }

    // In carrier: none mode a report is a no-op beyond that cross-check: this mode never touches the
    // carrier, and recording the report would put a link line into the status UI of every plain-PLC
    // installation - which now receives a report every second - describing a carrier nothing drives.
    // The one exception is a latched technology mismatch. That does surface a line, so the reports
    // behind it have to be recorded too, or the line would claim none had ever arrived.
    const bool cross_check_only =
        m_carrier_mode == carrier_mode::none and not decide_carrier(current_carrier_inputs()).technology_mismatch;
    if (cross_check_only) {
        apply_carrier();
        return;
    }

    const bool phy_operational = status.phy_operational not_eq 0;
    const bool plca_engaged = status.plca_engaged not_eq 0;

    // transition_count counts phy_operational/plca_engaged edges per flag since the MCU booted, so a
    // jump larger than the number of edges this snapshot carries means the PHY flapped and settled
    // again between two replies. Diagnostic only: the reported flags are the truth, the counter merely
    // explains the gap.
    //
    // It is a plain counter, not a sequence number: it restarts at zero on every MCU boot and wraps
    // like any uint32, so its ordering carries no meaning. Compare for inequality and take the
    // difference in the unsigned domain rather than testing which value is larger. A restart is what
    // makes the difference meaningless, and the reboot path's synthesized all-zero report is what
    // keeps this honest across one: it rebases the counter to zero before the fresh snapshot of the
    // new boot arrives. A restart the uptime check did not catch still lands here, as a difference no
    // amount of flapping could produce - reported as the restart it is rather than as a bogus count.
    constexpr std::uint32_t max_plausible_flaps = 1000000u;
    if (m_have_link_status and status.transition_count not_eq m_transition_count) {
        const std::uint32_t reported_edges =
            (phy_operational not_eq m_phy_operational ? 1u : 0u) + (plca_engaged not_eq m_plca_engaged ? 1u : 0u);
        auto const delta = static_cast<std::uint32_t>(status.transition_count - m_transition_count);
        if (delta > max_plausible_flaps) {
            utilities::print_error(m_identifier, "PLC/LINK", -1)
                << "link transition counter restarted (" << m_transition_count << " -> " << status.transition_count
                << "): the ChargeBridge restarted unnoticed" << std::endl;
        } else if (delta > reported_edges) {
            utilities::print_error(m_identifier, "PLC/LINK", -1)
                << "missed " << (delta - reported_edges) << " link transition(s) between replies" << std::endl;
        }
    }

    m_phy_operational = phy_operational;
    m_plca_engaged = plca_engaged;
    m_transition_count = status.transition_count;
    m_have_link_status = true;

    apply_carrier();
}

carrier_inputs plc_bridge::current_carrier_inputs() const {
    carrier_inputs inputs;
    inputs.mode = m_carrier_mode;
    inputs.cb_connected = m_cb_is_connected;
    inputs.phy_operational = m_phy_operational;
    inputs.plca_engaged = m_plca_engaged;
    inputs.technology = m_technology;
    inputs.carrier_unsupported = m_carrier_unsupported;
    inputs.fallback = m_carrier_fallback;
    inputs.gate = m_carrier_gate;
    inputs.ce_mated = m_ce_mated;
    return inputs;
}

void plc_bridge::report_carrier_unsupported() {
    if (m_carrier_unsupported_reported) {
        return;
    }
    m_carrier_unsupported_reported = true;
    // Reached both from the startup detection (policy warn - policy fail throws instead) and from a
    // runtime set_carrier() that failed the same way, where the configured policy can be either. Print
    // the configured value rather than assuming the one that reaches here at startup.
    utilities::print_error(m_identifier, "PLC/CARRIER", -1)
        << "kernel does not implement TUNSETCARRIER: the tap carrier stays permanently ON and link "
           "supervision is DEGRADED (plc.carrier_fallback: "
        << to_string(m_carrier_fallback) << ")" << std::endl;
}

void plc_bridge::apply_carrier() {
    auto const decision = decide_carrier(current_carrier_inputs());

    if (decision.technology_mismatch and not m_technology_mismatch_reported) {
        m_technology_mismatch_reported = true;
        utilities::print_error(m_identifier, "PLC/CARRIER", -1)
            << "configured plc.carrier: " << to_string(m_carrier_mode) << " but the ChargeBridge reports technology "
            << static_cast<int>(m_technology)
            << (decision.plc_fail_open ? " (PLC): forcing the carrier ON and ignoring the reported link flags"
                                       : " - check the configuration")
            << std::endl;
    }
    m_technology_mismatch = decision.technology_mismatch;

    if (not decision.apply or decision.carrier == m_carrier_applied) {
        return;
    }

    // May be absent while the client is between two devices. Nothing to do then: the re-open replays
    // the carrier argument and the up-edge above re-asserts the state we know.
    auto const& handle = m_tap.get_raw_handler();
    if (not handle) {
        return;
    }

    if (handle->set_carrier(decision.carrier)) {
        m_carrier_applied = decision.carrier;
        return;
    }

    auto const error = handle->get_error();
    if (is_carrier_unsupported(error)) {
        // Not caught at construction (or the device was re-created on a kernel that cannot do this).
        // Same policy, minus the refusal: the bridge is already running, and tearing it down here
        // would take the data path with it.
        m_carrier_unsupported = true;
        m_carrier_applied = true;
        report_carrier_unsupported();
        return;
    }
    utilities::print_error(m_identifier, "PLC/CARRIER", error)
        << "failed to set the tap carrier " << (decision.carrier ? "ON" : "OFF") << std::endl;
}

std::optional<utilities::chargebridge_link_status> plc_bridge::link_status() const {
    // Nothing to show in carrier: none mode. The link status now rides along with every heartbeat
    // reply on every board, so a plain-PLC installation receives one every second - but this mode
    // records none of it and never touches the carrier, so a line describing it would be permanent
    // noise in the dashboard of every existing installation. The one exception is the technology
    // cross-check: a board that reports SPE while configured as carrier: none is a real
    // misconfiguration and has to stay visible.
    if (m_carrier_mode == carrier_mode::none and not m_technology_mismatch) {
        return std::nullopt;
    }

    utilities::chargebridge_link_status status;
    status.carrier_mode = to_string(m_carrier_mode);
    status.have_report = m_have_link_status;
    status.technology = m_technology;
    status.phy_operational = m_phy_operational;
    status.plca_engaged = m_plca_engaged;
    status.transition_count = m_transition_count;
    status.carrier_applied = m_carrier_applied;
    status.carrier_unsupported = m_carrier_unsupported;
    status.technology_mismatch = m_technology_mismatch;
    status.gate_active = m_carrier_gate not_eq carrier_gate::none;
    status.gate_mated = m_ce_mated;
    return status;
}

bool plc_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(&m_tap) && result;
    result = handler.register_event_handler(m_udp.get()) && result;
    result = handler.register_event_handler(&m_timer, [this](auto) { handle_timer_event(); }) && result;
    return result;
}

bool plc_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_tap) && result;
    result = handler.unregister_event_handler(m_udp.get()) && result;
    result = handler.unregister_event_handler(&m_timer) && result;
    return result;
}

} // namespace charge_bridge
