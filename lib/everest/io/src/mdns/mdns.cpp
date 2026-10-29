// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/io/mdns/mdns.hpp"
#include <arpa/inet.h>
#include <iostream>
#include <utility>
namespace everest::lib::io::mdns {

namespace {

constexpr int mdns_packet_min_size = 12;

std::optional<std::string> parse_name(const std::uint8_t* buffer, int size, int* offset) {
    std::string name;
    int curr = *offset;
    bool moved = false;
    int next_offset = -1;
    constexpr int max_compression_hops = 128;
    constexpr int max_name_octets = 255;
    int hops = 0;
    int name_octets = 1;

    while (true) {
        if (curr < 0 || curr >= size) {
            return std::nullopt;
        }
        if (buffer[curr] == 0) {
            *offset = moved ? next_offset : curr + 1;
            return name;
        }
        if ((buffer[curr] & 0xC0) == 0xC0) {
            if (curr + 1 >= size || ++hops > max_compression_hops) {
                return std::nullopt;
            }
            int pointer_offset = ((buffer[curr] & 0x3F) << 8) | buffer[curr + 1];
            if (pointer_offset < mdns_packet_min_size || pointer_offset >= curr) {
                return std::nullopt;
            }
            if (!moved) {
                next_offset = curr + 2;
            }
            curr = pointer_offset;
            moved = true;
        } else {
            int len = buffer[curr];
            name_octets += 1 + len;
            if ((len & 0xC0) != 0 || curr + 1 + len > size || name_octets > max_name_octets) {
                return std::nullopt;
            }
            if (!name.empty()) {
                name += ".";
            }
            name.append(reinterpret_cast<const char*>(buffer + curr + 1), len);
            curr += 1 + len;
        }
    }
}

void parse_mdns_A(const std::uint8_t* buffer, mDNS_discovery& mdns) {
    char addr_str[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, buffer, addr_str, sizeof(addr_str))) {
        mdns.ip = addr_str;
    }
}

bool parse_mdns_SRV(const std::uint8_t* base, int record_data_offset, int rdlen, mDNS_discovery& mdns, int size) {
    mdns.port = (base[record_data_offset + 4] << 8) | base[record_data_offset + 5];
    int name_offset = record_data_offset + 6;
    auto hostname = parse_name(base, size, &name_offset);
    if (!hostname || name_offset > record_data_offset + rdlen) {
        return false;
    }
    mdns.hostname = std::move(*hostname);
    return true;
}

void parse_mdns_TXT(const std::uint8_t* buffer, mDNS_discovery& mdns, int rdlen) {
    int txt_ptr = 0;
    int end_of_record = rdlen;

    while (txt_ptr < end_of_record) {
        std::uint8_t txt_len = buffer[txt_ptr];
        txt_ptr++;
        if (txt_ptr + txt_len <= end_of_record) {
            const char* entry_cstr = reinterpret_cast<const char*>(&buffer[txt_ptr]);
            std::string entry(entry_cstr, txt_len);

            std::size_t sep = entry.find('=');
            if (sep != std::string::npos) {
                std::string key = entry.substr(0, sep);
                std::string val = entry.substr(sep + 1);
                mdns.add_string(key, val);
            }

            txt_ptr += txt_len;
        } else {
            break;
        }
    }
}

bool parse_mdns_PTR(const std::uint8_t* base, int record_data_offset, int rdlen, mDNS_discovery& mdns, int size) {
    int name_offset = record_data_offset;
    auto service_instance = parse_name(base, size, &name_offset);
    if (!service_instance || name_offset > record_data_offset + rdlen) {
        return false;
    }
    mdns.service_instance = std::move(*service_instance);
    return true;
}

} // namespace

