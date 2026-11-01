// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <iso15118/config.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/detail/message_2/decode_response.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/tbd_controller.hpp>

#include "mock_connection.hpp"

using iso15118::test::MockConnection;

namespace {

// Tracks every connection the controller creates so tests can drive the
// current session's transport and observe session recreation.
struct MockConnectionFactory {
    MockConnection* last{nullptr};
    int calls{0};

    iso15118::TbdController::ConnectionFactory fn() {
        return [this](iso15118::io::PollManager&, const std::string&) -> std::unique_ptr<iso15118::io::IConnection> {
            auto connection = std::make_unique<MockConnection>();
            last = connection.get();
            ++calls;
            return connection;
        };
    }
};

iso15118::config::SSLConfig make_ssl_config(const std::string& root) {
    iso15118::config::SSLConfig cfg{};
    iso15118::config::ChainConfig chain{};
    chain.path_certificate_chain = root + "/chain.pem";
    chain.path_certificate_key = root + "/key.pem";
    chain.trust_anchor_pem = root + "/v2g_root.pem";
    cfg.chains.push_back(std::move(chain));
    cfg.path_certificate_v2g_root = root + "/v2g_root.pem";
    return cfg;
}

iso15118::TbdController make_controller(const iso15118::session::feedback::Callbacks& callbacks,
                                        MockConnectionFactory& factory, iso15118::config::SSLConfig ssl = {}) {
    return iso15118::TbdController{iso15118::TbdConfig{std::move(ssl), "lo",
                                                       iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER,
                                                       /*enable_sdp_server=*/false},
                                   callbacks, iso15118::session::EvseSetupConfig{}, factory.fn()};
}

// Captured SupportedAppProtocolReq offering the -20:AC namespace.
constexpr uint8_t sap_req[] = {0x80, 0x00, 0xf3, 0xab, 0x93, 0x71, 0xd3, 0x4b, 0x9b, 0x79, 0xd3, 0x9b, 0xa3,
                               0x21, 0xd3, 0x4b, 0x9b, 0x79, 0xd1, 0x89, 0xa9, 0x89, 0x89, 0xc1, 0xd1, 0x69,
                               0x91, 0x81, 0xd2, 0x0a, 0x18, 0x01, 0x00, 0x00, 0x04, 0x00, 0x40};

// Captured SessionSetupReq with a zeroed session id (starts a new session).
constexpr uint8_t session_setup_req[] = {0x80, 0x8c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x9f,
                                         0x9c, 0x2b, 0xd0, 0x62, 0x0b, 0x2b, 0xa6, 0xa4, 0xab, 0x18, 0x99, 0x19, 0x9a,
                                         0x1a, 0x9b, 0x1b, 0x9c, 0x1c, 0x98, 0x20, 0xa1, 0x21, 0xa2, 0x22, 0xac, 0x00};

// Drive the current session until a response is deferred by the 100 ms rate limiter.
void arm_deferred_response(iso15118::TbdController& controller, MockConnection& conn) {
    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
    controller.tick();

    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::Part20Main, session_setup_req, sizeof(session_setup_req));
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
    controller.tick();
}

iso15118::TbdController make_controller(const iso15118::session::feedback::Callbacks& callbacks,
                                        MockConnectionFactory& factory, iso15118::session::EvseSetupConfig setup) {
    return iso15118::TbdController{iso15118::TbdConfig{{},
                                                       "lo",
                                                       iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER,
                                                       /*enable_sdp_server=*/false},
                                   callbacks, std::move(setup), factory.fn()};
}

// Payload of the last V2GTP frame the session wrote.
std::vector<uint8_t> last_frame_payload(const std::vector<uint8_t>& written) {
    const auto header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
    std::size_t pos = 0;
    std::vector<uint8_t> payload;
    while (pos + header_size <= written.size()) {
        const std::size_t len = (static_cast<std::size_t>(written[pos + 4]) << 24) |
                                (static_cast<std::size_t>(written[pos + 5]) << 16) |
                                (static_cast<std::size_t>(written[pos + 6]) << 8) | written[pos + 7];
        payload.assign(written.begin() + pos + header_size, written.begin() + pos + header_size + len);
        pos += header_size + len;
    }
    return payload;
}

