// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/heartbeat_service.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/mcu_uptime.hpp>
#include <charge_bridge/utilities/platform_utils.hpp>
#include <chrono>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <iostream>
#include <memory>
#include <protocol/cb_management.h>
#include <random>
#include <string>

namespace {
const std::uint16_t s_to_ms_factor = 1000;

// cb-session-v1: random nonzero id per tool instance; a new id makes the MCU hand the session
// over. 0 = legacy sender.
std::uint32_t generate_session_id() {
    std::random_device rd;
    std::uint32_t id = 0;
    while (id == 0) {
        id = (static_cast<std::uint32_t>(rd()) << 16) ^ static_cast<std::uint32_t>(rd());
    }
    return id;
}

std::string format_ipv4(std::uint32_t ip) {
    if (ip == 0) {
        return "unknown";
    }
    return std::to_string((ip >> 24) & 0xff) + "." + std::to_string((ip >> 16) & 0xff) + "." +
           std::to_string((ip >> 8) & 0xff) + "." + std::to_string(ip & 0xff);
}
} // namespace

namespace charge_bridge {
using namespace std::chrono_literals;

heartbeat_service::heartbeat_service(heartbeat_config const& config,
                                     std::function<void(bool)> const& publish_connection_status,
                                     std::function<void(CbLinkStatusPacket const&)> const& publish_link_status,
                                     everest::lib::io::event::event_fd& ready_notify) :
    m_udp_port(config.cb_port),
    m_udp_remote(config.cb_remote),
    m_publish_connection_status(publish_connection_status),
    m_publish_link_status(publish_link_status),
    m_ready_notify(ready_notify) {

    m_identifier = config.cb + "/" + config.item;
    std::memcpy(&m_config_message.data.module_config, &config.cb_config, sizeof(CbConfig));
    m_config_message.data.session_id = generate_session_id();
    m_config_message.data.session_flags = config.force_takeover ? CB_SESSION_FLAG_FORCE_TAKEOVER : 0;
    m_config_message.type = CbStructType::CST_HostToCb_Heartbeat;
    m_heartbeat_interval = std::chrono::milliseconds(config.interval_s * s_to_ms_factor);
    m_connection_to = std::chrono::milliseconds(config.connection_to_s * s_to_ms_factor);
    m_heartbeat_timer.set_timeout(m_heartbeat_interval);
    m_error_timer.set_timeout(5s);
    m_last_heartbeat_reply = std::chrono::steady_clock::time_point();

    create_udp_client(m_udp_remote, m_udp_port);

    m_ready.setCallback([this](auto&, auto&) { m_ready_notify.notify(); });
}

void heartbeat_service::create_udp_client(std::string const& remote, uint16_t remote_port) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port);
    m_udp_on_error = false;
    m_udp_ready = false;
    m_udp->set_rx_handler([this](auto const& data, auto&) { handle_udp_rx(data); });
    m_udp->set_error_handler([this](auto id, auto const& msg) {
        if (m_inital_cb_commcheck and id == 0) {
            utilities::print_error(m_identifier, "HEARTBEAT/UDP", 1) << "Waiting for ChargeBridge" << std::endl;
        } else {
            utilities::print_error(m_identifier, "HEARTBEAT/UDP", id) << msg << std::endl;
        }
        m_udp_on_error = id not_eq 0;
        m_udp_ready = id == 0;
        handle_ready();
    });
}

void heartbeat_service::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_udp_on_error = true;
    m_cb_connected = false;
    m_last_heartbeat_reply = std::chrono::steady_clock::time_point();
    // Everything below describes the device that was on the other end. This is also the retarget path
    // (connect_cb_endpoint goes through here), so the next endpoint may be a different ChargeBridge
    // entirely: keeping any of it would let the dashboard attribute one device's state to another.
    //
    // The role and the technology drive a correctness claim about the configuration - the mismatch
    // marker and its remedy - so a stale value there does not just look wrong, it accuses the wrong
    // hardware. Back to "nothing reported yet" until the new endpoint says otherwise, and the
    // once-per-boot mismatch report is re-armed so the new device gets its own.
    m_latched_cb_type = cb_type_not_latched;
    m_link_technology = CB_LINK_TECH_UNKNOWN;
    m_role_mismatch_reported = false;
    // Same class of staleness, same fix: a new device's uptime is unrelated to the old one's, and a
    // lower first reading would otherwise be read as a reboot - inflating the reset count and firing
    // the synthesized all-zero link status, which drops the tap carrier for a heartbeat. Zero makes
    // the first reply from the new endpoint unable to look like a regression. The reset tally itself
    // is deliberately kept: it counts what this session has seen.
    m_mcu_timestamp = 0;
    if (m_udp) {
        m_udp->reset();
    }
    m_udp.reset();
    handle_ready();
}