[[maybe_unused]] std::optional<mDNS_discovery> parse_mdns_packet(std::vector<std::uint8_t> const& packet) {
    int size = packet.size();
    const auto* buf = packet.data();
    if (size < mdns_packet_min_size) {
        return std::nullopt;
    }

    if ((buf[2] & 0x80) == 0) {
        return std::nullopt;
    }

    mDNS_discovery result;
    std::size_t questions = (buf[4] << 8) | buf[5];
    std::size_t answers = (buf[6] << 8) | buf[7];
    std::size_t authority = (buf[8] << 8) | buf[9];
    std::size_t additional = (buf[10] << 8) | buf[11];
    int curr = mdns_packet_min_size;

    for (size_t i = 0; i < questions; ++i) {
        if (!parse_name(buf, size, &curr) || curr + 4 > size) {
            return std::nullopt;
        }
        curr += 4;
    }

    auto const mdns_record_header_size = 10;
    int total_records = static_cast<int>(answers + authority + additional);
    for (int i = 0; i < total_records; ++i) {
        if (!parse_name(buf, size, &curr)) {
            return std::nullopt;
        }
        if (curr + mdns_record_header_size > size) {
            return std::nullopt;
        }
        std::uint16_t type = (buf[curr] << 8) | buf[curr + 1];
        std::uint16_t rdlen = (buf[curr + 8] << 8) | buf[curr + 9];
        curr += mdns_record_header_size;
        if (curr + rdlen > size) {
            return std::nullopt;
        }

        if (type == 0x01 && rdlen == 4) {
            parse_mdns_A(buf + curr, result);
        } else if (type == 0x21) {
            if (rdlen < 7 || !parse_mdns_SRV(buf, curr, rdlen, result, size)) {
                return std::nullopt;
            }
        } else if (type == 0x10) {
            parse_mdns_TXT(buf + curr, result, rdlen);
        } else if (type == 0x0C) {
            if (rdlen == 0 || !parse_mdns_PTR(buf, curr, rdlen, result, size)) {
                return std::nullopt;
            }
        }
        curr += rdlen;
    }
    return result;
}

[[maybe_unused]] std::vector<std::uint8_t> create_mdns_query(std::string const& name) {
    std::vector<uint8_t> encoded;
    std::size_t start = 0, end;
    while ((end = name.find('.', start)) != std::string::npos) {
        encoded.push_back(static_cast<uint8_t>(end - start));
        for (std::size_t i = start; i < end; ++i) {
            encoded.push_back(name[i]);
        }
        start = end + 1;
    }
    encoded.push_back(static_cast<uint8_t>(name.length() - start));
    for (std::size_t i = start; i < name.length(); ++i) {
        encoded.push_back(name[i]);
    }
    encoded.push_back(0);

    std::vector<std::uint8_t> packet = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
#if defined(__GNUC__) && (__GNUC__ < 12)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif
    packet.insert(packet.end(), encoded.begin(), encoded.end());
#if defined(__GNUC__) && (__GNUC__ < 12)
#pragma GCC diagnostic pop
#endif
    packet.push_back(0x00);
    packet.push_back(0x0c);
    packet.push_back(0x00);
    packet.push_back(0x01);

    return packet;
}

bool apply_value(std::string const& src, std::string& tar) {
    if (src == tar) {
        return false;
    }
    if (src.empty()) {
        return false;
    }
    tar = src;
    return true;
}

bool apply_value(std::uint16_t const src, std::uint16_t& tar) {
    if (src == 0 or src == tar) {
        return false;
    }
    tar = src;
    return true;
}

bool mDNS_registry::update(const mDNS_discovery& update) {
    if (update.service_instance.empty()) {
        return false;
    }

    bool something_new = data.count(update.service_instance) == 0;

    auto& item = data[update.service_instance];
    item.service_instance = update.service_instance;

    something_new = apply_value(update.port, item.port) || something_new;
    something_new = apply_value(update.hostname, item.hostname) || something_new;
    something_new = apply_value(update.ip, item.ip) || something_new;

    for (auto const& [key, val] : update.txt) {
        something_new = apply_value(val, item.txt[key]) || something_new;
    }
    return something_new;
}

void mDNS_registry::clear() {
    data.clear();
}

mDNS_registry::registry const& mDNS_registry::get() {
    return data;
}

} // namespace everest::lib::io::mdns
