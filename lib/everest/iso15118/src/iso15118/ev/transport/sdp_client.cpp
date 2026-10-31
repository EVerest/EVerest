// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/transport/sdp_client.hpp>

#include <stdexcept>
#include <utility>

#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_payload.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/v2gtp/sdp.hpp>

namespace iso15118::ev::transport {

namespace {
constexpr auto LINK_LOCAL_ALL_NODES = "ff02::1";
} // namespace

SdpClient::SdpClient(std::string interface_name_, iso15118::v2gtp::Security security_) :
    interface_name(std::move(interface_name_)), security(security_) {
}

SdpClient::~SdpClient() {
    if (registered_handler != nullptr and client) {
        registered_handler->unregister_event_handler(client.get());
    }
}

std::vector<uint8_t> SdpClient::build_request(iso15118::v2gtp::Security security,
                                              iso15118::v2gtp::TransportProtocol transport) {
    return v2gtp::encode_sdp_request({security, transport});
}

bool SdpClient::register_events(everest::lib::io::event::fd_event_handler& handler) {
    // Guard against the double-registration footgun: fd_event_handler does not
    // dedup, so a second register would re-run register_events / restart the client.
    if (registered_handler != nullptr) {
        return true;
    }

    using everest::lib::io::udp::endpoint;
    using everest::lib::io::udp::udp_unconnected_client;

    try {
        // endpoint construction resolves the egress interface (if_nametoindex); a
        // down or unknown interface_name throws. Don't let it escape this
        // bool-returning function onto the reactor thread.
        const endpoint target{LINK_LOCAL_ALL_NODES, iso15118::v2gtp::SDP_SERVER_PORT, interface_name};
        const everest::lib::io::socket::SourcePortRange source_ports{iso15118::v2gtp::SDP_CLIENT_PORT_MIN,
                                                                     iso15118::v2gtp::SDP_CLIENT_PORT_MAX};
        client = std::make_unique<udp_unconnected_client>(target, interface_name, source_ports);
    } catch (const std::runtime_error& e) {
        logf_error("Failed to build SDP endpoint on interface '%s': %s", interface_name.c_str(), e.what());
        client.reset();
        return false;
    }

    client->set_rx_handler([this](everest::lib::io::udp::udp_payload const& payload, auto& /*device*/) {
        const auto parsed = v2gtp::decode_sdp_response(payload.buffer.data(), payload.buffer.size());
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
    registered_handler = &handler;
    return true;
}

void SdpClient::discover(std::function<void(v2gtp::SdpResponse)> on_found_) {
    on_found = std::move(on_found_);
    if (registered_handler == nullptr) {
        send_request();
        return;
    }
    // The libio udp client only reaches its connected state once the reactor has polled it, so a
    // synchronous send here would be rejected. Queue the first request as a reactor action.
    registered_handler->add_action([this]() { send_request(); });
}

void SdpClient::send_request() {
    if (not client) {
        logf_error("SdpClient::send_request called before register_events");
        return;
    }

    const auto request = build_request(security, iso15118::v2gtp::TransportProtocol::TCP);

    everest::lib::io::udp::udp_payload payload;
    payload.set_message(request.data(), request.size());
    if (not client->tx(payload)) {
        logf_error("Failed to send SDP request on interface %s", interface_name.c_str());
    }
}

} // namespace iso15118::ev::transport
