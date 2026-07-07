// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/io_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/platform_utils.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <protocol/cb_management.h>
#include <stdexcept>
#include <string>

namespace charge_bridge {
using namespace std::chrono_literals;
namespace mqtt = everest::lib::io::mqtt;

namespace {
const int default_udp_timeout_ms = 1000;
const int mqtt_reconnect_timeout_ms = 1000;
} // namespace

io_bridge::io_bridge(io_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_udp_port(config.cb_port),
    m_udp_remote(config.cb_remote),
    m_mqtt(mqtt_reconnect_timeout_ms),
    m_ready_notify(ready_notify)

{
    m_identifier = config.cb + "/" + config.item;

    m_heartbeat_timer.set_timeout(std::chrono::seconds(config.interval_s));

    create_udp_client(m_udp_remote, m_udp_port);

    m_receive_topic = "pionix/chargebridge/" + config.cb + "/gpio/output/";
    m_ws28_receive_topic = "pionix/chargebridge/" + config.cb + "/ws28/output/";
    m_ws28_anim_receive_topic = "pionix/chargebridge/" + config.cb + "/ws28/anim/";
    m_send_topic = "pionix/chargebridge/" + config.cb + "/gpio/input/";
    m_adc_send_topic = "pionix/chargebridge/" + config.cb + "/adc/input/";
    m_telemetry_send_topic = "pionix/chargebridge/" + config.cb + "/telemetry/";

    m_mqtt.set_error_handler([this, config](int id, std::string const& msg) {
        utilities::print_error(m_identifier, "IO/MQTT", id) << msg << std::endl;
        m_mqtt_on_error = id not_eq 0;
        m_mqtt_ready = id == 0;
        handle_ready();
    });

    m_mqtt.set_callback_connect([this](auto&, auto, auto, auto const&) {
        m_mqtt.subscribe(
            m_receive_topic + "#", [this](auto&, auto const& payload) { dispatch(payload); },
            everest::lib::io::mqtt::mqtt_client::QoS::at_most_once);
        m_mqtt.subscribe(
            m_ws28_receive_topic + "#", [this](auto&, auto const& payload) { dispatch_ws28(payload); },
            everest::lib::io::mqtt::mqtt_client::QoS::at_most_once);
        m_mqtt.subscribe(
            m_ws28_anim_receive_topic + "#",
            [this](auto&, auto const& payload) { dispatch_ws28_anim(payload); },
            everest::lib::io::mqtt::mqtt_client::QoS::at_most_once);
    });

    m_mqtt.connect(config.mqtt_bind, config.mqtt_remote, config.mqtt_port, config.mqtt_ping_interval_ms);

    m_message.type = CbStructType::CST_HostToCb_Gpio;
    m_message.data.number_of_gpios = CB_NUMBER_OF_GPIOS;
    std::memset(m_message.data.gpio_values, 0, sizeof(m_message.data.gpio_values));

    m_ws28_message.type = CbStructType::CST_HostToCb_Ws28;
    m_ws28_message.data.gpio_index = 8; // only WS28-capable pin today
    m_ws28_message.data.reserved = 0;
    m_ws28_message.data.led_count = 0;
    std::memset(m_ws28_message.data.rgb, 0, sizeof(m_ws28_message.data.rgb));

    m_ws28_anim_message.type = CbStructType::CST_HostToCb_Ws28Anim;
    std::memset(&m_ws28_anim_message.data, 0, sizeof(m_ws28_anim_message.data));
    m_ws28_anim_message.data.gpio_index = 8; // only WS28-capable pin today

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

void io_bridge::create_udp_client(std::string const& remote, uint16_t remote_port) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port, default_udp_timeout_ms);
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp->set_rx_handler([this](auto const& data, auto&) { handle_udp_rx(data); });
    m_udp->set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "IO/UDP", id) << msg << std::endl;
        m_udp_on_error = id not_eq 0;
        m_udp_ready = id == 0;
        handle_ready();
    });
}

