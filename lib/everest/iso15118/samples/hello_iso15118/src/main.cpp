// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <iostream>
#include <memory>
#include <string>

#include <iso15118/io/logging.hpp>
#include <iso15118/tbd_controller.hpp>

#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

namespace {

K_SEM_DEFINE(wifi_associated, 0, 1);

net_mgmt_event_callback wifi_cb;

void wifi_event_handler(net_mgmt_event_callback* cb, uint64_t event, net_if* /*iface*/) {
    if (event != NET_EVENT_WIFI_CONNECT_RESULT) {
        return;
    }

    const auto* status = static_cast<const wifi_status*>(cb->info);
    if (status != nullptr && status->status != 0) {
        std::cout << "wifi: association failed (status " << status->status << ")\n";
        return;
    }

    k_sem_give(&wifi_associated);
}

// Associate with CONFIG_APP_WIFI_SSID in station mode and wait for a usable IPv6
// link-local address.
// Returns the interface name libiso15118 should bind to, or "" on failure.
std::string setup_network() {
    net_if* iface = net_if_get_first_wifi();
    if (iface == nullptr) {
        std::cout << "wifi: no WiFi interface present\n";
        return {};
    }

    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, NET_EVENT_WIFI_CONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    const char* const ssid = CONFIG_APP_WIFI_SSID;
    const char* const psk = CONFIG_APP_WIFI_PSK;
    const bool open_network = std::strlen(psk) == 0;

    wifi_connect_req_params params{};
    params.ssid = reinterpret_cast<const uint8_t*>(ssid);
    params.ssid_length = std::strlen(ssid);
    params.security = open_network ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
    if (!open_network) {
        params.psk = reinterpret_cast<const uint8_t*>(psk);
        params.psk_length = std::strlen(psk);
    }
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ; // ESP32-C6 is 2.4 GHz only
    params.mfp = WIFI_MFP_OPTIONAL;

    std::cout << "wifi: connecting to \"" << ssid << "\"...\n";
    if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params)) != 0) {
        std::cout << "wifi: connect request rejected\n";
        return {};
    }

    if (k_sem_take(&wifi_associated, K_SECONDS(CONFIG_APP_WIFI_CONNECT_TIMEOUT_S)) != 0) {
        std::cout << "wifi: association timed out\n";
        return {};
    }
    std::cout << "wifi: associated\n";

    // Wait for the link-local address to pass duplicate address detection.
    const int64_t deadline = k_uptime_get() + (CONFIG_APP_WIFI_CONNECT_TIMEOUT_S * 1000);
    const net_in6_addr* ll = nullptr;
    while ((ll = net_if_ipv6_get_ll(iface, NET_ADDR_PREFERRED)) == nullptr) {
        if (k_uptime_get() > deadline) {
            std::cout << "wifi: no link-local address after DAD\n";
            return {};
        }
        k_msleep(100);
    }

    char if_name[CONFIG_NET_INTERFACE_NAME_LEN + 1] = {};
    if (net_if_get_name(iface, if_name, sizeof(if_name)) < 0) {
        std::cout << "wifi: could not read interface name\n";
        return {};
    }

    char addr_str[NET_IPV6_ADDR_LEN] = {};
    net_addr_ntop(AF_INET6, ll, addr_str, sizeof(addr_str));
    std::cout << "wifi: " << if_name << " ready, link-local " << addr_str << "\n";

    return if_name;
}

const char* signal_name(iso15118::session::feedback::Signal signal) {
    using S = iso15118::session::feedback::Signal;
    switch (signal) {
    case S::REQUIRE_AUTH_EIM:
        return "REQUIRE_AUTH_EIM";
    case S::START_CABLE_CHECK:
        return "START_CABLE_CHECK";
    case S::SETUP_FINISHED:
        return "SETUP_FINISHED";
    case S::PRE_CHARGE_STARTED:
        return "PRE_CHARGE_STARTED";
    case S::CHARGE_LOOP_STARTED:
        return "CHARGE_LOOP_STARTED";
    case S::CHARGE_LOOP_FINISHED:
        return "CHARGE_LOOP_FINISHED";
    case S::DC_OPEN_CONTACTOR:
        return "DC_OPEN_CONTACTOR";
    case S::AC_CLOSE_CONTACTOR:
        return "AC_CLOSE_CONTACTOR";
    case S::AC_OPEN_CONTACTOR:
        return "AC_OPEN_CONTACTOR";
    case S::DLINK_TERMINATE:
        return "DLINK_TERMINATE";
    case S::DLINK_ERROR:
        return "DLINK_ERROR";
    case S::DLINK_PAUSE:
        return "DLINK_PAUSE";
    }
    return "unknown";
}

} // namespace

int main() {

    using namespace iso15118;

    std::unique_ptr<TbdController> controller;

    io::set_logging_callback([](LogLevel level, const std::string& message) {
        std::cout << "log(" << static_cast<int>(level) << "): " << message << "\n";
    });

    TbdConfig config{};
    config.enable_sdp_server = true;

    config.interface_name = setup_network();
    if (config.interface_name.empty()) {
        std::cout << "network setup failed, aborting\n";
        return 1;
    }

    session::feedback::Callbacks callbacks{};
    callbacks.signal = [](session::feedback::Signal signal) {
        std::cout << "log: signal received: " << signal_name(signal) << "\n";
    };

    d20::EvseSetupConfig evse_config{};

    controller = std::make_unique<TbdController>(std::move(config), std::move(callbacks), std::move(evse_config));

    controller->set_dlink_ready(true);

    controller->loop();
    return 0;
}
