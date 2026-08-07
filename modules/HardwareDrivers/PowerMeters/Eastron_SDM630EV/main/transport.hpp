// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#ifndef EASTRON_SDM630EV_TRANSPORT_HPP
#define EASTRON_SDM630EV_TRANSPORT_HPP

#include <cstdint>
#include <vector>

#include <generated/interfaces/serial_communication_hub/Interface.hpp>

// Thin Modbus access layer. Retries per query are handled by SerialCommHub;
// recovery policy (error raising, reconfigure) lives in powermeterImpl.
namespace transport {

using DataVector = std::vector<std::uint8_t>;

enum class RegisterType {
    Input,  // FC04
    Holding // FC03
};

class AbstractModbusTransport {
public:
    AbstractModbusTransport() = default;
    virtual ~AbstractModbusTransport() = default;

    AbstractModbusTransport(const AbstractModbusTransport&) = delete;
    AbstractModbusTransport& operator=(const AbstractModbusTransport&) = delete;
    AbstractModbusTransport(AbstractModbusTransport&&) = delete;
    AbstractModbusTransport& operator=(AbstractModbusTransport&&) = delete;

    virtual DataVector fetch(std::int32_t address, std::uint16_t register_count, RegisterType type) = 0;
    virtual void write_multiple_registers(std::int32_t address, const std::vector<std::uint16_t>& data) = 0;
};

class SerialCommHubTransport : public AbstractModbusTransport {
public:
    SerialCommHubTransport(serial_communication_hubIntf& serial_hub, std::int32_t device_id) :
        m_serial_hub(serial_hub), m_device_id(device_id) {
    }

    DataVector fetch(std::int32_t address, std::uint16_t register_count, RegisterType type) override;
    void write_multiple_registers(std::int32_t address, const std::vector<std::uint16_t>& data) override;

private:
    serial_communication_hubIntf& m_serial_hub;
    std::int32_t m_device_id;
};

} // namespace transport

#endif // EASTRON_SDM630EV_TRANSPORT_HPP