void io_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_udp_on_error = true;
    if (m_udp) {
        m_udp->reset();
    }
    m_udp.reset();
    handle_ready();
}

void io_bridge::connect_cb_endpoint(std::string const& remote) {
    m_udp_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_udp_remote, m_udp_port);
    handle_ready();
}

io_bridge::~io_bridge() {
}

bool io_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.register_event_handler(m_udp.get());
    result = handler.register_event_handler(&m_mqtt) && result;
    result = handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); }) && result;
    return result;
}

bool io_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.unregister_event_handler(m_udp.get());
    result = handler.unregister_event_handler(&m_mqtt) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    return result;
}

void io_bridge::dispatch(everest::lib::io::mqtt::mqtt_client::message const& data) {
    auto& topic = data.topic;
    auto& payload = data.payload;
    auto operation = utilities::string_after_pattern(topic, m_receive_topic);
    uint16_t value = 0;
    int id = 0;

    auto stous = [](std::string const& data) {
        auto val = stoi(data);
        if (val < 0 or val > std::numeric_limits<uint16_t>::max()) {
            throw std::range_error("");
        }
        return static_cast<uint16_t>(val);
    };

    try {
        value = stous(payload);
    } catch (...) {
        std::cout << "INVALID DATA on MQTT for GPIO DATA" << std::endl;
        return;
    }
    try {
        id = std::stoi(operation);
    } catch (...) {
        std::cout << "INVALID DATA on MQTT for GPIO ID" << std::endl;
        return;
    }
    if (id < 0 or id >= CB_NUMBER_OF_GPIOS) {
        std::cout << "INVALID GPIO ID" << std::endl;
        return;
    }

    m_message.data.gpio_values[id] = value;
    send_udp();
}

