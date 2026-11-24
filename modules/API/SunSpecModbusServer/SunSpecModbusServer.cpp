// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SunSpecModbusServer.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include <fmt/format.h>

#include <modbus-server/frames/read_holding_registers.hpp>
#include <modbus-server/modbus_basic_server.hpp>
#include <modbus-server/pdu_correlation.hpp>
#include <modbus-server/server_exception.hpp>
#include <modbus-server/transport.hpp>
#include <modbus-server/transport_protocol.hpp>

namespace module {

namespace {

constexpr int MAX_CLIENTS = 4;
constexpr time_t CLIENT_RECEIVE_TIMEOUT_S = 60;
// Limit of the Modbus read holding registers function.
constexpr std::uint16_t MAX_READ_REGISTERS = 125;

sunspec::AcType parse_ac_type(const std::string& ac_type) {
    if (ac_type == "split_phase") {
        return sunspec::AcType::SplitPhase;
    }
    if (ac_type == "three_phase") {
        return sunspec::AcType::ThreePhase;
    }
    return sunspec::AcType::SinglePhase;
}

sunspec::Measurements to_measurements(const types::powermeter::Powermeter& powermeter) {
    sunspec::Measurements m;
    if (powermeter.power_W.has_value()) {
        const auto& p = powermeter.power_W.value();
        m.power_W = {p.total, p.L1, p.L2, p.L3};
    }
    if (powermeter.current_A.has_value()) {
        const auto& a = powermeter.current_A.value();
        m.current_A = {a.L1, a.L2, a.L3};
    }
    if (powermeter.voltage_V.has_value()) {
        const auto& v = powermeter.voltage_V.value();
        m.voltage_V = {v.L1, v.L2, v.L3};
    }
    if (powermeter.frequency_Hz.has_value()) {
        m.frequency_Hz = powermeter.frequency_Hz->L1;
    }
    const auto& imported = powermeter.energy_Wh_import;
    m.energy_import_Wh = {imported.total, imported.L1, imported.L2, imported.L3};
    if (powermeter.energy_Wh_export.has_value()) {
        const auto& e = powermeter.energy_Wh_export.value();
        m.energy_export_Wh = {e.total, e.L1, e.L2, e.L3};
    }
    return m;
}

} // namespace

void SunSpecModbusServer::init() {
    sunspec::CommonInfo info;
    info.manufacturer = config.manufacturer;
    info.model = config.model;
    info.options = config.options;
    info.version = config.version;
    info.serial_number = config.serial_number;
    info.device_address = static_cast<std::uint16_t>(config.device_address);

    const sunspec::Ratings ratings{config.max_power_W, config.max_current_A, config.max_voltage_V};
    register_map = std::make_unique<sunspec::RegisterMap>(info, parse_ac_type(config.ac_type), ratings);

    r_evse_manager->subscribe_powermeter([this](const types::powermeter::Powermeter& powermeter) {
        const auto measurements = to_measurements(powermeter);
        std::lock_guard<std::mutex> lock(register_map_mutex);
        register_map->update(measurements);
    });
}

void SunSpecModbusServer::ready() {
    // The Modbus socket transport writes with send() without MSG_NOSIGNAL; a
    // client that disconnects mid-response would otherwise end the process.
    std::signal(SIGPIPE, SIG_IGN);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(config.port));
    if (inet_pton(AF_INET, config.listen_address.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error(fmt::format("Invalid listen_address '{}'", config.listen_address));
    }

    const int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        throw std::runtime_error(fmt::format("Could not create socket: {}", std::strerror(errno)));
    }
    const int reuse = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(listen_socket, MAX_CLIENTS) < 0) {
        const std::string error = std::strerror(errno);
        close(listen_socket);
        throw std::runtime_error(
            fmt::format("Could not listen on {}:{}: {}", config.listen_address, config.port, error));
    }

    EVLOG_info << fmt::format("SunSpec Modbus TCP server listening on {}:{}", config.listen_address, config.port);
    std::thread(&SunSpecModbusServer::accept_loop, this, listen_socket).detach();
}

void SunSpecModbusServer::accept_loop(int listen_socket) {
    while (true) {
        const int client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket < 0) {
            if (errno != EINTR) {
                EVLOG_error << fmt::format("Modbus accept failed: {}", std::strerror(errno));
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }
        if (active_clients.load() >= MAX_CLIENTS) {
            EVLOG_warning << "Rejecting Modbus client: connection limit reached";
            close(client_socket);
            continue;
        }

        // A client that stops sending is dropped instead of holding a slot forever.
        timeval timeout{CLIENT_RECEIVE_TIMEOUT_S, 0};
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        ++active_clients;
        std::thread([this, client_socket]() {
            serve_client(client_socket);
            close(client_socket);
            --active_clients;
        }).detach();
    }
}

void SunSpecModbusServer::serve_client(int client_socket) {
    using namespace modbus_server;

    auto transport = std::make_shared<ModbusSocketTransport>(client_socket);
    auto protocol = std::make_shared<ModbusTCPProtocol>(transport);
    auto correlation = std::make_shared<PDUCorrelationLayer>(protocol);
    ModbusBasicServer server(correlation);
    server.set_read_holding_registers_request_cb([this](const pdu::ReadHoldingRegistersRequest& request) {
        return pdu::ReadHoldingRegistersResponse(request,
                                                 read_registers(request.register_start, request.register_count));
    });

    try {
        while (true) {
            correlation->blocking_poll();
        }
    } catch (const transport_exceptions::ConnectionClosedException&) {
        EVLOG_debug << "Modbus client disconnected";
    } catch (const std::exception& e) {
        EVLOG_info << fmt::format("Closing Modbus client connection: {}", e.what());
    }
}

std::vector<std::uint8_t> SunSpecModbusServer::read_registers(std::uint16_t address, std::uint16_t count) {
    using modbus_server::ApplicationServerError;
    using modbus_server::pdu::PDUExceptionCode;

    if (count == 0 || count > MAX_READ_REGISTERS) {
        throw ApplicationServerError(PDUExceptionCode::ILLEGAL_DATA_VALUE);
    }

    std::vector<std::uint16_t> words;
    try {
        std::lock_guard<std::mutex> lock(register_map_mutex);
        words = register_map->read(address, count);
    } catch (const std::out_of_range&) {
        throw ApplicationServerError(PDUExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2);
    for (const auto word : words) {
        bytes.push_back(static_cast<std::uint8_t>(word >> 8));
        bytes.push_back(static_cast<std::uint8_t>(word & 0xFF));
    }
    return bytes;
}

} // namespace module
