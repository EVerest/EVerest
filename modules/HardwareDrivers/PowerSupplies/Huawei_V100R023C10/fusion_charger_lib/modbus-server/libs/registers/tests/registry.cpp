// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>

#include <modbus-registers/registry.hpp>

using namespace modbus::registers::complex_registers;
using namespace modbus::registers::registry;
using namespace modbus::registers::data_providers;
using namespace modbus::registers::converters;

namespace {

class FixedDataRegister : public ComplexRegisterUntyped {
    std::vector<std::uint8_t> data;

public:
    FixedDataRegister(std::uint16_t start_address, std::uint16_t size, std::vector<std::uint8_t> data) :
        ComplexRegisterUntyped(start_address, size), data(std::move(data)) {
    }

    void on_write(std::uint16_t /*offset*/, const std::vector<std::uint8_t>& /*data*/) override {
    }

    std::vector<std::uint8_t> on_read() override {
        return data;
    }

    DataProviderUntyped* get_data_provider() const override {
        return nullptr;
    }
};

// Memory registers with an identity converter, so a register's Modbus bytes are its provider's bytes
class RegisterMap {
    std::shared_ptr<ComplexRegisterSubregistry> subregistry = std::make_shared<ComplexRegisterSubregistry>();
    std::vector<std::function<void(std::map<std::uint32_t, std::uint8_t>&)>> collectors;
    std::vector<std::shared_ptr<void>> providers;
    std::map<std::uint32_t, std::uint8_t> expected_bytes;

public:
    ComplexRegisterRegistry registry;
    int write_callbacks = 0;

    template <size_t array_size> void add(std::uint16_t word_address) {
        const std::uint32_t start_b = std::uint32_t{word_address} * 2;
        std::vector<std::uint8_t> initial(array_size);
        for (size_t i = 0; i < array_size; ++i) {
            initial[i] = static_cast<std::uint8_t>((start_b + i) * 7 + 1);
            expected_bytes[start_b + i] = initial[i];
        }

        auto provider = std::make_shared<DataProviderMemoryHolding<array_size>>(initial);
        provider->add_write_callback([this](std::uint8_t*) { ++write_callbacks; });
        subregistry->add(new MemoryRegister<array_size>(word_address, *provider, ConverterIdentity::instance()));
        collectors.push_back([provider, start_b](std::map<std::uint32_t, std::uint8_t>& out) {
            for (size_t i = 0; i < array_size; ++i) {
                out[start_b + i] = provider->get_value()[i];
            }
        });
        providers.push_back(provider);
    }

    void finish() {
        registry.add(subregistry);
        registry.verify_overlap();
    }

    std::vector<std::uint8_t> expected_read(std::uint16_t address, std::uint16_t count) const {
        std::vector<std::uint8_t> expected(count * 2, 0);
        for (std::uint32_t i = 0; i < expected.size(); ++i) {
            const auto it = expected_bytes.find(std::uint32_t{address} * 2 + i);
            if (it != expected_bytes.end()) {
                expected[i] = it->second;
            }
        }
        return expected;
    }

    void apply_expected_write(std::uint16_t address, const std::vector<std::uint8_t>& data) {
        for (std::uint32_t i = 0; i < data.size(); ++i) {
            const auto it = expected_bytes.find(std::uint32_t{address} * 2 + i);
            if (it != expected_bytes.end()) {
                it->second = data[i];
            }
        }
    }

    const std::map<std::uint32_t, std::uint8_t>& expected() const {
        return expected_bytes;
    }

    std::map<std::uint32_t, std::uint8_t> stored() const {
        std::map<std::uint32_t, std::uint8_t> out;
        for (const auto& collect : collectors) {
            collect(out);
        }
        return out;
    }
};

// Registers of one, two, four and eight words, adjacent and with gaps between them
void add_mixed_layout(RegisterMap& map) {
    map.add<2>(0x0010);
    map.add<4>(0x0011);
    map.add<8>(0x0014);
    map.add<16>(0x001A);
    map.add<4>(0x0022);
    map.finish();
}

// Registers at both ends of the address space; reads and writes past 0xFFFF must not wrap around
void add_address_space_edges_layout(RegisterMap& map) {
    map.add<2>(0x0000);
    map.add<4>(0x0001);
    map.add<8>(0xFFFB);
    map.add<2>(0xFFFF);
    map.finish();
}

void read_every_window(RegisterMap& map, std::uint32_t first_address, std::uint32_t last_address,
                       std::uint16_t max_count) {
    for (std::uint32_t address = first_address; address <= last_address; ++address) {
        for (std::uint16_t count = 1; count <= max_count; ++count) {
            const auto data = map.registry.on_read(address, count);
            ASSERT_EQ(data, map.expected_read(address, count)) << "address " << address << " count " << count;
        }
    }
}

void write_every_window(RegisterMap& map, std::uint32_t first_address, std::uint32_t last_address,
                        std::uint16_t max_count) {
    std::uint8_t pattern = 0;
    for (std::uint32_t address = first_address; address <= last_address; ++address) {
        for (std::uint16_t count = 1; count <= max_count; ++count) {
            std::vector<std::uint8_t> data(count * 2);
            for (auto& byte : data) {
                byte = pattern;
                pattern += 31;
            }

            map.registry.on_write(address, data);
            map.apply_expected_write(address, data);
            ASSERT_EQ(map.stored(), map.expected()) << "address " << address << " count " << count;
        }
    }
}

} // namespace

