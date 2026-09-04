// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <charge_bridge/can_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/netlink/vcan_netlink_manager.hpp>
#include <memory>
#include <protocol/cb_can_message.h>

namespace charge_bridge {
using namespace std::chrono_literals;

namespace {

void msg_cb_to_host(cb_can_message const& src, everest::lib::io::can::socket_can::ClientPayloadT& tar) {
    tar.set_can_id_with_flags(src.can_id, src.can_flags & CanFlags_EFF, src.can_flags & CanFlags_RTR,
                              src.can_flags & CanFlags_ERR);
    tar.len8_dlc = 0;
    // dlc is wire data: clamp to the data array size so a bogus value can't over-read past src.data.
    auto const len = std::min<std::size_t>(src.dlc, sizeof(src.data));
    tar.payload.resize(len);
    std::memcpy(tar.payload.data(), src.data, len);
}

void msg_host_to_cb(everest::lib::io::can::socket_can::ClientPayloadT const& src, cb_can_message& tar) {
    tar = cb_can_message_set_zero;
    tar.can_id = src.get_can_id();
    tar.can_flags = 0;
    if (src.eff_flag()) {
        tar.can_flags |= CanFlags_EFF;
    }
    if (src.rtr_flag()) {
        tar.can_flags |= CanFlags_RTR;
    }
    if (src.err_flag()) {
        tar.can_flags |= CanFlags_ERR;
    }
    tar.dlc = std::min<uint8_t>(src.payload.size(), sizeof(tar.data));
    std::memcpy(tar.data, src.payload.data(), src.payload.size());
}

bool is_data_msg([[maybe_unused]] cb_can_message const& msg) {
    return true;
}

// A CAN UDP datagram carries 1..12 cb_can_message records (firmware: can_udp_handler.cpp); 12
// fits the MCU's 1280 B packet buffer.
constexpr std::size_t can_udp_batch_max = 12;

// Keep the MCU's CAN slot alive while the host->bus direction is idle: a keep-alive once no
// datagram went out for can_keepalive_idle, checked every can_keepalive_poll, so the idle gap
// is 4..5 s against the MCU's 20 s slot timeout. (A 10 s timer with a 10 s condition gave gaps
// of up to 20 s and tripped that timeout.)
constexpr auto can_keepalive_idle = std::chrono::seconds(4);
constexpr auto can_keepalive_poll = std::chrono::seconds(1);
// A keep-alive after a gap this long means the poll itself was late: worth a line.
constexpr auto can_keepalive_gap_warn = std::chrono::seconds(10);
// Bus emulation on the vcan: a tbf qdisc meters producers' writes so they feel the bus as on a
// real controller; this bridge's own writes (frames from the bus) bypass it via SO_PRIORITY. tbf
// charges 16 B per classic frame regardless of DLC while the wire costs 55..150 bits, so the
// rate is scaled to bitrate * 128 / avg_wire_bits from the forwarded frames (EWMA), re-applied
// at most once per vcan_rate_update_period when the mix moved by vcan_rate_hysteresis_pct.
// limit 1024 B = 64 classic frames ~ 8 ms at 1 Mbit/s.
constexpr std::uint32_t vcan_tbf_bits_per_classic_frame = 16 * 8;
// Headroom below the estimated capacity: stuffing depends on the payload; 100 % and 2 % still
// overflowed the CB's 256-frame TX ring in soak tests, 5 % did not.
constexpr unsigned vcan_utilisation_pct = 95;
constexpr unsigned vcan_rate_hysteresis_pct = 3;
constexpr auto vcan_rate_update_period = std::chrono::seconds(1);
// Typical stuffing overhead for varied payloads (~1 bit per 16). Identical payloads stuff ~4x
// more; raise if such a flood must run at full rate (tx_ring_drops on the MCU shows it).
constexpr unsigned stuffing_permille = 65;
// Wire cost assumed before the first frame: conservative (extended ID, 8 bytes, generous
// stuffing). A too-high guess lost frames right after start-up.
constexpr std::size_t initial_avg_wire_bits = 150;
// Decreases of the rate are applied immediately (a small hysteresis against jitter);
// increases wait for vcan_rate_update_period and vcan_rate_hysteresis_pct.
constexpr unsigned vcan_rate_decrease_hysteresis_pct = 1;
constexpr std::uint32_t vcan_burst_bytes = 256;
constexpr std::uint32_t vcan_limit_bytes = 1024;
// In-bridge pacing is the fallback when the qdisc cannot be installed (no CAP_NET_ADMIN); off
// while the qdisc is in place.
constexpr unsigned pace_utilisation_pct = 95;
constexpr unsigned pace_bucket_ms = 15;

} // namespace

can_bridge::can_bridge(can_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_cb_port(config.cb_port),
    m_cb_remote(config.cb_remote),
    m_can_device(config.can_device),
    m_last_msg_to_cb(std::chrono::steady_clock::time_point()),
    m_ready_notify(ready_notify) {

    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    // The manager is application agnostic and reports to std::cerr by default, which corrupts the
    // terminal status output (the vcan setup needs CAP_NET_ADMIN, so the failure is common and is
    // repeated on every retry). Route it through the print sink instead. The handler is stateless
    // apart from a copied identifier - the message already names the interface - so it stays valid
    // for the lifetime of the manager singleton, which outlives this bridge.
    manager.set_error_handler([identifier = config.cb + "/" + config.item](std::string const& message) {
        utilities::print_error(identifier, "CAN/NETLINK", 1) << message << std::endl;
    });

    // This socket emulates the bus: the receive queue holds ~150 ms of a 1 Mbit/s bus for
    // scheduling jitter (reads are paced), its writes bypass the shaper.
    everest::lib::io::can::socket_can_options can_options;
    can_options.receive_buffer_bytes = 1 * 1024 * 1024;
    can_options.socket_priority = everest::lib::io::netlink::vcan_netlink_manager::unshaped_socket_priority();

    auto success = manager.create(config.can_device) && manager.bring_up(config.can_device);
    if (success) {
        m_vcan_shaped = install_vcan_rate_limit(config);
        m_can = std::make_unique<everest::lib::io::can::socket_can>(
            config.can_device, std::vector<everest::lib::io::can::can_recv_filter>{}, can_options);
    } else {
        manager.destroy(config.can_device);
        success = manager.create(config.can_device) && manager.bring_up(config.can_device);
        if (success) {
            m_vcan_shaped = install_vcan_rate_limit(config);
            m_can = std::make_unique<everest::lib::io::can::socket_can>(
                config.can_device, std::vector<everest::lib::io::can::can_recv_filter>{}, can_options);
        } else {
            manager.destroy(config.can_device);
            throw std::runtime_error("Failed to setup virtual CAN device: " + config.can_device);
        }
    }

    m_can_batch.buffer.reserve(can_udp_batch_max * sizeof(cb_can_message));
    m_can_batch_timer.set_single_shot(true);
    m_pace_bitrate_bps = config.can_bitrate_bps;
    reset_pacing();

    m_can->set_rx_handler([this](auto const& data, auto&) {
        // Opportunistic batching alone yields 1.0-1.2 frames/datagram on a smooth flood; a 3 ms
        // hold-off cuts the MCU's per-datagram cost up to 12x for at most 3 ms latency. Queued
        // frames drain in the same pass (rx returns false on EAGAIN).
        // Pacing may leave the batch full and the socket paused; the backlog waits in the kernel
        // queue.
        append_to_can_batch(data);
        auto const& raw = m_can->get_raw_handler();
        everest::lib::io::can::socket_can::ClientPayloadT more;
        while (m_can_batch.buffer.size() < can_udp_batch_max * sizeof(cb_can_message) and raw and raw->rx(more)) {
            append_to_can_batch(more);
        }
        if (m_can_batch.buffer.size() >= can_udp_batch_max * sizeof(cb_can_message)) {
            flush_can_batch();
        } else if (not m_can_batch_timer_armed) {
            m_can_batch_timer.set_timeout(std::chrono::milliseconds(3));
            m_can_batch_timer_armed = true;
        }
    });

    create_udp_client(config.cb_remote, config.cb_port);

    auto identifier = config.cb + "/" + config.item;
    m_identifier = identifier;
    m_can->set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "CAN/HW", id) << msg << std::endl;
        m_can_ready = id == 0;
        if (not m_can_ready) {
            // This is a smart pointer!! Using .reset() would delete the obj!
            m_can->reset();
        }
        handle_ready();
    });
    m_heartbeat_timer.set_timeout(can_keepalive_poll);
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