void heartbeat_service::connect_cb_endpoint(std::string const& remote) {
    m_udp_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_udp_remote, m_udp_port);
    handle_ready();
}

heartbeat_service::~heartbeat_service() {
}

bool heartbeat_service::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(m_udp.get()) && result;
    result = handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); }) && result;
    result = handler.register_event_handler(&m_error_timer, [this](auto&) { handle_error_timer(); }) && result;
    return result;
}

bool heartbeat_service::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(m_udp.get()) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    result = handler.unregister_event_handler(&m_error_timer) && result;
    return result;
}

void heartbeat_service::handle_error_timer() {
    if (m_udp_on_error) {
        if (m_udp) {
            m_udp->reset();
        }
    }
}

void heartbeat_service::handle_heartbeat_timer() {
    if (not m_udp_on_error && m_udp) {
        everest::lib::io::udp::udp_payload payload;
        utilities::struct_to_vector(m_config_message, payload.buffer);
        m_udp->tx(payload);
    }
    auto timeout = std::chrono::steady_clock::now() - m_last_heartbeat_reply > m_connection_to;
    if (timeout and m_cb_connected) {
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", 1) << "ChargeBridge connection lost" << std::endl;
        m_cb_connected = false;
        handle_ready();
    }

    else if (not timeout and not m_cb_connected) {
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", 0) << "ChargeBridge connected" << std::endl;
        m_cb_connected = true;
        handle_ready();
    }
    if (m_publish_connection_status) {
        m_publish_connection_status(m_cb_connected);
    }
}

void heartbeat_service::handle_ready() {
    m_ready.set(m_cb_connected and m_udp_ready);
}

bool heartbeat_service::available() const {
    return m_ready;
}

int heartbeat_service::mcu_reset_count() const {
    return m_mcu_reset_count;
}

std::uint8_t heartbeat_service::latched_cb_type() const {
    return m_latched_cb_type;
}

std::uint8_t heartbeat_service::link_technology() const {
    return m_link_technology;
}

std::optional<utilities::chargebridge_telemetry> heartbeat_service::latest_telemetry() const {
    if (not m_have_telemetry) {
        return std::nullopt;
    }
    return m_telemetry;
}

// Host and MCU always ship together, so this parser exists for exactly one reason: never let
// unexpected bytes on the socket take the process down. The 2-byte type field decides first and the
// size is checked per message type, so a datagram that is too short to even carry a type, one whose
// type this build does not know, and one whose length does not match its type are all rejected with a
// log line instead of a memcpy past the end of the buffer.
void heartbeat_service::handle_udp_rx(everest::lib::io::udp::udp_payload const& payload) {
    CbStructType type;
    if (payload.size() < sizeof(type)) {
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", -1)
            << "TRUNCATED UDP RX of HEARTBEAT: " << payload.size() << " vs " << sizeof(type) << std::endl;
        return;
    }
    std::memcpy(&type, payload.buffer.data(), sizeof(type));

    switch (type) {
    case CbStructType::CST_CbToHost_Heartbeat:
        handle_heartbeat_reply(payload);
        return;
    default:
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", -1)
            << "UNEXPECTED MESSAGE TYPE in UDP RX of HEARTBEAT: " << static_cast<int>(type) << std::endl;
        return;
    }
}

