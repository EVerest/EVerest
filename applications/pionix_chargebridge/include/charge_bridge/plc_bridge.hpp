// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <charge_bridge/carrier_policy.hpp>
#include <charge_bridge/plc_keepalive.hpp>
#include <charge_bridge/utilities/print_status.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/tun_tap/tap_client.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <memory>
#include <optional>

namespace charge_bridge {

struct plc_bridge_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::string plc_tap;
    std::string plc_ip;
    std::string plc_netmaks;
    int plc_mtu;
    // Optional, config keys plc.carrier / plc.carrier_fallback / plc.carrier_gate. The defaults
    // reproduce today's behavior exactly: the tap's carrier is never touched, and no CE gate.
    carrier_mode carrier{carrier_mode::none};
    carrier_fallback carrier_fallback_policy{carrier_fallback::fail};
    carrier_gate gate{carrier_gate::none};
};

class plc_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    plc_bridge(plc_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify);
    ~plc_bridge() = default;

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    bool available() const;
    void set_cb_connection_status(bool connected);
    // The link status embedded in the MCU's latest heartbeat reply (via heartbeat_service), so the
    // carrier follows the PHY within one heartbeat interval. In carrier: none mode this only
    // cross-checks the reported technology; the carrier is never touched and nothing is recorded.
    void set_link_status(CbLinkStatusPacket const& status);
    // The CE state from the MCU's latest BSP status packet (routed here by charge_bridge from the
    // bsp bridge, same event loop thread). Input to the plc.carrier_gate: ce_mated policy; a no-op
    // in every other configuration. The BSP packet is event-driven on CE state changes, so an
    // unplug reaches the carrier within milliseconds, not on a poll.
    void set_ce_state(std::uint8_t ce_state);
    // Snapshot of the link state for the status UI, or nothing when this mode has nothing to say
    // (carrier: none without a technology mismatch). The applied carrier is the bridge's own tracked
    // state, never tap_handler::carrier(): that query cannot see IFF_LOWER_UP and lags by up to a
    // second, so it is a settled-state diagnostic only.
    std::optional<utilities::chargebridge_link_status> link_status() const;

private:
    void handle_timer_event();
    void handle_ready();
    void create_udp_client(std::string const& remote, uint16_t remote_port, std::string const& identifier);
    carrier_inputs current_carrier_inputs() const;
    void apply_carrier();
    void report_carrier_unsupported();
    // EXPERIMENTAL: re-teach the firmware the host's PLC endpoint (see plc_keepalive.hpp). Sent on the
    // 5 s timer and on every (re)connect while the reported technology is SPE.
    void send_keepalive();
    everest::lib::io::tun_tap::tap_client m_tap;
    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    everest::lib::io::event::timer_fd m_timer;
    uint16_t m_udp_port{0};
    std::string m_udp_remote;
    std::string m_identifier;
    bool m_udp_on_error{false};
    bool m_tap_on_error{false};
    bool m_udp_ready{false};
    bool m_tap_ready{false};
    everest::lib::util::observable<bool> m_cb_is_connected{false};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;

    // Carrier state. All of this is touched only from the single event loop thread (rx handlers,
    // error handlers, the timer and the connection-status fan-out all run there), so no locking.
    carrier_mode m_carrier_mode{carrier_mode::none};
    carrier_fallback m_carrier_fallback{carrier_fallback::fail};
    carrier_gate m_carrier_gate{carrier_gate::none};
    // ce_state_mated() of the last reported CE state. False until the first BSP status arrives, and
    // reset to false on every connection loss: a (re)connected board must re-prove the mate before
    // the gate opens, so a stale "mated" can never outlive the board that reported it.
    bool m_ce_mated{false};
    std::uint8_t m_technology{CB_LINK_TECH_UNKNOWN};
    bool m_phy_operational{false};
    bool m_plca_engaged{false};
    std::uint32_t m_transition_count{0};
    bool m_have_link_status{false};
    // What was last pushed to the kernel, so a no-op ioctl is never issued. Seeded with the state
    // open() established and re-seeded on every tap up-edge (the device is re-created by a reset).
    bool m_carrier_applied{true};
    bool m_carrier_unsupported{false};
    bool m_carrier_unsupported_reported{false};
    bool m_technology_mismatch{false};
    bool m_technology_mismatch_reported{false};
    std::string m_tap_name;
    std::optional<mac_address> m_tap_mac;
    bool m_tap_mac_failure_reported{false};
};

} // namespace charge_bridge