void can_bridge::create_udp_client(std::string const& remote, uint16_t remote_port) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port);
    m_udp->set_rx_handler([this](auto const& data, auto&) {
        // 1..N full-size records; the trailing data bytes of the last record may be omitted (see
        // cb_can_message).
        // Copy only the received bytes.
        static constexpr auto header_size = offsetof(cb_can_message, data);
        std::size_t off = 0;
        while (off + header_size <= data.size()) {
            auto const chunk = std::min(data.size() - off, sizeof(cb_can_message));
            everest::lib::io::can::socket_can::ClientPayloadT pl;
            cb_can_message msg = cb_can_message_set_zero;
            std::memcpy(&msg, data.buffer.data() + off, chunk);

            msg_cb_to_host(msg, pl);
            if (is_data_msg(msg)) {
                m_can->tx(pl);
            }
            off += sizeof(cb_can_message);
        }
    });
    m_udp->set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "CAN/UDP", id) << msg << std::endl;
        m_udp_ready = id == 0;
        if (not m_udp_ready) {
            if (m_udp) {
                m_udp->reset();
            }
        }
        handle_ready();
    });
}

void can_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_last_msg_to_cb = std::chrono::steady_clock::time_point{};
    // Frames collected for the endpoint this disconnect retires.
    m_can_batch.buffer.clear();
    m_can_batch_timer.disarm();
    m_can_batch_timer_armed = false;
    reset_pacing();
    if (m_udp) {
        m_udp->reset();
    }
    m_udp.reset();
    handle_ready();
}

