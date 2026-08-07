// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::io::mdns {

struct mDNS_discovery {
    using txt_field = std::map<std::string, std::string>;

    void add_string(std::string const& key, std::string const& value) {
        txt[key] = value;
    }

    /** IPv4 address from the A record; empty if none was seen */
    std::string ip;
    /** IPv6 address from the AAAA record; empty if none was seen. The discovery
     *  layer may append a %<interface> scope for link-local addresses. When used
     *  with \ref create_mdns_response it must carry the raw address (no %scope,
     *  no brackets). */
    std::string ipv6;
    std::uint16_t port{0};
    std::string hostname;
    std::string service_instance;
    txt_field txt;

    static const std::uint16_t txt_string_limit{255};
    static const std::uint16_t txt_record_limit{1500};
};

std::optional<mDNS_discovery> parse_mdns_packet(std::vector<std::uint8_t> const& packet);
std::vector<std::uint8_t> create_mdns_query(std::string const& name);

/// Build an mDNS response packet advertising the given service.
/// Includes the DNS-SD service-type PTR plus PTR, SRV, TXT, and A records,
/// and an AAAA record when \p service.ipv6 is set (raw address, no %scope).
std::vector<std::uint8_t> create_mdns_response(mDNS_discovery const& service, std::string const& service_type);

/// Check if an mDNS packet is a query for the given service type or DNS-SD service-type enumeration.
bool is_query_for(std::vector<std::uint8_t> const& packet, std::string const& service_type);

/// Preferred connect address of a discovered service: IPv4 (ip) when present,
/// otherwise IPv6 (ipv6, possibly carrying a %scope appended by the discovery
/// layer). Empty if neither is set.
std::string select_address(mDNS_discovery const& info);

/// True if addr is an IPv6 link-local (fe80::/10) literal, with or without %scope suffix.
bool is_link_local_v6(std::string const& addr);

class mDNS_registry {
public:
    using registry = std::map<std::string, mDNS_discovery>;
    bool update(mDNS_discovery const& update);
    void remove(const std::string& instance);
    void clear();
    registry const& get();

private:
    registry data;
};

} // namespace everest::lib::io::mdns
