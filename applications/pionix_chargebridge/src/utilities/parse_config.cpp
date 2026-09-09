// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "c4/yml/node.hpp"
#include <charge_bridge/utilities/parse_config.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <charge_bridge/utilities/type_converters.hpp>
#include <everest_api_types/evse_board_support/API.hpp>
#include <everest_api_types/evse_board_support/codec.hpp>
#include <iostream>

#include <filesystem>
// clang-format off
#include <ryml_std.hpp>
#include <ryml.hpp>
// clang-format on
using namespace everest::lib::API::V1_0::types;

namespace {
static const int g_cb_port_management = 6000;
static const int g_cb_port_evse_bsp = 6001;
static const int g_cb_port_plc = 6002;
static const int g_cb_port_can0 = 6003;
static const int g_cb_port_serial_1 = 6004;
static const int g_cb_port_serial_2 = 6005;
static const std::uint16_t default_mqtt_ping_interval_ms = 1000;

std::string print_yaml_location(ryml::Location const& loc) {
    std::stringstream error_msg;

    if (loc) {
        if (not loc.name.empty()) {
            auto tmp = std::string(loc.name.str, loc.name.len);
            if (charge_bridge::utilities::string_ends_with(tmp, ".hpp")) {
                return "";
            }
            error_msg << "\n  file ";
            error_msg << tmp;
        }
        error_msg << "\n  line " << loc.line;
        if (loc.col) {
            error_msg << " column " << loc.col;
        }
        if (loc.offset) {
            error_msg << " offset " << loc.offset << "B";
        }
        error_msg << "\n";
    }
    return error_msg.str();
}

void yaml_error_handler(const char* msg, std::size_t len, ryml::Location loc, void*) {
    std::stringstream error_msg;
    error_msg << "YAML parsing error: ";
    error_msg << print_yaml_location(loc);
    error_msg.write(msg, len);

    std::cerr << error_msg.str() << std::endl;
    throw std::runtime_error(error_msg.str());
}

void print_location(ryml::ConstNodeRef node, ryml::Parser& parser) {
    std::cerr << print_yaml_location(node.location(parser)) << std::endl;
}

void load_yaml_file(const std::string& filename, ryml::Parser* parser, ryml::Tree* t) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string file_content = buffer.str();

    parse_in_arena(parser, ryml::to_csubstr(filename), ryml::to_csubstr(file_content), t);
}

template <class T> c4::yml::ConstNodeRef decode(c4::yml::ConstNodeRef const& node, T& rhs) {
    using namespace charge_bridge::utilities;
    node >> rhs;
    return node;
}

std::pair<std::string, c4::yml::ConstNodeRef> find_node(c4::yml::NodeRef& config, std::string const& main,
                                                        std::string const& sub) {
    auto main_str = ryml::to_csubstr(main);
    auto node_str = main;
    c4::yml::ConstNodeRef node;
    if (not sub.empty()) {
        node_str = node_str + "::" + sub;
        auto sub_str = ryml::to_csubstr(sub);
        node = config.find_child(main_str);
        if (not node.invalid()) {
            node = config.find_child(main_str).find_child(sub_str);
        }
    } else {
        node = config[main_str];
    }
    return {node_str, node};
}

template <class DataT>
bool get_node_impl(c4::yml::ConstNodeRef node, ryml::Parser& parser, std::string const& node_str, DataT& data) {
    if (node.invalid()) {
        std::cerr << "Node not found: " << node_str << std::endl;
        throw std::runtime_error("");
    }
    try {
        decode(node, data);
        return true;
    } catch (std::exception const& e) {
        std::cerr << "Cannot parse config: " << node_str << std::endl;
        std::cerr << e.what() << std::endl;
    } catch (charge_bridge::utilities::yml_node_error const& e) {
        std::cerr << "Error source: \n"
                  << "  parent " << node_str << "\n"
                  << "  data " << e.m_msg << std::flush;
        print_location(e.m_node, parser);
    }
    throw std::runtime_error("");
}

struct RymlCallbackInitializer {
    RymlCallbackInitializer() {
        ryml::set_callbacks({nullptr, nullptr, nullptr, yaml_error_handler});
    }
};

} // namespace