void can_bridge::connect_cb_endpoint(std::string const& remote) {
    m_cb_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_cb_remote, m_cb_port);
    handle_ready();
}

can_bridge::~can_bridge() {
    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    if (m_can) {
        m_can.reset();
        manager.destroy(m_can_device);
    }
}

bool can_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.register_event_handler(m_can.get());
    result = handler.register_event_handler(m_udp.get()) && result;
    result = handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); }) && result;
    result = handler.register_event_handler(&m_can_batch_timer, [this](auto&) { flush_can_batch(); }) && result;

    if (result) {
        handler.add_action([this]() { handle_heartbeat_timer(); });
    }

    return result;
}

bool can_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.unregister_event_handler(m_can.get());
    result = handler.unregister_event_handler(m_udp.get()) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    result = handler.unregister_event_handler(&m_can_batch_timer) && result;
    return result;
}

void can_bridge::send_can_to_udp(cb_can_message const& msg) {
    if (not m_udp) {
        return;
    }
    everest::lib::io::udp::udp_client::ClientPayloadT udp_pl;
    udp_pl.buffer.resize(sizeof(cb_can_message));
    std::memcpy(udp_pl.buffer.data(), &msg, sizeof(cb_can_message));
    m_udp->tx(udp_pl);
    m_last_msg_to_cb = std::chrono::steady_clock::now();
}

void can_bridge::append_to_can_batch(everest::lib::io::can::socket_can::ClientPayloadT const& frame) {
    cb_can_message msg;
    msg_host_to_cb(frame, msg);
    auto const off = m_can_batch.buffer.size();
    m_can_batch.buffer.resize(off + sizeof(cb_can_message));
    std::memcpy(m_can_batch.buffer.data() + off, &msg, sizeof(cb_can_message));
    auto const bits = frame_wire_bits(msg);
    m_can_batch_bits += bits;
    // Running average of the real wire cost per frame (EWMA, 1/64) for the bus emulation.
    m_avg_wire_bits += (static_cast<double>(bits) - m_avg_wire_bits) / 64.0;
}

