// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Controller-level transport integration: a real ev::Controller is driven over a
// loopback TCP link with the SECC side played by this test.
//
// Reaching Controller::establish_data_path takes a genuine SDP response, and the
// only interface a test may use is `lo`, which carries no IPv6 link-local
// multicast: sending to ff02::1 fails with ENETUNREACH. The EV's discovery request
// therefore never leaves the host and no responder can learn a sender address to
// answer. The EV's SDP socket is bound to [::]:ephemeral all the same, so these
// tests locate that port among the process's own descriptors and deliver the
// unicast response the SECC would have sent.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cbv2g/exi_v2gtp.h>

#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/controller.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

using PT = io::v2gtp::PayloadType;
using message_20::datatypes::ControlMode;
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;

constexpr message_20::datatypes::SessionId LINK_SESSION_ID{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

// Matches the precharge target seeded into the Controller, so DC_PreCharge completes.
constexpr float PRECHARGE_VOLTAGE = 400.0f;

constexpr size_t SDP_RESPONSE_SIZE = 28;
constexpr size_t SDP_RESPONSE_PAYLOAD_LEN = 20;

// Every bound UDP/IPv6 socket this process holds, as (descriptor, local port).
std::vector<std::pair<int, uint16_t>> bound_udp6_sockets() {
    std::vector<std::pair<int, uint16_t>> found;

    auto* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) {
        return found;
    }

    while (const auto* entry = ::readdir(dir)) {
        char* end = nullptr;
        const auto fd = std::strtol(entry->d_name, &end, 10);
        if (end == entry->d_name or *end != '\0') {
            continue;
        }

        int type = 0;
        socklen_t len = sizeof(type);
        if (::getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_TYPE, &type, &len) != 0 or type != SOCK_DGRAM) {
            continue;
        }

        int domain = 0;
        len = sizeof(domain);
        if (::getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_DOMAIN, &domain, &len) != 0 or domain != AF_INET6) {
            continue;
        }

        sockaddr_in6 local{};
        socklen_t local_len = sizeof(local);
        if (::getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&local), &local_len) != 0) {
            continue;
        }

        const auto port = ntohs(local.sin6_port);
        if (port == 0) {
            continue;
        }
        found.emplace_back(static_cast<int>(fd), port);
    }

    ::closedir(dir);
    return found;
}

// The EV SDP client's rx port: the single bound UDP/IPv6 socket the test does not
// own. Empty while the Controller has not created it yet, and empty (rather than
// wrong) should the process ever hold more than one candidate.
std::optional<uint16_t> sdp_rx_port(const std::set<int>& own) {
    std::optional<uint16_t> port;
    for (const auto& [fd, candidate] : bound_udp6_sockets()) {
        if (own.count(fd) != 0) {
            continue;
        }
        if (port.has_value()) {
            return std::nullopt;
        }
        port = candidate;
    }
    return port;
}

// Sends the unicast SDP response the SECC would have replied with.
class SdpResponder {
public:
    SdpResponder() {
        fd = ::socket(AF_INET6, SOCK_DGRAM, 0);
        REQUIRE(fd >= 0);
    }

    ~SdpResponder() {
        if (fd >= 0) {
            ::close(fd);
        }
    }

    SdpResponder(const SdpResponder&) = delete;
    SdpResponder& operator=(const SdpResponder&) = delete;

    int descriptor() const {
        return fd;
    }

