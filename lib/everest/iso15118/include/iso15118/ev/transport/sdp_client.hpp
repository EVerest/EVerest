// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/udp/udp_unconnected_client.hpp>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/sdp.hpp>

namespace iso15118::ev::transport {

/**
 * Parsed SDP response: the SECC's TCP/TLS endpoint plus the transport security
 * and protocol it advertises. The security drives which transport client the
 * controller creates (plain TCP vs. TLS).
 */
struct SdpResponse {
    iso15118::io::Ipv6EndPoint endpoint;
    iso15118::io::v2gtp::Security security;
    iso15118::io::v2gtp::TransportProtocol transport;
};

/**
 * EV-side SECC Discovery Protocol client.
 *
 * Sends an SDP request to the link-local all-nodes multicast address on the
 * egress interface and parses the unicast reply to learn the SECC TCP/TLS
 * endpoint. The pure wire codecs (\ref build_request / \ref parse_response)
 * are socket-free and independently testable.
 */
class SdpClient {
public:
    /**
     * @brief Construct a client bound to an egress interface.
     * @param[in] interface_name Name of the interface used to send the
     * multicast request (sets IPV6_MULTICAST_IF / scope id).
     * @param[in] security Transport security advertised in the SDP request.
     */
    explicit SdpClient(std::string interface_name,
                       iso15118::io::v2gtp::Security security = iso15118::io::v2gtp::Security::NO_TRANSPORT_SECURITY);

    /**
     * @brief Register the internal UDP client with an event handler.
     * @details Idempotent: a second call is a no-op (guarded against the
     * double-registration footgun in fd_event_handler).
     * @param[in] handler The reactor to register with.
     * @return True on success, false otherwise.
     */
    bool register_events(everest::lib::io::event::fd_event_handler& handler);

    /**
     * @brief Build a 10-byte SDP request buffer.
     * @details Bytes 0-7 are the V2GTP header (payload id 0x9000, length 2),
     * byte 8 is the security value, byte 9 is the transport value.
     * @param[in] security Requested transport security.
     * @param[in] transport Requested transport protocol.
     * @return The serialized request.
     */
    static std::vector<uint8_t> build_request(iso15118::io::v2gtp::Security security,
                                              iso15118::io::v2gtp::TransportProtocol transport);

    /**
     * @brief Parse a 28-byte SDP response buffer.
     * @details Returns std::nullopt for a short buffer or a header that does
     * not carry the SDP response payload id (0x9001).
     * @param[in] buf Pointer to the response bytes.
     * @param[in] len Number of bytes available at @p buf.
     * @return The advertised endpoint, security and transport, or std::nullopt
     * on a malformed response.
     */
    static std::optional<SdpResponse> parse_response(const uint8_t* buf, size_t len);

    /**
     * @brief Send a discovery request and report the learned endpoint.
     * @details Transmits the request to ff02::1 port SDP_SERVER_PORT via the
     * internal libio UDP client and invokes @p on_found with the parsed reply.
     * @param[in] on_found Callback invoked with the parsed SDP response.
     */
    void discover(std::function<void(SdpResponse)> on_found);

    /**
     * @brief Re-transmit the SDP request to the same multicast group.
     * @details Idempotent re-send used by the controller's SDP retry timer: the
     * request is sent on the standard SDP retransmit interval until a response
     * arrives or the communication setup timeout elapses. Reuses the on_found
     * callback registered by \ref discover; a no-op (with a log) if called before
     * \ref register_events.
     */
    void send_request();

private:
    std::string interface_name;
    iso15118::io::v2gtp::Security security;
    bool registered{false};
    std::function<void(SdpResponse)> on_found;
    std::unique_ptr<everest::lib::io::udp::udp_unconnected_client> client;
};

} // namespace iso15118::ev::transport