std::size_t can_bridge::frame_wire_bits(cb_can_message const& msg) {
    // Payload bytes from the DLC code (FD codes 9..15 map to 12..64 bytes). FD data is
    // counted at the nominal rate, which over-estimates BRS frames -- conservative.
    static constexpr std::size_t fd_len[] = {12, 16, 20, 24, 32, 48, 64};
    std::size_t const bytes = msg.dlc <= 8 ? msg.dlc : fd_len[std::min<std::size_t>(msg.dlc - 9, 6)];
    // Fixed part: SOF(1)+ID(11)+RTR(1)+IDE(1)+r0(1)+DLC(4)+CRC(15)+CRC delim(1)+ACK(2)+
    // EOF(7)+IFS(3) = 47 bits; a 29-bit ID adds 18 ID bits + SRR + r1 = 20.
    bool const extended = (msg.can_flags & CanFlags_EFF) != 0;
    std::size_t const fixed = (extended ? 67 : 47) + 8 * bytes;
    // Bit stuffing applies from SOF through the CRC sequence, i.e. everything but the
    // CRC delimiter, ACK, EOF and IFS (13 bits).
    std::size_t const stuffable = fixed - 13;
    return fixed + (stuffable * stuffing_permille) / 1000;
}

// tbf rate (16 B per frame accounting) matching the physical frame rate for avg_wire_bits per
// frame.
std::uint64_t can_bridge::vcan_rate_for(double avg_wire_bits) const {
    double const bits = std::max(avg_wire_bits, 1.0);
    return static_cast<std::uint64_t>(static_cast<double>(m_pace_bitrate_bps) * vcan_tbf_bits_per_classic_frame *
                                      vcan_utilisation_pct / 100.0 / bits);
}

// Re-scale the vcan's tbf to the traffic actually flowing (see the notes at the top).
void can_bridge::update_vcan_rate_limit() {
    if (not m_vcan_shaped or m_pace_bitrate_bps == 0) {
        return;
    }
    auto const now = std::chrono::steady_clock::now();
    auto const wanted = vcan_rate_for(m_avg_wire_bits);
    auto const installed = m_vcan_rate_bps;
    if (wanted < installed) {
        // Admitting more than the bus carries loses frames at the CB: lower the rate at once.
        if ((installed - wanted) * 100 < installed * vcan_rate_decrease_hysteresis_pct) {
            return;
        }
    } else {
        // Admitting less only costs throughput: raise it calmly.
        if (now - m_vcan_rate_updated < vcan_rate_update_period) {
            return;
        }
        if (installed != 0 and (wanted - installed) * 100 < installed * vcan_rate_hysteresis_pct) {
            return;
        }
    }
    m_vcan_rate_updated = now;
    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    if (manager.set_transmit_rate_limit(m_can_device, wanted, vcan_burst_bytes, vcan_limit_bytes)) {
        m_vcan_rate_bps = wanted;
        utilities::print_info(m_identifier, "CAN/BUS")
            << "bus emulation: " << static_cast<unsigned>(m_avg_wire_bits) << " wire bits/frame -> vcan rate " << wanted
            << " bit/s (" << (wanted * 100 / m_pace_bitrate_bps) << " % of " << m_pace_bitrate_bps << ")" << std::endl;
    }
}

// Best effort: on failure the in-bridge pacing remains.
bool can_bridge::install_vcan_rate_limit(can_bridge_config const& config) {
    if (config.can_bitrate_bps == 0) {
        return false; // bitrate unknown: nothing sensible to install
    }
    m_pace_bitrate_bps = config.can_bitrate_bps;
    m_avg_wire_bits = initial_avg_wire_bits;
    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    auto const rate = vcan_rate_for(m_avg_wire_bits);
    if (not manager.set_transmit_rate_limit(config.can_device, rate, vcan_burst_bytes, vcan_limit_bytes)) {
        return false;
    }
    m_vcan_rate_bps = rate;
    m_vcan_rate_updated = std::chrono::steady_clock::now();
    return true;
}

