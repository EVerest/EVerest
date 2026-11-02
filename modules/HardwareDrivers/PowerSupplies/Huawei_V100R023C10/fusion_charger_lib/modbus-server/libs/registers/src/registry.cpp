// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <modbus-registers/registry.hpp>

using namespace modbus::registers::complex_registers;
using namespace modbus::registers::registry;

ComplexRegisterSubregistry::ComplexRegisterSubregistry() : registers() {
}

void ComplexRegisterSubregistry::add(ComplexRegisterUntyped* reg) {
    registers.push_back(std::unique_ptr<ComplexRegisterUntyped>(reg));
}

void ComplexRegisterSubregistry::verify_internal_overlap() {
    std::vector<std::uint16_t> used_addresses;
    for (auto& reg : registers) {
        for (std::uint16_t i = 0; i < reg->get_size(); ++i) {
            std::uint16_t address = reg->get_start_address() + i;
            if (std::find(used_addresses.begin(), used_addresses.end(), address) != used_addresses.end()) {
                throw std::runtime_error("Overlapping register addresses");
            }
            used_addresses.push_back(address);
        }
    }
}

std::vector<ComplexRegisterUntyped*> ComplexRegisterSubregistry::get_registers() {
    std::vector<complex_registers::ComplexRegisterUntyped*> result;
    for (auto& reg : registers) {
        result.push_back(reg.get());
    }
    return result;
}

void ComplexRegisterRegistry::verify_overlap() {
    std::vector<std::uint16_t> used_addresses;

    for (auto& reg : get_all_registers()) {
        for (std::uint16_t i = 0; i < reg->get_size(); ++i) {
            std::uint16_t address = reg->get_start_address() + i;
            if (std::find(used_addresses.begin(), used_addresses.end(), address) != used_addresses.end()) {
                throw std::runtime_error("Overlapping register addresses");
            }
            used_addresses.push_back(address);
        }
    }
}

void ComplexRegisterRegistry::on_write(std::uint16_t address, const std::vector<std::uint8_t>& data) {
    if (data.size() % 2 != 0) {
        throw std::runtime_error("Data size must be a multiple of 2 bytes");
    }

    // unit: 2b
    const std::uint32_t write_start = address;
    // unit: 2b, exclusive
    const std::uint32_t write_end = write_start + data.size() / 2;

    for (auto& reg : get_all_registers()) {
        // unit: 2b
        const std::uint32_t reg_start = reg->get_start_address();
        // unit: 2b, exclusive
        const std::uint32_t reg_end = reg_start + reg->get_size();

        if (std::max(write_start, reg_start) >= std::min(write_end, reg_end)) {
            continue;
        }

        if (write_start < reg_start) {
            const auto skipped_bytes = static_cast<std::ptrdiff_t>(reg_start - write_start) * 2;
            reg->on_write(0, std::vector<std::uint8_t>(data.begin() + skipped_bytes, data.end()));
        } else {
            reg->on_write(write_start - reg_start, data);
        }
    }
}

std::vector<std::uint8_t> ComplexRegisterRegistry::on_read(std::uint16_t address, std::uint16_t size) {
    if (size == 0) {
        return {};
    }

    std::vector<std::uint8_t> result(size * 2, 0);
    std::vector<bool> is_empty(size * 2, true);

    // byte addresses, end exclusive
    const std::uint32_t read_start_b = std::uint32_t{address} * 2;
    const std::uint32_t read_end_b = read_start_b + result.size();

    for (auto& reg : get_all_registers()) {
        const std::uint32_t reg_start_b = std::uint32_t{reg->get_start_address()} * 2;
        const std::uint32_t reg_end_b = reg_start_b + std::uint32_t{reg->get_size()} * 2;

        const std::uint32_t copy_start_b = std::max(read_start_b, reg_start_b);
        const std::uint32_t copy_end_b = std::min(read_end_b, reg_end_b);
        if (copy_start_b >= copy_end_b) {
            continue;
        }

        const std::vector<std::uint8_t> data = reg->on_read();
        const std::uint32_t data_end_b = std::min<std::uint32_t>(copy_end_b, reg_start_b + data.size());

        for (std::uint32_t byte_address = copy_start_b; byte_address < data_end_b; ++byte_address) {
            const std::uint32_t result_index = byte_address - read_start_b;
            if (is_empty[result_index]) {
                result[result_index] = data[byte_address - reg_start_b];
                is_empty[result_index] = false;
            }
        }
    }

    // note: empty bytes are already filled with 0 by default (see initializer of
    // result)

    return result;
}

void ComplexRegisterRegistry::add(std::shared_ptr<ComplexRegisterSubregistry> registry) {
    this->registries.push_back(std::move(registry));
}

std::vector<ComplexRegisterUntyped*> ComplexRegisterRegistry::get_all_registers() {
    std::vector<ComplexRegisterUntyped*> result;
    for (auto& reg : registries) {
        for (auto& subreg : reg->get_registers()) {
            result.push_back(subreg);
        }
    }
    return result;
}
