// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charging_station_base.cpp
/// \brief Lifetime tests for ChargingStationBase.

#include <chrono>
#include <future>
#include <memory>

#include <boost/asio/post.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/common/charging_station_base.hpp>

#include "evse_security_mock.hpp"

namespace ocpp {

namespace {
constexpr auto WAIT_TIMEOUT = std::chrono::seconds(5);
constexpr int ITERATIONS = 200;
} // namespace

class TestChargingStation : public ChargingStationBase {
public:
    using ChargingStationBase::ChargingStationBase;

    /// \brief Run \p handler on the io_context thread and wait for it.
    bool run_on_io_context(const std::function<void()>& handler) {
        std::promise<void> done;
        boost::asio::post(this->io_context, [&]() {
            handler();
            done.set_value();
        });
        return done.get_future().wait_for(WAIT_TIMEOUT) == std::future_status::ready;
    }
};

// The io_context, its work guard and its thread are torn down in the destructor. The work guard references the
// io_context, so it has to be released before the io_context is destroyed.
TEST(ChargingStationBaseTest, RepeatedConstructionAndDestructionIsClean) {
    const auto evse_security = std::make_shared<::testing::NiceMock<EvseSecurityMock>>();

    for (int i = 0; i < ITERATIONS; ++i) {
        TestChargingStation station(evse_security);
        ASSERT_TRUE(station.run_on_io_context([]() {})) << "io_context thread is not running in iteration " << i;
    }
}

} // namespace ocpp