void can_bridge::reset_pacing() {
    m_can_batch_bits = 0;
    // Start with a full bucket so the first batch after (re)connect is not delayed.
    m_pace_tokens_bits = m_pace_bitrate_bps ? (static_cast<double>(m_pace_bitrate_bps) * pace_bucket_ms / 1000.0) : 0.0;
    m_pace_last = std::chrono::steady_clock::now();
    if (m_can_rx_paused and m_can) {
        m_can->resume_rx();
    }
    m_can_rx_paused = false;
}

// UDP gives the CB no way to push back and its buffers hold ~35 ms of bus time; bursts beyond
// that showed as tx_ring_drops. The batch ships only when the token bucket (pace_utilisation
// of the bitrate) has enough bits; meanwhile the CAN socket is paused and the backlog waits in
// the kernel queue.
void can_bridge::flush_can_batch() {
    m_can_batch_timer.disarm();
    m_can_batch_timer_armed = false;
    if (m_can_batch.buffer.empty()) {
        return;
    }
    if (m_pace_bitrate_bps != 0 and not m_vcan_shaped) {
        auto const now = std::chrono::steady_clock::now();
        double const rate = static_cast<double>(m_pace_bitrate_bps) * pace_utilisation_pct / 100.0; // bit/s
        double const cap = rate * pace_bucket_ms / 1000.0;
        auto const elapsed = std::chrono::duration<double>(now - m_pace_last).count();
        m_pace_last = now;
        m_pace_tokens_bits = std::min(cap, m_pace_tokens_bits + elapsed * rate);
        if (m_pace_tokens_bits < static_cast<double>(m_can_batch_bits)) {
            auto const wait_s = (static_cast<double>(m_can_batch_bits) - m_pace_tokens_bits) / rate;
            auto wait = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(wait_s));
            wait = std::max(wait, std::chrono::nanoseconds(std::chrono::microseconds(100)));
            m_can_batch_timer.set_timeout(wait);
            m_can_batch_timer_armed = true;
            if (not m_can_rx_paused and m_can) {
                m_can_rx_paused = m_can->pause_rx();
            }
            return;
        }
        m_pace_tokens_bits -= static_cast<double>(m_can_batch_bits);
    }
    if (m_udp) {
        m_udp->tx(m_can_batch);
        m_last_msg_to_cb = std::chrono::steady_clock::now();
    }
    // No UDP client: dropped, as before.
    m_can_batch.buffer.clear();
    m_can_batch_bits = 0;
    update_vcan_rate_limit();
    if (m_can_rx_paused and m_can) {
        m_can->resume_rx();
        m_can_rx_paused = false;
    }
}

void can_bridge::handle_heartbeat_timer() {
    if (not m_udp or m_udp->on_error()) {
        // If the connection is not available, retry soon and invalidate last hearbeat
        m_heartbeat_timer.set_timeout(250ms);
        m_last_msg_to_cb = std::chrono::steady_clock::time_point();
        return;
    } else {
        // otherwise go back to regular interval
        m_heartbeat_timer.set_timeout(can_keepalive_poll);
    }
    auto delta = std::chrono::steady_clock::now() - m_last_msg_to_cb;
    if (delta >= can_keepalive_idle) {
        if (m_last_msg_to_cb != std::chrono::steady_clock::time_point{} and delta > can_keepalive_gap_warn) {
            utilities::print_error(m_identifier, "CAN/UDP", -1)
                << "no datagram to the CB for " << std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()
                << " ms; keep-alive sent" << std::endl;
        }
        cb_can_message msg = cb_can_message_set_zero;
        msg.packet_type = CanPacketType_Keep_Alive;
        send_can_to_udp(msg);
    }
}

void can_bridge::handle_ready() {
    m_ready.set(m_udp_ready and m_can_ready and m_cb_is_connected);
}

bool can_bridge::available() const {
    return m_ready;
}

void can_bridge::set_cb_connection_status(bool connected) {
    m_cb_is_connected.set(connected);
}

} // namespace charge_bridge