void io_bridge::dispatch_ws28(everest::lib::io::mqtt::mqtt_client::message const& data) {
    auto& topic = data.topic;
    auto& payload = data.payload;
    auto operation = utilities::string_after_pattern(topic, m_ws28_receive_topic);

    int gpio_id = 0;
    try {
        gpio_id = std::stoi(operation);
    } catch (...) {
        std::cout << "INVALID GPIO ID on MQTT for WS28 DATA" << std::endl;
        return;
    }
    if (gpio_id < 0 or gpio_id >= CB_NUMBER_OF_GPIOS) {
        std::cout << "INVALID GPIO ID for WS28 DATA" << std::endl;
        return;
    }

    // Payload is a hex string, 6 chars per LED ("RRGGBB"), no separators.
    if (payload.size() % 6 != 0) {
        std::cout << "INVALID WS28 PAYLOAD (length must be a multiple of 6 hex chars)" << std::endl;
        return;
    }
    auto const led_count = payload.size() / 6;
    if (led_count > CB_WS28_MAX_LEDS) {
        std::cout << "WS28 PAYLOAD too long: " << led_count << " > " << CB_WS28_MAX_LEDS << std::endl;
        return;
    }

    auto hex_nibble = [](char c) -> int {
        if (c >= '0' and c <= '9') {
            return c - '0';
        }
        if (c >= 'a' and c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' and c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };

    auto const byte_count = led_count * 3;
    for (std::size_t i = 0; i < byte_count; ++i) {
        auto hi = hex_nibble(payload[2 * i]);
        auto lo = hex_nibble(payload[2 * i + 1]);
        if (hi < 0 or lo < 0) {
            std::cout << "INVALID WS28 PAYLOAD (non-hex character)" << std::endl;
            return;
        }
        m_ws28_message.data.rgb[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    m_ws28_message.data.gpio_index = static_cast<uint8_t>(gpio_id);
    m_ws28_message.data.led_count = static_cast<uint16_t>(led_count);
    send_ws28_udp();
}

void io_bridge::dispatch_ws28_anim(everest::lib::io::mqtt::mqtt_client::message const& data) {
    auto& topic = data.topic;
    auto& payload = data.payload;
    auto operation = utilities::string_after_pattern(topic, m_ws28_anim_receive_topic);

    int gpio_id = 0;
    try {
        gpio_id = std::stoi(operation);
    } catch (...) {
        std::cout << "INVALID GPIO ID on MQTT for WS28 ANIM" << std::endl;
        return;
    }
    if (gpio_id < 0 or gpio_id >= CB_NUMBER_OF_GPIOS) {
        std::cout << "INVALID GPIO ID for WS28 ANIM" << std::endl;
        return;
    }

    // style name -> Ws28AnimStyle value (must match the enum in ws28_led.hpp / the comment in
    // cb_management.h). Names are also accepted as a plain integer.
    static const std::vector<std::string> style_names = {
        "static", "blink",   "breathe",       "wipe",    "theater",  "scanner",
        "comet",  "rainbow", "rainbow_chase", "sparkle", "gradient", "fire"};

    // "RRGGBB" hex -> packed bytes; returns false on a malformed colour.
    auto parse_color = [](std::string const& s, uint8_t& r, uint8_t& g, uint8_t& b) -> bool {
        if (s.size() != 6) {
            return false;
        }
        try {
            auto v = std::stoul(s, nullptr, 16);
            r = static_cast<uint8_t>((v >> 16) & 0xFF);
            g = static_cast<uint8_t>((v >> 8) & 0xFF);
            b = static_cast<uint8_t>(v & 0xFF);
        } catch (...) {
            return false;
        }
        // reject non-hex characters that stoul may have skipped/stopped at
        return s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    };

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload);
    } catch (...) {
        std::cout << "INVALID WS28 ANIM PAYLOAD (not JSON)" << std::endl;
        return;
    }

    // style: accept a name string or an integer index.
    uint8_t style = 0;
    if (j.contains("style")) {
        auto const& s = j.at("style");
        if (s.is_string()) {
            auto name = s.get<std::string>();
            auto it = std::find(style_names.begin(), style_names.end(), name);
            if (it == style_names.end()) {
                std::cout << "INVALID WS28 ANIM style name: " << name << std::endl;
                return;
            }
            style = static_cast<uint8_t>(std::distance(style_names.begin(), it));
        } else if (s.is_number_integer()) {
            auto v = s.get<int>();
            if (v < 0 or v >= static_cast<int>(style_names.size())) {
                std::cout << "INVALID WS28 ANIM style index: " << v << std::endl;
                return;
            }
            style = static_cast<uint8_t>(v);
        }
    }

    auto clamp_u8 = [](nlohmann::json const& j, char const* key, uint8_t def) -> uint8_t {
        if (not j.contains(key) or not j.at(key).is_number()) {
            return def;
        }
        auto v = j.at(key).get<int>();
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };

    auto& d = m_ws28_anim_message.data;
    d.gpio_index = static_cast<uint8_t>(gpio_id);
    d.style = style;
    d.speed = clamp_u8(j, "speed", 128);
    d.brightness = clamp_u8(j, "brightness", 255);
    d.param = clamp_u8(j, "param", 0);
    d.flags = clamp_u8(j, "flags", 0);

    d.r1 = d.g1 = d.b1 = 0;
    d.r2 = d.g2 = d.b2 = 0;
    if (j.contains("color") and j.at("color").is_string()) {
        if (not parse_color(j.at("color").get<std::string>(), d.r1, d.g1, d.b1)) {
            std::cout << "INVALID WS28 ANIM color (expected RRGGBB hex)" << std::endl;
            return;
        }
    }
    if (j.contains("color2") and j.at("color2").is_string()) {
        if (not parse_color(j.at("color2").get<std::string>(), d.r2, d.g2, d.b2)) {
            std::cout << "INVALID WS28 ANIM color2 (expected RRGGBB hex)" << std::endl;
            return;
        }
    }

    send_ws28_anim_udp();
}

void io_bridge::send_mqtt(std::string const& topic, std::string const& message) {
    everest::lib::io::mqtt::mqtt_client::message payload;
    payload.topic = m_send_topic + topic;
    payload.payload = message;
    m_mqtt.publish(payload);
}

void io_bridge::send_adc_mqtt(std::string const& topic, std::string const& message) {
    everest::lib::io::mqtt::mqtt_client::message payload;
    payload.topic = m_adc_send_topic + topic;
    payload.payload = message;
    m_mqtt.publish(payload);
}

void io_bridge::send_telemetry_mqtt(std::string const& topic, std::string const& message) {
    everest::lib::io::mqtt::mqtt_client::message payload;
    payload.topic = m_telemetry_send_topic + topic;
    payload.payload = message;
    m_mqtt.publish(payload);
}

void io_bridge::send_udp() {
    if (not m_udp_on_error && m_udp) {
        everest::lib::io::udp::udp_payload payload;
        utilities::struct_to_vector(m_message, payload.buffer);
        m_udp->tx(payload);
    }
}

void io_bridge::send_ws28_udp() {
    if (not m_udp_on_error && m_udp) {
        everest::lib::io::udp::udp_payload payload;
        utilities::struct_to_vector(m_ws28_message, payload.buffer);
        m_udp->tx(payload);
    }
}

void io_bridge::send_ws28_anim_udp() {
    if (not m_udp_on_error && m_udp) {
        everest::lib::io::udp::udp_payload payload;
        utilities::struct_to_vector(m_ws28_anim_message, payload.buffer);
        m_udp->tx(payload);
    }
}

void io_bridge::handle_heartbeat_timer() {
    send_udp();
}

void io_bridge::handle_udp_rx(everest::lib::io::udp::udp_payload const& payload) {
    // Debug-UART forwarding: the MCU sends its printf output on this same IO connection as raw byte
    // chunks (CST_CbToHost_DebugUart), each possibly containing several '\n'-separated lines split at
    // an arbitrary boundary. Peek the type and handle it before the CbIoPacket decoding below, which
    // would otherwise reject it as an unexpected type.
    if (payload.size() >= sizeof(CbStructType)) {
        CbStructType peek_type{};
        std::memcpy(&peek_type, payload.buffer.data(), sizeof(peek_type));
        if (peek_type == CbStructType::CST_CbToHost_DebugUart) {
            CbManagementPacket<CbDebugUartLinePacket> dbg{};
            auto const hdr = sizeof(dbg.type) + sizeof(dbg.data.length);
            if (payload.size() >= hdr) {
                auto const copy_n = std::min(payload.size(), sizeof(dbg));
                std::memcpy(&dbg, payload.buffer.data(), copy_n);
                std::uint16_t len = dbg.data.length;
                if (len > CB_DEBUG_UART_LINE_MAX) {
                    len = CB_DEBUG_UART_LINE_MAX;
                }
                // Never trust the declared length past what actually arrived: a truncated or crafted
                // packet must not append uninitialized filler bytes to the reassembly buffer.
                auto const received_text = copy_n - hdr;
                if (len > received_text) {
                    len = static_cast<std::uint16_t>(received_text);
                }
                // Reassemble the byte stream and emit one log entry per complete line. Route through
                // print_info (not raw std::cout) so each line lands in the terminal UI's message
                // panel instead of being painted over by the ftxui redraw; in log mode it prints to
                // stdout like the other "[ unit ] device ..." lines.
                m_mcu_log_partial.append(dbg.data.text, len);
                std::size_t nl;
                while ((nl = m_mcu_log_partial.find('\n')) != std::string::npos) {
                    std::string line = m_mcu_log_partial.substr(0, nl);
                    m_mcu_log_partial.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    utilities::print_info(m_identifier, "MCU") << line << std::endl;
                }
                // Flush an over-long unterminated line so a missing newline can't grow it unbounded.
                constexpr std::size_t k_max_partial = 2048;
                if (m_mcu_log_partial.size() > k_max_partial) {
                    utilities::print_info(m_identifier, "MCU") << m_mcu_log_partial << std::endl;
                    m_mcu_log_partial.clear();
                }
            }
            return;
        }
    }

    CbManagementPacket<CbIoPacket> data{}; // zero-init so untransmitted telemetry slots stay empty

    // The telemetry tail is variable length: the MCU sends only the populated entries. The fixed
    // prefix is everything up to and including telemetry.number_of_entries; valid total lengths are
    // [fixed_prefix, sizeof(data)] and must match the entry count exactly.
    auto const entry_size = sizeof(CbTelemetryEntry);
    auto const fixed_prefix = sizeof(data) - sizeof(data.data.telemetry.entries);
    auto const size = payload.size();
    if (size < fixed_prefix || size > sizeof(data)) {
        std::cout << "INVALID DATA SIZE in UDP RX of IO: " << size << " (expected " << fixed_prefix << ".."
                  << sizeof(data) << ")" << std::endl;
        return;
    }
    std::memcpy(&data, payload.buffer.data(), size);
    if (data.type != CbStructType::CST_CbToHost_Io) {
        std::cout << "UNEXPECTED packet type in UDP RX of IO: " << static_cast<int>(data.type) << std::endl;
        return;
    }
    auto const entry_count = data.data.telemetry.number_of_entries;
    if (entry_count > CB_TELEMETRY_MAX_ENTRIES || size != fixed_prefix + entry_count * entry_size) {
        std::cout << "INVALID TELEMETRY in UDP RX of IO: entries=" << static_cast<int>(entry_count)
                  << " size=" << size << std::endl;
        return;
    }

    // Snapshot the decoded contents for the terminal UI (this runs on the event loop thread, the
    // same thread that reads it via get_status()). gpio_values/adc_values live in a packed struct,
    // so copy element by element (no binding to array references).
    m_io_state.gpio.clear();
    m_io_state.adc.clear();
    m_io_state.telemetry.clear();

    for (std::size_t i = 0; i < sizeof(data.data.gpio_values) / sizeof(data.data.gpio_values[0]); ++i) {
        m_io_state.gpio.push_back(data.data.gpio_values[i]);
        send_mqtt(std::to_string(i), std::to_string(data.data.gpio_values[i]));
    }
    for (std::size_t i = 0; i < sizeof(data.data.adc_values) / sizeof(data.data.adc_values[0]); ++i) {
        m_io_state.adc.push_back(data.data.adc_values[i]);
        send_adc_mqtt(std::to_string(i), std::to_string(data.data.adc_values[i]));
    }
    // Unstructured telemetry: republish each name -> value verbatim. We do not interpret the
    // names; the MCU owns their meaning.
    for (std::size_t i = 0; i < entry_count; ++i) {
        auto const& entry = data.data.telemetry.entries[i];
        std::string name(entry.name, ::strnlen(entry.name, CB_TELEMETRY_NAME_LEN));
        if (name.empty()) {
            continue;
        }
        m_io_state.telemetry.emplace_back(name, entry.value);
        send_telemetry_mqtt(name, std::to_string(entry.value));
    }
    m_have_io = true;
}

void io_bridge::handle_ready() {
    m_ready.set(m_udp_ready and m_mqtt_ready and m_cb_is_connected);
}

bool io_bridge::available() const {
    return m_ready;
}

std::optional<io_state> io_bridge::latest_io() const {
    if (not m_have_io) {
        return std::nullopt;
    }
    return m_io_state;
}

void io_bridge::set_cb_connection_status(bool connected) {
    m_cb_is_connected.set(connected);
}

} // namespace charge_bridge