TEST(Registry, write_cases) {
    DataProviderHolding<std::uint32_t> provider(0);
    DataProviderHolding<std::uint64_t> provider2(0);

    auto subregistry = std::make_shared<ComplexRegisterSubregistry>();
    subregistry->add(new ElementaryRegister<std::uint32_t>(0x0010, provider, ConverterABCD::instance()));
    subregistry->add(new ElementaryRegister<std::uint64_t>(0x0012, provider2, ConverterABCD::instance()));

    EXPECT_NO_THROW(subregistry->verify_internal_overlap());

    ComplexRegisterRegistry registry;
    registry.add(subregistry);
    EXPECT_NO_THROW(registry.verify_overlap());

    //  [  ][      ]
    //  |--|
    registry.on_write(0x0010, {0x12, 0x34, 0x56, 0x78});
    EXPECT_EQ(provider.get_value(), 0x12345678);

    // [  ]
    //   ||
    registry.on_write(0x0011, {0xde, 0xad});
    EXPECT_EQ(provider.get_value(), 0x1234dead);

    //   [  ]
    // |--|
    registry.on_write(0x000F, {0xbe, 0xef, 0xca, 0xfe});
    EXPECT_EQ(provider.get_value(), 0xcafedead);

    // [  ][      ]
    //     |--|
    registry.on_write(0x0012, {0x01, 0x02, 0x03, 0x04});
    EXPECT_EQ(provider2.get_value(), 0x0102030400000000);

    //   [  ][      ]
    // |------|
    registry.on_write(0x000F, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    EXPECT_EQ(provider.get_value(), 0x03040506);
    EXPECT_EQ(provider2.get_value(), 0x0708030400000000);

    // [  ][      ]
    //     |------|
    registry.on_write(0x0012, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    EXPECT_EQ(provider.get_value(), 0x03040506);
    EXPECT_EQ(provider2.get_value(), 0x0102030405060708);
}

TEST(Registry, read_cases) {
    DataProviderHolding<std::uint32_t> provider(0x0010);
    DataProviderHolding<std::uint64_t> provider2(0x0012);

    auto subregistry = std::make_shared<ComplexRegisterSubregistry>();
    subregistry->add(new ElementaryRegister<std::uint32_t>(0x0010, provider, ConverterABCD::instance()));
    subregistry->add(new ElementaryRegister<std::uint64_t>(0x0012, provider2, ConverterABCD::instance()));

    EXPECT_NO_THROW(subregistry->verify_internal_overlap());

    ComplexRegisterRegistry registry;
    registry.add(subregistry);
    EXPECT_NO_THROW(registry.verify_overlap());

    provider.update_value(0x12345678);
    provider2.update_value(0x0102030405060708);

    auto data = registry.on_read(0x0010, 0);
    EXPECT_EQ(data.size(), 0);

    // [  ][    ]
    //     |--|
    data = registry.on_read(0x0012, 2);
    EXPECT_EQ(data.size(), 4);
    EXPECT_EQ(data[0], 0x01);
    EXPECT_EQ(data[1], 0x02);
    EXPECT_EQ(data[2], 0x03);
    EXPECT_EQ(data[3], 0x04);

    // [  ][      ]
    //     |------|
    data = registry.on_read(0x0012, 4);
    EXPECT_EQ(data.size(), 8);
    EXPECT_EQ(data[0], 0x01);
    EXPECT_EQ(data[1], 0x02);
    EXPECT_EQ(data[2], 0x03);
    EXPECT_EQ(data[3], 0x04);
    EXPECT_EQ(data[4], 0x05);
    EXPECT_EQ(data[5], 0x06);
    EXPECT_EQ(data[6], 0x07);
    EXPECT_EQ(data[7], 0x08);

    // [  ][      ]
    //   |--|
    data = registry.on_read(0x0011, 2);
    EXPECT_EQ(data.size(), 4);
    EXPECT_EQ(data[0], 0x56);
    EXPECT_EQ(data[1], 0x78);
    EXPECT_EQ(data[2], 0x01);
    EXPECT_EQ(data[3], 0x02);

    // [  ][      ]
    // |--|
    data = registry.on_read(0x0010, 2);
    EXPECT_EQ(data.size(), 4);
    EXPECT_EQ(data[0], 0x12);
    EXPECT_EQ(data[1], 0x34);
    EXPECT_EQ(data[2], 0x56);
    EXPECT_EQ(data[3], 0x78);

    // [  ][      ]
    // |----------|
    data = registry.on_read(0x0010, 6);
    EXPECT_EQ(data.size(), 12);
    EXPECT_EQ(data[0], 0x12);
    EXPECT_EQ(data[1], 0x34);
    EXPECT_EQ(data[2], 0x56);
    EXPECT_EQ(data[3], 0x78);
    EXPECT_EQ(data[4], 0x01);
    EXPECT_EQ(data[5], 0x02);
    EXPECT_EQ(data[6], 0x03);
    EXPECT_EQ(data[7], 0x04);
    EXPECT_EQ(data[8], 0x05);
    EXPECT_EQ(data[9], 0x06);
    EXPECT_EQ(data[10], 0x07);
    EXPECT_EQ(data[11], 0x08);
}

TEST(Registry, read_ending_one_word_before_register_end) {
    DataProviderHolding<std::uint64_t> provider(0x0102030405060708);

    auto subregistry = std::make_shared<ComplexRegisterSubregistry>();
    subregistry->add(new ElementaryRegister<std::uint64_t>(0x0012, provider, ConverterABCD::instance()));

    ComplexRegisterRegistry registry;
    registry.add(subregistry);

    // [      ]
    // |----|
    EXPECT_EQ(registry.on_read(0x0012, 3), (std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}));

    // [      ]
    //   |--|
    EXPECT_EQ(registry.on_read(0x0013, 2), (std::vector<std::uint8_t>{0x03, 0x04, 0x05, 0x06}));

    //  [      ]
    // |------|
    EXPECT_EQ(registry.on_read(0x0011, 4), (std::vector<std::uint8_t>{0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}));
}

TEST(Registry, read_every_window_of_mixed_layout) {
    RegisterMap map;
    add_mixed_layout(map);

    read_every_window(map, 0x0008, 0x0028, 40);
}

TEST(Registry, read_every_window_at_address_space_edges) {
    RegisterMap map;
    add_address_space_edges_layout(map);

    read_every_window(map, 0x0000, 0x0010, 125);
    read_every_window(map, 0xFF80, 0xFFFF, 125);

    EXPECT_EQ(map.registry.on_read(0xFFFF, 0xFFFF), map.expected_read(0xFFFF, 0xFFFF));
}

TEST(Registry, write_every_window_of_mixed_layout) {
    RegisterMap map;
    add_mixed_layout(map);

    write_every_window(map, 0x0008, 0x0028, 40);
}

TEST(Registry, write_every_window_at_address_space_edges) {
    RegisterMap map;
    add_address_space_edges_layout(map);

    write_every_window(map, 0x0000, 0x0010, 123);
    write_every_window(map, 0xFF80, 0xFFFF, 123);
}

TEST(Registry, write_without_data_touches_no_register) {
    RegisterMap map;
    add_mixed_layout(map);

    for (std::uint32_t address = 0x0008; address <= 0x0028; ++address) {
        map.registry.on_write(address, {});
    }

    EXPECT_EQ(map.stored(), map.expected());
    EXPECT_EQ(map.write_callbacks, 0);
}

TEST(Registry, write_rejects_odd_data_size) {
    RegisterMap map;
    add_mixed_layout(map);

    EXPECT_THROW(map.registry.on_write(0x0010, {0x01}), std::runtime_error);
    EXPECT_THROW(map.registry.on_write(0x0011, {0x01, 0x02, 0x03}), std::runtime_error);
    EXPECT_EQ(map.stored(), map.expected());
    EXPECT_EQ(map.write_callbacks, 0);
}

TEST(Registry, read_ignores_register_data_beyond_its_size) {
    auto subregistry = std::make_shared<ComplexRegisterSubregistry>();
    subregistry->add(new FixedDataRegister(0x0010, 1, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}));

    ComplexRegisterRegistry registry;
    registry.add(subregistry);

    EXPECT_EQ(registry.on_read(0x0010, 3), (std::vector<std::uint8_t>{0x01, 0x02, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(registry.on_read(0x000F, 2), (std::vector<std::uint8_t>{0x00, 0x00, 0x01, 0x02}));
}

TEST(Registry, read_zero_fills_register_data_shorter_than_its_size) {
    auto subregistry = std::make_shared<ComplexRegisterSubregistry>();
    subregistry->add(new FixedDataRegister(0x0010, 4, {0x01, 0x02, 0x03}));
    subregistry->add(new FixedDataRegister(0x0014, 2, {}));

    ComplexRegisterRegistry registry;
    registry.add(subregistry);

    EXPECT_EQ(registry.on_read(0x0010, 4), (std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(registry.on_read(0x0011, 3), (std::vector<std::uint8_t>{0x03, 0x00, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(registry.on_read(0x0012, 4), (std::vector<std::uint8_t>(8, 0x00)));
}

TEST(Registry, read_of_overlapping_registers_keeps_first_registered) {
    auto first = std::make_shared<ComplexRegisterSubregistry>();
    first->add(new FixedDataRegister(0x0011, 1, {0x11, 0x12}));
    auto second = std::make_shared<ComplexRegisterSubregistry>();
    second->add(new FixedDataRegister(0x0010, 3, {0x21, 0x22, 0x23, 0x24, 0x25, 0x26}));

    ComplexRegisterRegistry registry;
    registry.add(first);
    registry.add(second);

    EXPECT_EQ(registry.on_read(0x0010, 3), (std::vector<std::uint8_t>{0x21, 0x22, 0x11, 0x12, 0x25, 0x26}));
}
