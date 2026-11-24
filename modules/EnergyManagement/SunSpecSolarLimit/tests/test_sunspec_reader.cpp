// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <sunspec/sunspec_reader.hpp>

#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace {

using sunspec_solar::DiscoveryError;
using sunspec_solar::ProductionSource;

class FakeDevice {
public:
    void set(std::uint16_t address, std::uint16_t value) {
        registers[address] = value;
    }

    // Builds a model chain at `base` from (model id, length) pairs.
    // Returns the start address of each model.
    std::vector<std::uint16_t> chain(std::uint16_t base,
                                     const std::vector<std::pair<std::uint16_t, std::uint16_t>>& models) {
        set(base, 0x5375);
        set(base + 1, 0x6E53);
        std::vector<std::uint16_t> starts;
        std::uint16_t address = base + 2;
        for (const auto& [id, length] : models) {
            starts.push_back(address);
            set(address, id);
            set(address + 1, length);
            for (std::uint16_t i = 0; i < length; ++i) {
                set(address + 2 + i, 0);
            }
            address += 2 + length;
        }
        set(address, 0xFFFF);
        set(address + 1, 0);
        return starts;
    }

    sunspec_solar::ReadRegisters reader() const {
        return [this](std::uint16_t address, std::uint16_t count) {
            std::vector<std::uint16_t> out;
            for (std::uint32_t a = address; a < static_cast<std::uint32_t>(address) + count; ++a) {
                const auto it = registers.find(static_cast<std::uint16_t>(a));
                if (it == registers.end()) {
                    throw std::runtime_error("illegal data address");
                }
                out.push_back(it->second);
            }
            return out;
        };
    }

private:
    std::map<std::uint16_t, std::uint16_t> registers;
};

std::uint16_t s16(int value) {
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
}

TEST(SunSpecDiscovery, FindsModel103AtBase40000) {
    FakeDevice device;
    const auto starts = device.chain(40000, {{1, 66}, {103, 50}});

    const auto source = sunspec_solar::discover(device.reader());

    EXPECT_EQ(source.model_id, 103);
    EXPECT_EQ(source.w_address, starts.at(1) + 14);
    EXPECT_EQ(source.w_sf_address, starts.at(1) + 15);
}

TEST(SunSpecDiscovery, FindsModel101And102) {
    for (const std::uint16_t id : {101, 102}) {
        FakeDevice device;
        device.chain(40000, {{1, 66}, {id, 50}});

        EXPECT_EQ(sunspec_solar::discover(device.reader()).model_id, id);
    }
}

TEST(SunSpecDiscovery, FindsModel701WithItsOffsets) {
    FakeDevice device;
    const auto starts = device.chain(40000, {{1, 66}, {701, 153}});

    const auto source = sunspec_solar::discover(device.reader());

    EXPECT_EQ(source.model_id, 701);
    EXPECT_EQ(source.w_address, starts.at(1) + 10);
    EXPECT_EQ(source.w_sf_address, starts.at(1) + 116);
}

TEST(SunSpecDiscovery, PrefersModel701OverInverterModels) {
    FakeDevice device;
    device.chain(40000, {{1, 66}, {103, 50}, {701, 153}});

    EXPECT_EQ(sunspec_solar::discover(device.reader()).model_id, 701);
}

TEST(SunSpecDiscovery, TriesBaseAddressesZeroAnd50000) {
    FakeDevice at_zero;
    at_zero.chain(0, {{1, 66}, {103, 50}});
    EXPECT_EQ(sunspec_solar::discover(at_zero.reader()).w_address, 2 + 68 + 14);

    FakeDevice at_50000;
    at_50000.chain(50000, {{1, 66}, {103, 50}});
    EXPECT_EQ(sunspec_solar::discover(at_50000.reader()).w_address, 50000 + 2 + 68 + 14);
}

TEST(SunSpecDiscovery, SkipsModelTooShortForItsPoints) {
    FakeDevice device;
    device.chain(40000, {{1, 66}, {701, 20}, {103, 50}});

    EXPECT_EQ(sunspec_solar::discover(device.reader()).model_id, 103);
}

TEST(SunSpecDiscovery, FailsWithoutMarker) {
    FakeDevice device;
    device.set(40000, 0x1234);
    device.set(40001, 0x5678);

    EXPECT_THROW(sunspec_solar::discover(device.reader()), DiscoveryError);
}

TEST(SunSpecDiscovery, FailsWithoutSupportedModel) {
    FakeDevice device;
    device.chain(40000, {{1, 66}, {201, 105}});

    EXPECT_THROW(sunspec_solar::discover(device.reader()), DiscoveryError);
}

TEST(SunSpecDiscovery, FailsOnChainWithoutEndMarker) {
    FakeDevice device;
    device.chain(40000, {{1, 66}});
    // Overwrite the end marker with a model that runs past the register space.
    device.set(40070, 64000);
    device.set(40071, 30000);

    EXPECT_THROW(sunspec_solar::discover(device.reader()), DiscoveryError);
}

TEST(SunSpecProduction, AppliesScaleFactor) {
    FakeDevice device;
    device.set(100, 32000);
    device.set(101, s16(-1));
    const ProductionSource source{103, 100, 101};

    EXPECT_DOUBLE_EQ(sunspec_solar::read_production_W(device.reader(), source).value(), 3200.0);
}

TEST(SunSpecProduction, KeepsSignOfNegativePower) {
    FakeDevice device;
    device.set(100, s16(-150));
    device.set(101, 0);
    const ProductionSource source{103, 100, 101};

    EXPECT_DOUBLE_EQ(sunspec_solar::read_production_W(device.reader(), source).value(), -150.0);
}

TEST(SunSpecProduction, UnimplementedValuesAreInvalid) {
    const ProductionSource source{103, 100, 101};

    FakeDevice no_power;
    no_power.set(100, 0x8000);
    no_power.set(101, 0);
    EXPECT_FALSE(sunspec_solar::read_production_W(no_power.reader(), source).has_value());

    FakeDevice no_scale;
    no_scale.set(100, 5000);
    no_scale.set(101, 0x8000);
    EXPECT_FALSE(sunspec_solar::read_production_W(no_scale.reader(), source).has_value());
}

TEST(SunSpecProduction, ImplausibleScaleFactorIsInvalid) {
    FakeDevice device;
    device.set(100, 5000);
    device.set(101, 20);
    const ProductionSource source{103, 100, 101};

    EXPECT_FALSE(sunspec_solar::read_production_W(device.reader(), source).has_value());
}

} // namespace