    // Advertise [::1]:tcp_port over plain TCP to the EV's SDP rx port.
    bool respond(uint16_t ev_port, uint16_t tcp_port) const {
        std::vector<uint8_t> buffer(SDP_RESPONSE_SIZE, 0);
        V2GTP20_WriteHeader(buffer.data(), SDP_RESPONSE_PAYLOAD_LEN, V2GTP20_SDP_RESPONSE_PAYLOAD_ID);
        std::memcpy(buffer.data() + 8, &in6addr_loopback, sizeof(in6addr_loopback));
        const auto port_net = htons(tcp_port);
        std::memcpy(buffer.data() + 24, &port_net, sizeof(port_net));
        buffer[26] = static_cast<uint8_t>(io::v2gtp::Security::NO_TRANSPORT_SECURITY);
        buffer[27] = static_cast<uint8_t>(io::v2gtp::TransportProtocol::TCP);

        sockaddr_in6 to{};
        to.sin6_family = AF_INET6;
        to.sin6_addr = in6addr_loopback;
        to.sin6_port = htons(ev_port);

        const auto sent = ::sendto(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        return sent == static_cast<ssize_t>(buffer.size());
    }

private:
    int fd{-1};
};

// A ServiceDetail parameter set carrying a named ControlMode; the ServiceDetail
// state needs a Dynamic set to advance.
message_20::datatypes::ParameterSet make_param_set(uint16_t id, ControlMode control_mode) {
    message_20::datatypes::ParameterSet set{};
    set.id = id;
    set.parameter.push_back({"Connector", static_cast<int32_t>(1)});
    set.parameter.push_back({"ControlMode", static_cast<int32_t>(control_mode)});
    set.parameter.push_back({"EVSENominalVoltage", static_cast<int32_t>(230)});
    return set;
}

// The SECC end of the data path: a loopback listener that answers each request
// frame the EV puts on the wire with the canned response for it, walking the DC
// entry sequence through to a running charge loop.
class SeccLink {
public:
    SeccLink() {
        // Non-blocking: service() polls for the EV's connect and must not park in
        // accept when the EV never gets that far.
        listen_fd = ::socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
        REQUIRE(listen_fd >= 0);

        int reuse = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        addr.sin6_port = 0; // let the OS choose

        REQUIRE(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd, 1) == 0);

        sockaddr_in6 bound{};
        socklen_t bound_len = sizeof(bound);
        REQUIRE(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0);
        bound_port = ntohs(bound.sin6_port);
    }

    ~SeccLink() {
        drop();
    }

    SeccLink(const SeccLink&) = delete;
    SeccLink& operator=(const SeccLink&) = delete;

    uint16_t port() const {
        return bound_port;
    }

    bool accepted() const {
        return peer_fd >= 0;
    }

    // Requests the EV put on the wire that this SECC has no canned answer for, i.e.
    // the walk left the expected entry sequence.
    int unanswered() const {
        return unanswered_count;
    }

    int charge_loop_requests() const {
        return loop_request_count;
    }

    // One SECC pass: accept if the EV has connected, then answer every complete
    // request frame that has arrived. Non-blocking throughout.
    void service() {
        if (dropped) {
            return;
        }
        if (peer_fd < 0) {
            try_accept();
            return;
        }
        read_available();
        while (auto frame = take_frame()) {
            auto variant = decode_frame(*frame);
            if (variant.get_if<message_20::DC_ChargeLoopRequest>() != nullptr) {
                ++loop_request_count;
            }
            const auto response = canned_response(variant);
            if (not response.has_value()) {
                ++unanswered_count;
                continue;
            }
            const auto bytes = frame_payload(response->first, response->second);
            REQUIRE(::send(peer_fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size()));
        }
    }