namespace charge_bridge::utilities {

void parse_config_impl(c4::yml::NodeRef& config, charge_bridge_config& c, std::filesystem::path const& config_path,
                       ryml::Parser& parser) {
    auto get_node = [&config, &parser](auto& data, std::string const& main, std::string const& sub = "") {
        auto [node_str, node] = find_node(config, main, sub);
        get_node_impl(node, parser, node_str, data);
    };

    auto get_node_or_default = [&get_node, &config](auto& data, std::string const& main, std::string const& sub,
                                                    auto fallback) {
        auto [node_str, node] = find_node(config, main, sub);
        if (node.invalid()) {
            data = fallback;
            return;
        }
        try {
            get_node(data, main, sub);
        } catch (...) {
            data = fallback;
        }
    };

    auto get_block = [&config, &c](std::string const& block, auto& block_cfg, auto const& ftor) {
        bool enable = false;
        auto block_str = ryml::to_csubstr(block);
        if (not config.find_child(block_str).invalid()) {
            if (config[block_str].find_child("enable").invalid()) {
                enable = true;
            } else {
                decode(config[block_str]["enable"], enable);
            }
        }
        if (enable) {
            block_cfg.emplace();
            ftor(*block_cfg, block);
            block_cfg->cb = c.cb_name;
            block_cfg->item = block;
        }
    };

    // True when the key exists at all, which is what tells a configured value from an absent one:
    // get_node_or_default cannot, because it hands back the fallback either way.
    auto has_node = [&config](std::string const& main, std::string const& sub = "") {
        auto [node_str, node] = find_node(config, main, sub);
        (void)node_str;
        return not node.invalid();
    };

    // Like get_node_or_default, except that a key which IS present must decode: get_node_or_default
    // swallows a decode failure and hands back the fallback, which turns a broken value (a map or a
    // sequence where a scalar belongs) into a silent default. Present-but-undecodable is a broken
    // config, not an absent one.
    auto get_node_if_present = [&get_node, &has_node](auto& data, std::string const& main, std::string const& sub,
                                                      auto fallback) {
        if (has_node(main, sub)) {
            get_node(data, main, sub);
        } else {
            data = fallback;
        }
    };

    get_node(c.cb_name, "charge_bridge", "name");
    get_node(c.cb_remote, "charge_bridge", "ip");

    // accept the bracketed IPv6 spelling ("[fd00::1]"); sentinels (ANY_EVSE/ANY_EV)
    // and everything else pass through unchanged. Normalized here, before cb_remote
    // is copied into the per-bridge configs below.
    if (not string_starts_with(c.cb_remote, "ANY_EV")) {
        c.cb_remote = strip_brackets(c.cb_remote);
    }
    c.cb_port = g_cb_port_management;

    // Optional: the role this ChargeBridge plays. An absent key stays absent all the way to the MCU
    // rather than being turned into EVSE - a config that never mentions a role is not claiming to be
    // an EVSE, and on a strapping-coded CCS board that distinction is what keeps a correctly
    // configured EV station from raising a permanent role alarm. Read before the blocks below because
    // it derives the plc.station_id default.
    if (has_node("charge_bridge", "type")) {
        // get_node, not get_node_or_default: a key that is present but cannot be decoded (a map or a
        // sequence where a string belongs) is a broken config, not an absent one, and must not fall
        // back to a silent default.
        std::string type;
        get_node(type, "charge_bridge", "type");
        if (type == "EVSE") {
            c.type = cb_role::evse;
        } else if (type == "EV") {
            c.type = cb_role::ev;
        } else {
            std::cerr << "Configuration error: charge_bridge::type must be 'EVSE' or 'EV', got '" << type << "'"
                      << std::endl;
            throw std::runtime_error("");
        }
    } else {
        c.type = cb_role::unspecified;
    }

    get_block("telemetry", c.telemetry, [&](auto& cfg, auto const& main) {
        get_node(cfg.mqtt_remote, main, "mqtt_remote");
        get_node(cfg.mqtt_port, main, "mqtt_port");
        get_node_or_default(cfg.mqtt_bind, main, "mqtt_bind", "");
        get_node_or_default(cfg.mqtt_ping_interval_ms, main, "mqtt_ping_interval_ms", default_mqtt_ping_interval_ms);
        get_node(cfg.telemetry_topic, main, "telemetry_topic");
    });

    get_block("can_0", c.can0, [&](auto& cfg, auto const& main) {
        get_node(cfg.can_device, main, "local");
        cfg.cb_port = g_cb_port_can0;
        cfg.cb_remote = c.cb_remote;
    });

    get_block("serial_1", c.serial1, [&](auto& cfg, auto const& main) {
        get_node(cfg.serial_device, main, "local");
        cfg.cb_port = g_cb_port_serial_1;
        cfg.cb_remote = c.cb_remote;
    });

    get_block("serial_2", c.serial2, [&](auto& cfg, auto const& main) {
        get_node(cfg.serial_device, main, "local");
        cfg.cb_port = g_cb_port_serial_2;
        cfg.cb_remote = c.cb_remote;
    });

    // FIXME (JH) serial3 not availabe in first release
    // get_block("serial_3", c.serial3, [&](auto& cfg, auto const& main) {
    //     get_node(main, "local", cfg.serial_device);
    //     get_node(main, "port", cfg.cb_port);
    //     cfg.cb_remote = c.cb_remote;
    // });

    get_block("plc", c.plc, [&](auto& cfg, auto const& main) {
        get_node(cfg.plc_tap, main, "tap");
        get_node(cfg.plc_ip, main, "ip");
        get_node(cfg.plc_netmaks, main, "netmask");
        get_node(cfg.plc_mtu, main, "mtu");
        cfg.cb_port = g_cb_port_plc;
        cfg.cb_remote = c.cb_remote;

        // Optional: how the tap device's carrier is driven. "none" (the default) never issues
        // TUNSETCARRIER, which is today's behavior and the only correct setting for HomePlug - SLAC
        // MMEs must cross the tap before any link exists. "firmware" mirrors the MCU's reported SPE
        // PHY state onto the carrier (MCS) and opts the MCU into sending link-status reports.
        std::string carrier;
        get_node_if_present(carrier, main, "carrier", std::string("none"));
        if (carrier == "none") {
            cfg.carrier = carrier_mode::none;
        } else if (carrier == "firmware") {
            cfg.carrier = carrier_mode::firmware;
        } else {
            std::cerr << "Configuration error: plc::carrier must be 'none' or 'firmware', got '" << carrier << "'"
                      << std::endl;
            throw std::runtime_error("");
        }

        // Optional: what to do when the kernel has no TUNSETCARRIER (pre-5.0) in "firmware" mode.
        // "fail" (the default) refuses to start the plc bridge, "warn" keeps bridging with the
        // carrier permanently on and degraded supervision. Ignored in "none" mode.
        std::string fallback;
        get_node_if_present(fallback, main, "carrier_fallback", std::string("fail"));
        if (fallback == "fail") {
            cfg.carrier_fallback_policy = carrier_fallback::fail;
        } else if (fallback == "warn") {
            cfg.carrier_fallback_policy = carrier_fallback::warn;
        } else {
            std::cerr << "Configuration error: plc::carrier_fallback must be 'fail' or 'warn', got '" << fallback << "'"
                      << std::endl;
            throw std::runtime_error("");
        }

        // Optional: additionally gate the carrier on basic-signalling state. "none" (the default)
        // keeps carrier: firmware's PHY-only rule. "ce_mated" holds the carrier down unless the BSP
        // status reports a mated CE state, so the netdev's link exists exactly while a vehicle is
        // physically present. Requires "firmware" mode and a BSP block (validated below).
        std::string gate;
        get_node_if_present(gate, main, "carrier_gate", std::string("none"));
        if (gate == "none") {
            cfg.gate = carrier_gate::none;
        } else if (gate == "ce_mated") {
            cfg.gate = carrier_gate::ce_mated;
        } else {
            std::cerr << "Configuration error: plc::carrier_gate must be 'none' or 'ce_mated', got '" << gate << "'"
                      << std::endl;
            throw std::runtime_error("");
        }
    });

    {
        bool wants_ev = false;
        bool wants_evse = false;
        get_node_or_default(wants_ev, "ev_bsp", "enable", false);
        get_node_or_default(wants_evse, "evse_bsp", "enable", false);
        if (wants_ev && wants_evse) {
            std::cerr << "Configuration error: Cannot enable EVSE and EV BSP at the same time" << std::endl;
            throw std::exception();
        }
    }

    get_block("evse_bsp", c.bsp, [&](auto& cfg, auto const& main) {
        cfg.cb_port = g_cb_port_evse_bsp;
        cfg.api.evse.enabled = true;
        get_node(cfg.api.evse.module_id, main, "module_id");
        get_node(cfg.api.mqtt_remote, main, "mqtt_remote");
        get_node_or_default(cfg.api.mqtt_bind, main, "mqtt_bind", "");
        get_node(cfg.api.mqtt_port, main, "mqtt_port");
        get_node_or_default(cfg.api.mqtt_ping_interval_ms, main, "mqtt_ping_interval_ms",
                            default_mqtt_ping_interval_ms);
        cfg.cb_remote = c.cb_remote;
        get_node(cfg.api.evse.capabilities, main, "capabilities");
        get_node(cfg.api.ovm.enabled, main, "ovm_enabled");
        get_node(cfg.api.ovm.module_id, main, "ovm_module_id");
    });

    if (not c.bsp.has_value()) {
        get_block("ev_bsp", c.bsp, [&](auto& cfg, auto const& main) {
            cfg.cb_port = g_cb_port_evse_bsp;
            cfg.api.ev.enabled = true;
            get_node(cfg.api.ev.module_id, main, "module_id");
            get_node(cfg.api.mqtt_remote, main, "mqtt_remote");
            get_node_or_default(cfg.api.mqtt_bind, main, "mqtt_bind", "");
            get_node(cfg.api.mqtt_port, main, "mqtt_port");
            get_node_or_default(cfg.api.mqtt_ping_interval_ms, main, "mqtt_ping_interval_ms",
                                default_mqtt_ping_interval_ms);
            cfg.cb_remote = c.cb_remote;
            get_node(cfg.api.ovm.enabled, main, "ovm_enabled");
            get_node(cfg.api.ovm.module_id, main, "ovm_module_id");
        });
    }

    // The section was renamed "gpio" -> "io". Reject the old name explicitly: silently ignoring it
    // would leave c.io unset and send a zeroed GPIO config to the MCU (all pins disabled, IO MQTT
    // topics dead) with no warning.
    if (not config.find_child(ryml::to_csubstr("gpio")).invalid()) {
        std::cerr << "Config error: the 'gpio' section was renamed to 'io'; please update the config" << std::endl;
        throw std::runtime_error("");
    }

    // Combined GPIO + ADC bridge: a single "io" config section drives one bridge that both
    // writes GPIO outputs and republishes the GPIO inputs + ADC values from the combined packet.
    get_block("io", c.io, [&](auto& cfg, auto const& main) {
        get_node(cfg.interval_s, main, "interval_s");
        get_node(cfg.mqtt_remote, main, "mqtt_remote");
        get_node_or_default(cfg.mqtt_bind, main, "mqtt_bind", "");
        get_node(cfg.mqtt_port, main, "mqtt_port");
        get_node_or_default(cfg.mqtt_ping_interval_ms, main, "mqtt_ping_interval_ms", default_mqtt_ping_interval_ms);
        cfg.cb_remote = c.cb_remote;
        cfg.cb_port = c.cb_port;
    });

    get_block("heartbeat", c.heartbeat, [&](auto& cfg, auto const& main) {
        get_node_or_default(cfg.interval_s, main, "interval_s", 1);
        get_node_or_default(cfg.connection_to_s, main, "connection_to_s", 3 * cfg.interval_s);
        // cb-session-v1: steal the MCU even if a healthy session on another host owns it
        get_node_or_default(cfg.force_takeover, main, "force_takeover", false);
        cfg.cb_remote = c.cb_remote;
        cfg.cb_port = c.cb_port;
        get_node(cfg.cb_config.safety, "safety");

        std::memset(cfg.cb_config.gpios, 0, CB_NUMBER_OF_GPIOS * sizeof(CbGpioConfig));
        std::memset(cfg.cb_config.uarts, 0, CB_NUMBER_OF_UARTS * sizeof(CbUartConfig));
        std::memset(cfg.cb_config.adcs, 0, CB_NUMBER_OF_ADCS * sizeof(CbAdcConfig));
        if (c.serial1) {
            get_node(cfg.cb_config.uarts[0], "serial_1");
        }
        if (c.serial2) {
            get_node(cfg.cb_config.uarts[1], "serial_2");
        }
        // FIXME (JH) serial 3 not available in first release
        // if (c.serial3) {
        //     get_main_node("serial_3", cfg.cb_config.uarts[2]);
        // }
        if (c.io) {
            for (auto i = 0; i < CB_NUMBER_OF_GPIOS; ++i) {
                get_node(cfg.cb_config.gpios[i], "io", "gpio_" + std::to_string(i));
            }
            for (auto i = 0; i < CB_NUMBER_OF_ADCS; ++i) {
                get_node(cfg.cb_config.adcs[i], "io", "adc_" + std::to_string(i));
            }
        }

        if (c.can0) {
            get_node(cfg.cb_config.can, "can_0");
        }
        get_node(cfg.cb_config.plc_powersaving_mode, "plc", "powersaving_mode");

        // The role the MCU latches from the first config heartbeat after it boots.
        cfg.cb_config.cb_type = to_wire(c.type);

        // charge_bridge.type derives the station_id default (EVSE is the PLCA coordinator, an EV the
        // first follower). Read into an int rather than the int8_t on the wire so a value that is no
        // node id at all can be recognised instead of silently wrapping.
        std::optional<int> configured_station_id;
        if (has_node("plc", "station_id")) {
            int value = 0;
            get_node(value, "plc", "station_id");
            configured_station_id = value;
        }
        auto const station = decide_station_id(c.type, configured_station_id);
        switch (station.issue) {
        case station_id_issue::evse_not_coordinator:
            // Fires for an explicit EVSE and for an absent type, which derives like one. Naming EVSE
            // in both cases would repeat the absent-is-EVSE conflation this key exists to avoid, so
            // the message says which of the two it actually is.
            std::cerr << "Configuration warning: plc::station_id is " << station.station_id << " but charge_bridge::"
                      << (c.type == cb_role::unspecified ? "type is not configured (defaults to EVSE)" : "type is EVSE")
                      << ", which is the PLCA coordinator (station 0)" << std::endl;
            break;
        case station_id_issue::ev_is_coordinator:
            std::cerr << "Configuration warning: plc::station_id 0 is the PLCA coordinator (the EVSE), "
                         "but charge_bridge::type is EV"
                      << std::endl;
            break;
        case station_id_issue::out_of_range:
            // Refused rather than corrected: every fallback has to guess, and the guess for an EVSE
            // would be station 0 - the coordinator seat, which is exactly what a typo must not be able
            // to hand out silently.
            std::cerr << "Configuration error: plc::station_id " << station.station_id
                      << " is not a PLCA node id (0..7, or -1 for collision detection)" << std::endl;
            break;
        case station_id_issue::none:
            break;
        }
        if (is_fatal(station.issue)) {
            throw std::runtime_error("");
        }
        cfg.cb_config.station_id = static_cast<std::int8_t>(station.station_id);

        // Optional: forward the MCU's debug-UART (printf) output to this host over UDP. Off by
        // default; the bridge logs each received line to the console prefixed with "[MCU]".
        bool enable_debug_uart_udp = false;
        get_node_or_default(enable_debug_uart_udp, main, "enable_debug_uart_udp", false);
        cfg.cb_config.debug_uart_udp_enabled = enable_debug_uart_udp ? 1 : 0;

        cfg.cb_config.config_version = CB_CONFIG_VERSION;
    });

    // The link status rides inside the heartbeat reply, and the carrier is gated on the
    // heartbeat-verified connection state: without a heartbeat block there is no transport for the
    // status and the carrier could never be raised at all. Reject the configuration instead of
    // silently keeping the link down.
    if (c.plc.has_value() and c.plc->carrier == carrier_mode::firmware and not c.heartbeat.has_value()) {
        std::cerr << "Configuration error: plc::carrier: firmware requires an enabled 'heartbeat' block" << std::endl;
        throw std::runtime_error("");
    }

    // The carrier gate's CE state rides in the BSP status packet the same way: without a BSP block
    // there is no transport for it and the gate would hold the carrier down forever. And in carrier
    // mode none there is no carrier being driven for the gate to act on - a configured gate that
    // silently does nothing is a misconfiguration, not a default.
    if (c.plc.has_value() and c.plc->gate not_eq carrier_gate::none) {
        if (c.plc->carrier not_eq carrier_mode::firmware) {
            std::cerr << "Configuration error: plc::carrier_gate requires plc::carrier: firmware" << std::endl;
            throw std::runtime_error("");
        }
        if (not c.bsp.has_value()) {
            std::cerr << "Configuration error: plc::carrier_gate: ce_mated requires an enabled 'evse_bsp' or "
                         "'ev_bsp' block (the CE state rides in the BSP status packet)"
                      << std::endl;
            throw std::runtime_error("");
        }
    }

    get_node(c.firmware.fw_path, "charge_bridge", "fw_file");
    get_node(c.firmware.fw_update_on_start, "charge_bridge", "fw_update_on_start");

    // If the path to the firmware file is relative, make it relative to the config file
    std::filesystem::path fw_path = c.firmware.fw_path;
    if (fw_path.is_relative()) {
        c.firmware.fw_path = config_path.parent_path().append(c.firmware.fw_path);
    }

    c.firmware.cb_remote = c.cb_remote;
    c.firmware.cb_port = c.cb_port;
    c.firmware.cb = c.cb_name;
}

charge_bridge_config set_config_placeholders(charge_bridge_config const& src, charge_bridge_config& result,
                                             std::string const& ip, std::size_t index) {
    auto index_str = std::to_string(index);
    result = src;
    auto replace = [index_str](std::string& src) { replace_all_in_place(src, "##", index_str); };

    result.cb_remote = ip;
    result.firmware.cb_remote = ip;
    replace(result.cb_name);
    result.firmware.cb = result.cb_name;
    if (result.can0.has_value()) {
        result.can0->cb_remote = ip;
        result.can0->cb = result.cb_name;
        replace(result.can0->can_device);
    }
    if (result.serial1.has_value()) {
        result.serial1->cb_remote = ip;
        result.serial1->cb = result.cb_name;
        replace(result.serial1->serial_device);
    }
    if (result.serial2.has_value()) {
        result.serial2->cb_remote = ip;
        result.serial2->cb = result.cb_name;
        replace(result.serial2->serial_device);
    }
    if (result.serial3.has_value()) {
        result.serial3->cb_remote = ip;
        result.serial3->cb = result.cb_name;
        replace(result.serial3->serial_device);
    }
    if (result.plc.has_value()) {
        result.plc->cb_remote = ip;
        result.plc->cb = result.cb_name;
        replace(result.plc->plc_tap);
    }
    if (result.bsp.has_value()) {
        result.bsp->cb_remote = ip;
        result.bsp->cb = result.cb_name;
        replace(result.bsp->api.evse.module_id);
        replace(result.bsp->api.ev.module_id);
        replace(result.bsp->api.ovm.module_id);
    }
    if (result.heartbeat.has_value()) {
        result.heartbeat->cb = result.cb_name;
        result.heartbeat->cb_remote = ip;
    }
    if (result.io.has_value()) {
        result.io->cb = result.cb_name;
        result.io->cb_remote = ip;
    }

    return result;
}

std::vector<charge_bridge_config> parse_config_multi(std::string const& config_file) {
    const static RymlCallbackInitializer ryml_callback_initializer;

    try {
        ryml::EventHandlerTree evt_handler = {};
        ryml::Parser parser(&evt_handler, ryml::ParserOptions().locations(true));
        ryml::Tree config_tree;
        load_yaml_file(config_file, &parser, &config_tree);
        c4::yml::NodeRef config = config_tree.rootref();
        if (config.invalid()) {
            std::cerr << "Config file not found: " << config_file << std::endl;
            return {};
        }
        charge_bridge_config base_config;
        parse_config_impl(config, base_config, config_file, parser);

        auto ip_list_node = config.find_child("charge_bridge_ip_list");
        if (ip_list_node.invalid()) {
            return {base_config};
        }
        std::vector<std::string> ip_list;
        ip_list_node >> ip_list;
        std::vector<charge_bridge_config> cb_config_list(ip_list.size());

        for (std::size_t i = 0; i < ip_list.size(); ++i) {
            set_config_placeholders(base_config, cb_config_list[i], strip_brackets(ip_list[i]), i);
        }

        return cb_config_list;
    } catch (...) {
        std::cerr << "FAILED to parse configuration!" << std::endl;
    }
    return {};
}

} // namespace charge_bridge::utilities
