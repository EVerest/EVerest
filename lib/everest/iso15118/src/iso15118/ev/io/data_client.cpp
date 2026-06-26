// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/io/data_client.hpp>

#include <utility>

#include <arpa/inet.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::ev::io {

namespace {
// libio tcp_socket::setup expects a connect timeout in milliseconds.
constexpr int CONNECT_TIMEOUT_MS = 5000;
} // namespace

DataClient::DataClient(everest::lib::io::event::fd_event_handler& handler_) : handler(handler_) {
}

void DataClient::fire_failed() {
    if (failed_fired) {
        return;
    }
    failed_fired = true;
    if (on_failed) {
        on_failed();
    }
}

void DataClient::connect(const iso15118::io::Ipv6EndPoint& endpoint, const std::string& device,
                         std::function<void()> on_connected_, std::function<void()> on_failed_) {
    on_connected = std::move(on_connected_);
    on_failed = std::move(on_failed_);
    connected_fired = false;
    failed_fired = false;

    // A reconnect must tear down the prior registration first: register_events
    // short-circuits on the registered guard, so without this the fresh client
    // would never be registered with the reactor and its outcome never fire.
    if (client) {
        handler.unregister_event_handler(client.get());
    }
    registered = false;

    char remote[INET6_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET6, endpoint.address, remote, sizeof(remote)) == nullptr) {
        logf_error("DataClient::connect failed to format the SECC address");
        fire_failed();
        return;
    }

    // The scope for a link-local SECC address is supplied by binding the socket
    // to the egress interface via SO_BINDTODEVICE; without it connect() to an
    // fe80:: address fails with EINVAL. An empty device leaves the socket unbound
    // (the unit-test path); the production caller always passes the egress device.
    try {
        // The tcp_client ctor registers and make_shared internally and is not
        // noexcept; a throw must not escape with client null and on_failed
        // un-fired, or a caller awaiting the outcome would hang forever.
        client = std::make_unique<everest::lib::io::tcp::tcp_client>(std::string(remote), endpoint.port,
                                                                     CONNECT_TIMEOUT_MS, device);

        client->set_rx_handler([this](std::vector<uint8_t> const& bytes, auto& /*device*/) {
            if (m_on_rx) {
                m_on_rx(bytes);
            }
        });

        // libio connects on a detached thread and routes connect/socket failures
        // through the error handler; without this, a failed or timed-out connect
        // is invisible because on_connected simply never fires. The handler also
        // fires on the error-cleared transition (errno 0), which is not a failure.
        client->set_error_handler([this](int error, std::string const& msg) {
            if (error == 0) {
                // error-cleared transition, not a failure
                return;
            }
            logf_error("DataClient TCP error %d: %s", error, msg.c_str());
            fire_failed();
        });

        // set_on_ready_action is persistent and fires on every ready transition,
        // including internal resets; a one-shot guard keeps the on_connected
        // contract (fired exactly once per connect).
        client->set_on_ready_action([this]() {
            if (connected_fired) {
                return;
            }
            connected_fired = true;
            if (on_connected) {
                on_connected();
            }
        });
    } catch (const std::exception& e) {
        logf_error("DataClient::connect failed to construct the TCP client: %s", e.what());
        client.reset();
        fire_failed();
        return;
    }

    if (not register_events(handler)) {
        logf_error("DataClient::connect failed to register the TCP client");
        fire_failed();
    }
}

bool DataClient::send(const std::vector<uint8_t>& frame) {
    if (not client) {
        logf_error("DataClient::send called before connect");
        return false;
    }
    return client->tx(frame);
}

void DataClient::on_rx(std::function<void(const std::vector<uint8_t>&)> handler_) {
    m_on_rx = std::move(handler_);
}

bool DataClient::register_events(everest::lib::io::event::fd_event_handler& handler_) {
    // Idempotency guard: a second call is a clean no-op. The tcp_client registers
    // via the fd-keyed fd_event_sync_interface overload (which would reject the
    // duplicate fd), and the guard also avoids re-running the registration setup.
    if (registered) {
        return true;
    }

    if (not client) {
        logf_error("DataClient::register_events called before connect");
        return false;
    }

    const auto ok = handler_.register_event_handler(client.get());
    if (not ok) {
        // Keep the invariant "client non-null <=> registered": a half-registered
        // client must not survive, or a later send() would pass its non-null
        // guard and buffer bytes into a client the reactor never pumps.
        logf_error("Failed to register the TCP data client with the event handler");
        client.reset();
        return false;
    }
    registered = true;
    return true;
}

} // namespace iso15118::ev::io