    // Vanish: close the accepted socket AND stop listening, so libio's reconnect
    // attempt is refused rather than silently completed by the kernel backlog.
    void drop() {
        dropped = true;
        if (peer_fd >= 0) {
            ::close(peer_fd);
            peer_fd = -1;
        }
        if (listen_fd >= 0) {
            ::close(listen_fd);
            listen_fd = -1;
        }
    }

private:
    void try_accept() {
        const int fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK);
        if (fd >= 0) {
            peer_fd = fd;
        }
    }

    void read_available() {
        uint8_t buffer[2048];
        for (;;) {
            const auto n = ::recv(peer_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (n <= 0) {
                return;
            }
            inbound.insert(inbound.end(), buffer, buffer + n);
        }
    }

    std::optional<std::vector<uint8_t>> take_frame() {
        constexpr auto header = io::SdpPacket::V2GTP_HEADER_SIZE;
        if (inbound.size() < header) {
            return std::nullopt;
        }
        uint32_t len_be = 0;
        std::memcpy(&len_be, inbound.data() + 4, sizeof(len_be));
        const auto total = header + ntohl(len_be);
        if (inbound.size() < total) {
            return std::nullopt;
        }
        std::vector<uint8_t> frame(inbound.begin(), inbound.begin() + total);
        inbound.erase(inbound.begin(), inbound.begin() + total);
        return frame;
    }

    using Response = std::pair<PT, std::vector<uint8_t>>;

    // The SECC's answer for one request, mirroring the canned DC entry sequence the
    // session-level walk injects: SAP -> SessionSetup -> AuthorizationSetup ->
    // Authorization -> ServiceDiscovery -> ServiceDetail -> ServiceSelection ->
    // DC_ChargeParameterDiscovery -> ScheduleExchange -> DC_CableCheck ->
    // DC_PreCharge -> PowerDelivery(Start) -> DC_ChargeLoop.
    static std::optional<Response> canned_response(const message_20::Variant& request) {
        const auto sid = LINK_SESSION_ID;

        if (request.get_if<message_20::SupportedAppProtocolRequest>() != nullptr) {
            return Response{PT::SAP,
                            serialize_msg(message_20::SupportedAppProtocolResponse{
                                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1})};
        }

        if (request.get_if<message_20::SessionSetupRequest>() != nullptr) {
            // A valid establishment carries both a non-zero session id and an evseid.
            message_20::SessionSetupResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK_NewSessionEstablished;
            res.evseid = "DE*PNX*E12345";
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::AuthorizationSetupRequest>() != nullptr) {
            message_20::AuthorizationSetupResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.authorization_services = {message_20::datatypes::Authorization::EIM};
            res.certificate_installation_service = false;
            res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::AuthorizationRequest>() != nullptr) {
            message_20::AuthorizationResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.evse_processing = Processing::Finished;
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::ServiceDiscoveryRequest>() != nullptr) {
            message_20::ServiceDiscoveryResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.energy_transfer_service_list = {{message_20::datatypes::ServiceCategory::DC, false}};
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::ServiceDetailRequest>() != nullptr) {
            message_20::ServiceDetailResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC);
            res.service_parameter_list = {make_param_set(1, ControlMode::Dynamic)};
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::ServiceSelectionRequest>() != nullptr) {
            message_20::ServiceSelectionResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::DC_ChargeParameterDiscoveryRequest>() != nullptr) {
            message_20::DC_ChargeParameterDiscoveryResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
            return Response{PT::Part20DC, serialize_msg(res)};
        }

        if (request.get_if<message_20::ScheduleExchangeRequest>() != nullptr) {
            message_20::ScheduleExchangeResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.processing = Processing::Finished;
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::DC_CableCheckRequest>() != nullptr) {
            message_20::DC_CableCheckResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.processing = Processing::Finished;
            return Response{PT::Part20DC, serialize_msg(res)};
        }

        if (request.get_if<message_20::DC_PreChargeRequest>() != nullptr) {
            message_20::DC_PreChargeResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.present_voltage = message_20::datatypes::from_float(PRECHARGE_VOLTAGE);
            return Response{PT::Part20DC, serialize_msg(res)};
        }

        if (request.get_if<message_20::PowerDeliveryRequest>() != nullptr) {
            message_20::PowerDeliveryResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            return Response{PT::Part20Main, serialize_msg(res)};
        }

        if (request.get_if<message_20::DC_ChargeLoopRequest>() != nullptr) {
            // Plain OK, no Terminate notification: the loop keeps running.
            message_20::DC_ChargeLoopResponse res{};
            res.header.session_id = sid;
            res.response_code = ResponseCode::OK;
            res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
            return Response{PT::Part20DC, serialize_msg(res)};
        }

        return std::nullopt;
    }

    int listen_fd{-1};
    int peer_fd{-1};
    uint16_t bound_port{0};
    bool dropped{false};
    int unanswered_count{0};
    int loop_request_count{0};
    std::vector<uint8_t> inbound;
};

ev::EvConfig link_config() {
    ev::EvConfig config{};
    config.interface_name = "lo";
    config.evcc_id = "EVTESTID01";
    config.send_delay = 5ms;
    // Far enough out that the response watchdog cannot be what ends any run below.
    config.response_timeout = 5s;
    return config;
}