// Ticks until the condition holds, bounded; the 100 ms response pacer needs real time to pass.
template <typename Condition> void tick_until(iso15118::TbdController& controller, Condition condition) {
    for (int i = 0; i < 20 and not condition(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        controller.tick();
    }
    REQUIRE(condition());
}

template <typename Request> void queue(MockConnection& conn, const Request& req) {
    std::array<uint8_t, 256> buffer{};
    const auto size = serialize(req, iso15118::io::StreamOutputView{buffer.data(), buffer.size()});
    conn.written.clear();
    conn.queue_v2gtp_packet(iso15118::io::v2gtp::PayloadType::SAP, buffer.data(), size);
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
}

// Sends an ISO 15118-2 request and waits for its (paced) response on the wire.
template <typename Request>
void exchange(iso15118::TbdController& controller, MockConnection& conn, const Request& req) {
    queue(conn, req);
    tick_until(controller, [&] { return not conn.written.empty(); });
}

// Decodes the last ISO 15118-2 response the session wrote; \p pick selects the body element.
template <typename Response, typename Pick> Response iso2_response(const MockConnection& conn, Pick pick) {
    const auto payload = last_frame_payload(conn.written);
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, const_cast<uint8_t*>(payload.data()), payload.size(), 0, nullptr);
    iso2_exiDocument doc{};
    REQUIRE(decode_iso2_exiDocument(&stream, &doc) == 0);
    Response result{};
    convert(doc.V2G_Message.Header, result.header);
    convert(pick(doc.V2G_Message.Body), result);
    return result;
}

// Drives an ISO 15118-2 EIM session to a pause after power delivery stopped and lets the EV close the
// connection, so the paused session is reaped with its pause context retained.
void pause_iso2_session(iso15118::TbdController& controller, MockConnectionFactory& factory) {
    namespace mdt = iso15118::message_2::datatypes;
    controller.tick();
    auto* conn = factory.last;
    conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);

    iso15118::message_20::SupportedAppProtocolRequest sap;
    sap.app_protocol.push_back({iso15118::ISO2_NAMESPACE, 2, 0, 1, 1});
    exchange(controller, *conn, sap);

    exchange(controller, *conn, iso15118::message_2::SessionSetupRequest{});
    const auto session_id = iso2_response<iso15118::message_2::SessionSetupResponse>(
                                *conn, [](const auto& body) -> const auto& {
                                    REQUIRE(body.SessionSetupRes_isUsed);
                                    return body.SessionSetupRes;
                                })
                                .header.session_id;

    iso15118::message_2::ServiceDiscoveryRequest discovery{};
    discovery.header.session_id = session_id;
    exchange(controller, *conn, discovery);

    iso15118::message_2::PaymentServiceSelectionRequest selection{};
    selection.header.session_id = session_id;
    selection.selected_payment_option = mdt::PaymentOption::ExternalPayment;
    selection.selected_service_list.push_back({1, std::nullopt});
    exchange(controller, *conn, selection);

    controller.send_control_event(iso15118::d20::AuthorizationResponse{true});
    iso15118::message_2::AuthorizationRequest auth{};
    auth.header.session_id = session_id;
    exchange(controller, *conn, auth);

    iso15118::message_2::ChargeParameterDiscoveryRequest parameters{};
    parameters.header.session_id = session_id;
    parameters.requested_energy_transfer_mode = mdt::EnergyTransferMode::AC_three_phase_core;
    auto& ev_params = parameters.ac_ev_charge_parameter.emplace();
    ev_params.ev_max_voltage = mdt::to_physical_value(230, mdt::Unit::V);
    ev_params.ev_max_current = mdt::to_physical_value(32, mdt::Unit::A);
    ev_params.ev_min_current = mdt::to_physical_value(6, mdt::Unit::A);
    exchange(controller, *conn, parameters);
    const auto schedules = iso2_response<iso15118::message_2::ChargeParameterDiscoveryResponse>(
        *conn, [](const auto& body) -> const auto& {
            REQUIRE(body.ChargeParameterDiscoveryRes_isUsed);
            return body.ChargeParameterDiscoveryRes;
        });
    REQUIRE(schedules.sa_schedule_list.has_value());

    iso15118::message_2::PowerDeliveryRequest power{};
    power.header.session_id = session_id;
    power.sa_schedule_tuple_id = schedules.sa_schedule_list->front().sa_schedule_tuple_id;
    power.charge_progress = mdt::ChargeProgress::Stop;
    exchange(controller, *conn, power);

    iso15118::message_2::SessionStopRequest stop{};
    stop.header.session_id = session_id;
    stop.charging_session = mdt::ChargingSession::Pause;
    exchange(controller, *conn, stop);
    REQUIRE(iso2_response<iso15118::message_2::SessionStopResponse>(
                *conn, [](const auto& body) -> const auto& {
                    REQUIRE(body.SessionStopRes_isUsed);
                    return body.SessionStopRes;
                })
                .response_code == mdt::ResponseCode::OK);

    conn->fire(iso15118::io::ConnectionEvent::CLOSED);
    const int sessions_before = factory.calls;
    tick_until(controller, [&] { return factory.calls > sessions_before; });
}

} // namespace

