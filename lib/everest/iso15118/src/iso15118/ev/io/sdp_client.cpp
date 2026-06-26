// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/io/sdp_client.hpp>

#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <arpa/inet.h>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_payload.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::ev::io {

namespace {
// link-local all-nodes multicast address; the SECC SDP server joins this group
constexpr auto LINK_LOCAL_ALL_NODES = "ff02::1";

constexpr size_t SDP_REQUEST_SIZE = 10;
constexpr size_t SDP_RESPONSE_SIZE = 28;
constexpr size_t SDP_RESPONSE_PAYLOAD_LEN = 20;

constexpr size_t RESPONSE_ADDRESS_OFFSET = 8;
constexpr size_t RESPONSE_PORT_OFFSET = 24;
constexpr size_t RESPONSE_SECURITY_OFFSET = 26;
constexpr size_t RESPONSE_TRANSPORT_OFFSET = 27;
} // namespace

SdpClient::SdpClient(std::string interface_name_, iso15118::io::v2gtp::Security security_) :
    interface_name(std::move(interface_name_)), security(security_) {
}

std::vector<uint8_t> SdpClient::build_request(iso15118::io::v2gtp::Security security,
                                              iso15118::io::v2gtp::TransportProtocol transport) {
    std::vector<uint8_t> buffer(SDP_REQUEST_SIZE, 0);

    V2GTP20_WriteHeader(buffer.data(), 2, V2GTP20_SDP_REQUEST_PAYLOAD_ID);

    buffer[8] = static_cast<std::underlying_type_t<iso15118::io::v2gtp::Security>>(security);
    buffer[9] = static_cast<std::underlying_type_t<iso15118::io::v2gtp::TransportProtocol>>(transport);

    return buffer;
}

std::optional<SdpResponse> SdpClient::parse_response(const uint8_t* buf, size_t len) {
    if (buf == nullptr or len < SDP_RESPONSE_SIZE) {
        return std::nullopt;
    }

    uint32_t payload_len = 0;
    const auto read_result = V2GTP20_ReadHeader(buf, &payload_len, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);
    if (read_result != V2GTP_ERROR__NO_ERROR or payload_len != SDP_RESPONSE_PAYLOAD_LEN) {
        return std::nullopt;
    }

    SdpResponse response{};

    // address is copied raw, in the same byte order as the wire (mirrors sdp_server send_response)
    std::memcpy(response.endpoint.address, buf + RESPONSE_ADDRESS_OFFSET, sizeof(response.endpoint.address));

    uint16_t port_net = 0;
    std::memcpy(&port_net, buf + RESPONSE_PORT_OFFSET, sizeof(port_net));
    response.endpoint.port = ntohs(port_net);

    response.security = static_cast<iso15118::io::v2gtp::Security>(buf[RESPONSE_SECURITY_OFFSET]);
    response.transport = static_cast<iso15118::io::v2gtp::TransportProtocol>(buf[RESPONSE_TRANSPORT_OFFSET]);

    return response;
}

bool SdpClient::register_events(everest::lib::io::event::fd_event_handler& handler) {
    // Guard against the double-registration footgun: fd_event_handler does not
    // dedup, so a second register would re-run register_events / restart the client.
    if (registered) {
        return true;
    }

    using everest::lib::io::udp::endpoint;
    using everest::lib::io::udp::udp_unconnected_client;

    try {
        // endpoint construction resolves the egress interface (if_nametoindex); a
        // down or unknown interface_name throws. Don't let it escape this
        // bool-returning function onto the reactor thread.
        const endpoint target{LINK_LOCAL_ALL_NODES, iso15118::io::v2gtp::SDP_SERVER_PORT, interface_name};
        client = std::make_unique<udp_unconnected_client>(target, interface_name);
    } catch (const std::runtime_error& e) {
        logf_error("Failed to build SDP endpoint on interface '%s': %s", interface_name.c_str(), e.what());
        client.reset();
        return false;
    }

    client->set_rx_handler([this](everest::lib::io::udp::udp_payload const& payload, auto& /*device*/) {
        const auto parsed = parse_response(payload.buffer.data(), payload.buffer.size());
        if (not parsed) {
            logf_warning("Received an unexpected SDP response, ignoring");
            return;
        }
        if (on_found) {
            on_found(*parsed);
        }
    });

    const auto ok = handler.register_event_handler(client.get());
    if (not ok) {
        // Keep the invariant "client non-null <=> registered": a half-registered client
        // must not survive, or a later retry would destroy it mid async-connect.
        logf_error("Failed to register SDP client with the event handler");
        client.reset();
        return false;
    }
    registered = true;
    return true;
}

void SdpClient::discover(std::function<void(SdpResponse)> on_found_) {
    on_found = std::move(on_found_);
    send_request();
}

void SdpClient::send_request() {
    if (not client) {
        logf_error("SdpClient::send_request called before register_events");
        return;
    }

    const auto request = build_request(security, iso15118::io::v2gtp::TransportProtocol::TCP);

    everest::lib::io::udp::udp_payload payload;
    payload.set_message(request.data(), request.size());
    if (not client->tx(payload)) {
        logf_error("Failed to send SDP request on interface %s", interface_name.c_str());
    }
}

} // namespace iso15118::ev::io