ev::DcChargeParams link_dc_params() {
    ev::DcChargeParams params{};
    params.target_voltage = PRECHARGE_VOLTAGE;
    return params;
}

} // namespace

SCENARIO("ISO15118-20 EV Controller ends the run when the SECC drops the data path") {
    // A SECC that vanishes mid-session must end the run through the transport, not
    // by leaving the EV parked on a dead socket until the per-request response
    // watchdog expires. The watchdog is configured five seconds out and the
    // assertion budget below is one second, so a run that ends in time can only
    // have ended through the transport.
    GIVEN("a Controller walked over a loopback link to a running DC charge loop") {
        SdpResponder responder;
        SeccLink secc;
        const std::set<int> own_sockets{responder.descriptor()};

        ev::feedback::Callbacks callbacks{};
        std::atomic_int connected_count{0};
        std::atomic_int stopped_count{0};
        callbacks.connected = [&connected_count](const io::Ipv6EndPoint&) { ++connected_count; };
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{link_config(), callbacks, link_dc_params()};
        ControllerRun run{controller};

        // The SDP socket exists only once loop() has registered the SDP client.
        std::optional<uint16_t> ev_port;
        REQUIRE(poll_until(
            [&]() {
                ev_port = sdp_rx_port(own_sockets);
                return ev_port.has_value();
            },
            5s));
        REQUIRE(responder.respond(*ev_port, secc.port()));

        // Serve the SECC side until the charge loop has turned over twice, proving the
        // link carried a full entry sequence and is live in both directions.
        const auto walked = poll_until(
            [&]() {
                secc.service();
                return secc.charge_loop_requests() >= 2;
            },
            10s);

        REQUIRE(walked);
        REQUIRE(connected_count == 1);
        REQUIRE(secc.accepted());
        // Every request came from the expected entry sequence.
        REQUIRE(secc.unanswered() == 0);
        REQUIRE(stopped_count == 0);

        WHEN("the SECC drops the connection") {
            secc.drop();

            THEN("the run ends promptly and fires stopped exactly once") {
                REQUIRE(poll_until([&]() { return stopped_count > 0; }, 1s));
                REQUIRE(stopped_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Controller fires stopped once when the connected callback throws") {
    // establish_data_path runs from the SDP rx callback, inside a reactor poll that
    // has no try/catch of its own. A throwing consumer connected callback must be
    // caught there and end the run, and the catch must NOT fire stopped itself:
    // loop() fires it once on the way out, so a second fire in the catch would
    // deliver two session ends for one session.
    GIVEN("a Controller whose connected callback throws") {
        SdpResponder responder;
        SeccLink secc;
        const std::set<int> own_sockets{responder.descriptor()};

        ev::feedback::Callbacks callbacks{};
        std::atomic_int connected_count{0};
        std::atomic_int stopped_count{0};
        callbacks.connected = [&connected_count](const io::Ipv6EndPoint&) {
            ++connected_count;
            throw std::runtime_error("consumer connected callback failure");
        };
        callbacks.stopped = [&stopped_count]() { ++stopped_count; };

        ev::Controller controller{link_config(), callbacks, link_dc_params()};
        ControllerRun run{controller};

        std::optional<uint16_t> ev_port;
        REQUIRE(poll_until(
            [&]() {
                ev_port = sdp_rx_port(own_sockets);
                return ev_port.has_value();
            },
            5s));

        WHEN("the SDP response arrives and the callback throws") {
            REQUIRE(responder.respond(*ev_port, secc.port()));

            THEN("the throw is caught, the run ends and stopped fires exactly once") {
                // Well under the 18 s setup timeout, the only other thing that could
                // end this run.
                REQUIRE(poll_until(
                    [&]() {
                        secc.service();
                        return stopped_count > 0;
                    },
                    2s));
                REQUIRE(connected_count == 1);
                REQUIRE(stopped_count == 1);
                // The throw pre-empted the data client, so the EV never connected.
                REQUIRE_FALSE(secc.accepted());
            }
        }
    }
}
