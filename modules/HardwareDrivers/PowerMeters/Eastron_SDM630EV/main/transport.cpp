// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "transport.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include <fmt/core.h>
#include <utils/exceptions.hpp>

#include "registers.hpp"

namespace transport {

namespace {

void throw_on_bad_status(types::serial_comm_hub_requests::StatusCodeEnum status, const char* operation) {
    if (status != types::serial_comm_hub_requests::StatusCodeEnum::Success) {
        throw std::runtime_error(fmt::format("Modbus {} failed with status: {}", operation,
                                             types::serial_comm_hub_requests::status_code_enum_to_string(status)));
    }
}

} // namespace

DataVector SerialCommHubTransport::fetch(std::int32_t address, std::uint16_t register_count, RegisterType type) {
    DataVector response;
    response.reserve(static_cast<std::size_t>(register_count) * 2U);

    std::uint16_t remaining = register_count;
    std::int32_t read_address = address;

    while (remaining > 0) {
        const std::uint16_t count = std::min(remaining, sdm630ev::registers::MAX_REGISTERS_PER_READ);

        types::serial_comm_hub_requests::Result result;
        try {
            result = (type == RegisterType::Input)
                         ? m_serial_hub.call_modbus_read_input_registers(m_device_id, read_address, count)
                         : m_serial_hub.call_modbus_read_holding_registers(m_device_id, read_address, count);
        } catch (const Everest::CmdTimeout& e) {
            throw std::runtime_error(
                fmt::format("Modbus read timeout: SerialCommHub command timed out ({})", e.what()));
        }

        throw_on_bad_status(result.status_code, "read");
        if (not result.value.has_value()) {
            throw std::runtime_error("Modbus read returned no data");
        }

        for (const auto item : result.value.value()) {
            response.push_back(static_cast<std::uint8_t>((item >> 8) & 0xFF));
            response.push_back(static_cast<std::uint8_t>(item & 0xFF));
        }

        read_address += count;
        remaining -= count;
    }

    return response;
}

void SerialCommHubTransport::write_multiple_registers(std::int32_t address, const std::vector<std::uint16_t>& data) {
    // The device accepts only one parameter per write message, so writes are
    // never chunked; callers pass exactly one parameter's registers.
    types::serial_comm_hub_requests::VectorUint16 data_raw;
    data_raw.data.assign(data.begin(), data.end());

    types::serial_comm_hub_requests::StatusCodeEnum status;
    try {
        status = m_serial_hub.call_modbus_write_multiple_registers(m_device_id, address, data_raw);
    } catch (const Everest::CmdTimeout& e) {
        throw std::runtime_error(fmt::format("Modbus write timeout: SerialCommHub command timed out ({})", e.what()));
    }
    throw_on_bad_status(status, "write");
}

} // namespace transport