void heartbeat_service::handle_heartbeat_reply(everest::lib::io::udp::udp_payload const& payload) {
    CbManagementPacket<CbHeartbeatReplyPacket> data;
    if (payload.size() not_eq sizeof(data)) {
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", -1)
            << "INVALID DATA SIZE in UDP RX of HEARTBEAT: " << payload.size() << " vs " << sizeof(data) << std::endl;
        return;
    }
    std::memcpy(&data, payload.buffer.data(), sizeof(data));

    // Owned by another host's session (cb-session-v1): not liveness, and none of the measurement,
    // link-status or role fields below are valid in such a reply. Report the owner once per episode.
    if (data.data.session_status == static_cast<uint8_t>(CbSessionStatus::CBSS_RejectedBusy)) {
        if (not m_session_rejected) {
            utilities::print_error(m_identifier, "HEARTBEAT/UDP", -1)
                << "ChargeBridge is owned by another host (" << format_ipv4(data.data.owner_ip_v4) << ", session "
                << std::hex << data.data.owner_session_id << std::dec
                << "); set heartbeat.force_takeover to steal it" << std::endl;
            m_session_rejected = true;
        }
        return;
    }
    if (m_session_rejected) {
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", 0) << "ChargeBridge session accepted" << std::endl;
        m_session_rejected = false;
    }

    m_last_heartbeat_reply = std::chrono::steady_clock::now();
    auto mcu_current = static_cast<uint32_t>(data.data.uptime_ms);
    if (utilities::mcu_rebooted(m_mcu_timestamp, mcu_current)) {
        m_mcu_reset_count++;
        utilities::print_error(m_identifier, "HEARTBEAT/UDP", -1)
            << "ChargeBridge reset count " << m_mcu_reset_count << std::endl;
        m_ready_notify.notify();
        // A rebooted MCU has no valid PHY state until it reports again, and its link-status
        // transition counter restarted. Synthesize an all-zero report so a carrier consumer drops the
        // link now instead of waiting for the next report or the connection timeout.
        if (m_publish_link_status) {
            m_publish_link_status(CbLinkStatusPacket{});
        }
        // A reboot is the only thing that can change the latched role, so it is also the only reason
        // to say it again.
        m_role_mismatch_reported = false;
    }
    m_mcu_timestamp = mcu_current;

    // Snapshot the numeric telemetry for the interactive terminal UI (live readouts/sparklines).
    // This runs on the event loop thread, the same thread that reads it via get_status().
    m_telemetry.cp_hi_mV = data.data.cp_hi_mV;
    m_telemetry.cp_lo_mV = data.data.cp_lo_mV;
    m_telemetry.pp_mOhm = data.data.pp_mOhm;
    m_telemetry.temperature_mcu_C = data.data.temperature_mcu_C;
    m_telemetry.temperature_pcb_C = data.data.temperature_pcb_C;
    m_telemetry.vdd_12V_mV = data.data.vdd_12V;
    m_telemetry.vdd_N12V_mV = data.data.vdd_N12V;
    m_telemetry.vdd_3v3_mV = data.data.vdd_3v3;
    m_have_telemetry = true;

    // The MCU's PHY state rides inside every reply, always populated and on every board. Published
    // after the reboot check above so a reply that reports a restart drops the carrier through the
    // synthesized all-zero report first and only then applies the fresh snapshot - the counters in it
    // belong to the new boot, and forwarding them the other way round would leave a carrier raised
    // from a stale state.
    if (m_publish_link_status) {
        m_publish_link_status(data.data.link_status);
    }

    // The role the MCU actually runs, and the technology that says who owns it. On an MCS board the
    // role is latched from the first config after boot, so a reboot applies a changed type; on a CCS
    // board (technology PLC) the straps decide and no reboot will ever change the answer, which makes
    // the remedy different advice for the same symptom. An unconfigured type cannot disagree with
    // anything, so it produces no report at all.
    m_latched_cb_type = data.data.latched_cb_type;
    m_link_technology = data.data.link_status.technology;
    auto const configured = m_config_message.data.module_config.cb_type;
    if (evaluate_role_latch(configured, m_latched_cb_type) == role_latch_state::mismatched and
        not m_role_mismatch_reported) {
        m_role_mismatch_reported = true;
        utilities::print_error(m_identifier, "HEARTBEAT/ROLE", -1)
            << "configured charge_bridge.type is " << cb_type_name(configured) << " but the ChargeBridge is running "
            << cb_type_name(m_latched_cb_type) << ": " << role_mismatch_remedy(m_link_technology) << std::endl;
    }
}

} // namespace charge_bridge
