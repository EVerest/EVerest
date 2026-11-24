// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SunSpecSolarLimit.hpp"

#include <date/date.h>
#include <everest/logging.hpp>
#include <utils/date.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <netdb.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include <fmt/format.h>

#include <modbus-server/client.hpp>
#include <modbus-server/pdu_correlation.hpp>
#include <modbus-server/transport.hpp>
#include <modbus-server/transport_protocol.hpp>

#include <sunspec/sunspec_reader.hpp>

namespace module {

namespace {

constexpr const char* EXTERNAL_LIMIT_SOURCE = "SunSpecSolarLimit";
constexpr time_t CONNECT_TIMEOUT_S = 5;

// One Modbus TCP connection to the inverter. Responses are received on a
// dedicated thread, which the Modbus client library requires.
class InverterConnection {
public:
    InverterConnection(const std::string& host, int port, int unit_id) {
        socket_fd = connect_to(host, port);
        auto transport = std::make_shared<modbus_server::ModbusSocketTransport>(socket_fd);
        auto protocol = std::make_shared<modbus_server::ModbusTCPProtocol>(
            transport, static_cast<std::uint16_t>(unit_id), static_cast<std::uint16_t>(0));
        correlation = std::make_shared<modbus_server::PDUCorrelationLayer>(protocol);
        client = std::make_unique<modbus_server::client::ModbusClient>(correlation);
        receiver = std::thread([this]() {
            try {
                while (true) {
                    correlation->blocking_poll();
                }
            } catch (const std::exception&) {
                closed = true;
            }
        });
    }

    InverterConnection(const InverterConnection&) = delete;
    InverterConnection& operator=(const InverterConnection&) = delete;

    ~InverterConnection() {
        shutdown(socket_fd, SHUT_RDWR);
        receiver.join();
        close(socket_fd);
    }

    std::vector<std::uint16_t> read(std::uint16_t address, std::uint16_t count) {
        if (closed) {
            throw std::runtime_error("connection closed by the inverter");
        }
        return client->read_holding_registers(address, count);
    }

private:
    static int connect_to(const std::string& host, int port) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        const int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addresses);
        if (rc != 0) {
            throw std::runtime_error(fmt::format("cannot resolve {}: {}", host, gai_strerror(rc)));
        }

        std::string last_error = "no address";
        for (auto* a = addresses; a != nullptr; a = a->ai_next) {
            const int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0) {
                last_error = std::strerror(errno);
                continue;
            }
            // SO_SNDTIMEO also bounds connect() on Linux.
            timeval timeout{CONNECT_TIMEOUT_S, 0};
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
                freeaddrinfo(addresses);
                return fd;
            }
            last_error = std::strerror(errno);
            close(fd);
        }
        freeaddrinfo(addresses);
        throw std::runtime_error(fmt::format("cannot connect to {}:{}: {}", host, port, last_error));
    }

    int socket_fd{-1};
    std::shared_ptr<modbus_server::PDUCorrelationLayer> correlation;
    std::unique_ptr<modbus_server::client::ModbusClient> client;
    std::thread receiver;
    std::atomic_bool closed{false};
};

} // namespace

void SunSpecSolarLimit::init() {
    if (config.inverter_host.empty()) {
        throw std::runtime_error("inverter_host must be set");
    }

    sunspec_solar::PolicyConfig policy_config;
    policy_config.reserve_W = config.reserve_W;
    policy_config.fallback_power_W = config.fallback_power_W;
    policy_config.stale_timeout = std::chrono::seconds(config.stale_timeout_s);
    policy = std::make_unique<sunspec_solar::LimitPolicy>(policy_config);
}

void SunSpecSolarLimit::ready() {
    // The Modbus socket transport writes with send() without MSG_NOSIGNAL; an
    // inverter that closes the connection would otherwise end the process.
    std::signal(SIGPIPE, SIG_IGN);
    std::thread(&SunSpecSolarLimit::poll_loop, this).detach();
}

void SunSpecSolarLimit::poll_loop() {
    std::unique_ptr<InverterConnection> connection;
    std::optional<sunspec_solar::ProductionSource> source;
    // Problems are logged once until readings recover, not on every poll.
    bool problem_reported = false;

    const auto report_problem = [&problem_reported](const std::string& message) {
        if (!problem_reported) {
            EVLOG_warning << message;
            problem_reported = true;
        }
    };

    while (true) {
        try {
            if (!connection) {
                connection =
                    std::make_unique<InverterConnection>(config.inverter_host, config.inverter_port, config.unit_id);
            }
            const auto read = [&connection](std::uint16_t address, std::uint16_t count) {
                return connection->read(address, count);
            };
            if (!source.has_value()) {
                source = sunspec_solar::discover(read);
                EVLOG_info << fmt::format("Reading PV production from SunSpec Model {} at register {}",
                                          source->model_id, source->w_address);
            }

            const auto production_W = sunspec_solar::read_production_W(read, *source);
            if (production_W.has_value()) {
                policy->on_production(*production_W, sunspec_solar::LimitPolicy::Clock::now());
                if (problem_reported) {
                    EVLOG_info << "PV production readings resumed";
                    problem_reported = false;
                }
            } else {
                report_problem("Inverter reports PV power as not implemented");
            }
        } catch (const std::exception& e) {
            report_problem(fmt::format("Cannot read PV production: {}", e.what()));
            connection.reset();
            source.reset();
        }

        if (const auto limit = policy->limit_to_send(sunspec_solar::LimitPolicy::Clock::now())) {
            send_limit(*limit);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
    }
}

void SunSpecSolarLimit::send_limit(double limit_W) {
    types::energy::ScheduleReqEntry entry;
    entry.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    entry.limits_to_leaves.total_power_W = {static_cast<float>(limit_W), EXTERNAL_LIMIT_SOURCE};

    types::energy::ExternalLimits limits;
    limits.schedule_import = {entry};
    limits.schedule_export = {};
    limits.schedule_setpoints = {};

    EVLOG_debug << fmt::format("Solar charging limit {:.0f} W", limit_W);
    r_energy_node->call_set_external_limits(limits);
}

} // namespace module
