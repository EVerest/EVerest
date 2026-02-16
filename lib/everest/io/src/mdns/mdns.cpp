// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/io/mdns/mdns.hpp"
#include <iostream>
namespace everest::lib::io::mdns {

namespace {

std::string parse_name(const std::uint8_t* buffer, int size, int* offset) {
    std::string name = "";
    int curr = *offset;
    bool moved = false;
    int next_offset = -1;

    while (curr < size && buffer[curr] != 0) {
        if ((buffer[curr] & 0xC0) == 0xC0) {
            int pointer_offset = ((buffer[curr] & 0x3F) << 8) | buffer[curr + 1];
            if (!moved) {
                next_offset = curr + 2;
            }
            curr = pointer_offset;
            moved = true;
        } else {
            int len = buffer[curr];
            curr++;
            for (int i = 0; i < len && curr < size; ++i) {
                name += static_cast<char>(buffer[curr]);
                curr++;
            }
            if (curr < size && buffer[curr] != 0) {
                name += ".";
            }
        }
    }
    *offset = moved ? next_offset : curr + 1;
    return name;
}

void parse_mdns_A(const std::uint8_t* buffer, mDNS_discovery& mdns) {
    auto size_of_ip_string = 16;
    mdns.ip.resize(size_of_ip_string);
    std::snprintf(mdns.ip.data(), mdns.ip.size(), "%d.%d.%d.%d", buffer[0], buffer[1], buffer[2], buffer[3]);
}

void parse_mdns_SRV(const std::uint8_t* base, int record_data_offset, mDNS_discovery& mdns, int size) {
    mdns.port = (base[record_data_offset + 4] << 8) | base[record_data_offset + 5];
    int name_offset = record_data_offset + 6;
    mdns.hostname = parse_name(base, size, &name_offset);
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

void parse_mdns_PTR(const std::uint8_t* base, int record_data_offset, mDNS_discovery& mdns, int size) {
    std::string service_instance = parse_name(base, size, &record_data_offset);
    mdns.service_instance = service_instance;
}

} // namespace

[[maybe_unused]] std::optional<mDNS_discovery> parse_mdns_packet(std::vector<std::uint8_t> const& packet) {
    int size = packet.size();
    const auto* buf = packet.data();
    auto const mdns_packet_min_size = 12;
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

    for (size_t i = 0; i < questions && curr < size; ++i) {
        parse_name(buf, size, &curr);
        curr += 4;
    }

    auto const mdns_record_header_size = 10;
    int total_records = static_cast<int>(answers + authority + additional);
    for (int i = 0; i < total_records && curr < size; ++i) {
        std::string name = parse_name(buf, size, &curr);
        if (curr + mdns_record_header_size > size) {
            break;
        }
        std::uint16_t type = (buf[curr] << 8) | buf[curr + 1];
        std::uint16_t rdlen = (buf[curr + 8] << 8) | buf[curr + 9];
        curr += mdns_record_header_size;

        if (type == 0x01 && rdlen == 4) {
            parse_mdns_A(buf + curr, result);
        } else if (type == 0x21) {
            parse_mdns_SRV(buf, curr, result, size);
        } else if (type == 0x10) {
            parse_mdns_TXT(buf + curr, result, rdlen);
        } else if (type == 0x0C) {
            parse_mdns_PTR(buf, curr, result, size);
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
    packet.insert(packet.end(), encoded.begin(), encoded.end());
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