SCENARIO("Data-link loss tears down the active session") {
    bool terminate_signaled = false;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [&](iso15118::session::feedback::Signal s) {
        if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
            terminate_signaled = true;
        }
    };

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.last != nullptr);
    factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);

    terminate_signaled = false;

    WHEN("the data link drops and the loop ticks") {
        controller.set_dlink_ready(false);
        controller.tick();

        THEN("the session is torn down") {
            REQUIRE(terminate_signaled);
        }
    }
}

SCENARIO("Data-link loss tears down a session with a deferred response") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.calls == 1);
    factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    arm_deferred_response(controller, *factory.last);

    WHEN("the data link drops and the loop ticks") {
        controller.set_dlink_ready(false);
        controller.tick();

        THEN("the session with the pending response is reaped and replaced") {
            REQUIRE(factory.calls == 2);
        }
    }
}

SCENARIO("Data-link loss reaps a never-connected session") {
    bool terminate_signaled = false;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [&](iso15118::session::feedback::Signal s) {
        if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
            terminate_signaled = true;
        }
    };

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.last != nullptr);

    terminate_signaled = false;

    WHEN("the data link drops and the loop ticks") {
        controller.set_dlink_ready(false);
        controller.tick();

        THEN("the never-connected session is torn down") {
            REQUIRE(terminate_signaled);
        }
    }
}

SCENARIO("A terminate racing session creation wins over the fresh session") {
    bool terminate_signaled = false;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [&](iso15118::session::feedback::Signal s) {
        if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
            terminate_signaled = true;
        }
    };

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    GIVEN("a session that exists when a data-link terminate is raised") {
        controller.tick();
        REQUIRE(factory.calls == 1);
        factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);

        // A terminate raised while the session is live (as a dlink drop racing an
        // SDP-created session would) must not be swallowed.
        controller.set_dlink_ready(false);
        terminate_signaled = false;

        WHEN("the loop ticks") {
            controller.tick();

            THEN("the session is reaped and a subsequent request creates a fresh one") {
                REQUIRE(terminate_signaled);
                REQUIRE(factory.calls == 2);

                AND_WHEN("the fresh session is accepted and the loop ticks again") {
                    factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);
                    terminate_signaled = false;
                    controller.tick();

                    THEN("the recovered session survives") {
                        REQUIRE_FALSE(terminate_signaled);
                        REQUIRE(controller.has_active_session());
                        REQUIRE(factory.calls == 2);
                    }
                }
            }
        }
    }
}

SCENARIO("SAE DER setup updates from another thread race session teardown safely") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    controller.tick();
    REQUIRE(factory.calls == 1);

    WHEN("SAE DER setup configs are pushed while sessions are reaped and recreated") {
        constexpr int session_cycles = 200;
        std::atomic<bool> done{false};

        std::thread updater([&] {
            const auto setup_config = iso15118::d20::make_inert_default_sae_setup_config(230.0F);
            while (not done.load()) {
                controller.update_der_sae_limits(iso15118::d20::SaeDerTransferLimits{}, setup_config);
            }
        });

        for (int i = 0; i < session_cycles; i++) {
            controller.set_dlink_ready(false);
            controller.tick();
        }

        done.store(true);
        updater.join();

        THEN("every cycle replaced the session") {
            REQUIRE(factory.calls == session_cycles + 1);
        }
    }
}

SCENARIO("The rotation seam exposes the rotated SSL config") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    MockConnectionFactory factory;
    const auto initial = make_ssl_config("/tmp/iso15118-test/a");
    auto controller = make_controller(callbacks, factory, initial);

    WHEN("set_ssl_config() rotates the config") {
        const auto rotated = make_ssl_config("/tmp/iso15118-test/b");
        controller.set_ssl_config(rotated);

        THEN("connection_ssl_config() returns the rotated config, not the construction-time one") {
            REQUIRE(controller.connection_ssl_config() == rotated);
            REQUIRE_FALSE(controller.connection_ssl_config() == initial);
        }
    }
}

SCENARIO("A paused session can be resumed until the EV is unplugged") {
    namespace dt = iso15118::message_20::datatypes;
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    iso15118::session::EvseSetupConfig setup{};
    setup.evse_id = "DE*EVR*E00000*1";
    setup.supported_energy_services = {dt::ServiceCategory::AC};
    setup.supported_protocols = {iso15118::ProtocolId::ISO15118_2};
    setup.ac_limits.charge_power.max = dt::from_float(11000);
    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory, std::move(setup));

    pause_iso2_session(controller, factory);
    REQUIRE(controller.has_pause_context());

    WHEN("the EV stays plugged in") {
        controller.send_control_event(iso15118::d20::CpStateChanged{iso15118::d20::CpState::B});
        controller.tick();

        THEN("the session can still be resumed") {
            REQUIRE(controller.has_pause_context());
        }
    }

    WHEN("the EV is unplugged") {
        controller.send_control_event(iso15118::d20::CpStateChanged{iso15118::d20::CpState::A});
        controller.tick();

        THEN("nothing is left to resume") {
            REQUIRE_FALSE(controller.has_pause_context());
        }
    }
}

SCENARIO("An unplug reported during a running session takes effect when it ends") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    MockConnectionFactory factory;
    auto controller = make_controller(callbacks, factory);

    // A vehicle certificate makes the new -20 session resumable
    controller.tick();
    auto* conn = factory.last;
    conn->vehicle_cert_hash.emplace().fill(0x42);
    conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    conn->fire(iso15118::io::ConnectionEvent::OPEN);
    arm_deferred_response(controller, *conn);
    tick_until(controller, [&] { return controller.has_pause_context(); });

    controller.send_control_event(iso15118::d20::CpStateChanged{iso15118::d20::CpState::A});
    controller.tick();
    THEN("the running session keeps its context until the connection is gone") {
        REQUIRE(controller.has_pause_context());
        conn->fire(iso15118::io::ConnectionEvent::CLOSED);
        tick_until(controller, [&] { return factory.calls == 2; });
        REQUIRE_FALSE(controller.has_pause_context());
    }
}
