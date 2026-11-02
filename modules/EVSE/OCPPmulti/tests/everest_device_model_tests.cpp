// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

#include <everest/ocpp_module_common/device_model/everest_device_model.hpp>

#include "stubs/interfaces_stub.hpp"

namespace {

using ocpp_module_common::device_model::EverestDeviceModel;

class FakeRegistry : public ocpp::v2::DeviceModelRegistryInterface {
public:
    ocpp::v2::SetVariableStatusEnum status{ocpp::v2::SetVariableStatusEnum::Accepted};
    /// A report of this value waits until release() is called before the registry takes it
    std::optional<std::string> blocking_value;

    ocpp::v2::ComponentRegistrationResult
    register_components(const std::string&, const std::vector<ocpp::v2::RegisteredComponent>&,
                        std::shared_ptr<ocpp::v2::VariableValueProvider>) override {
        return {};
    }

    void unregister_components(const std::string&) override {
    }

    ocpp::v2::SetVariableStatusEnum notify_value_changed(const ocpp::v2::Component&, const ocpp::v2::Variable&,
                                                         ocpp::v2::AttributeEnum, const std::string& value,
                                                         const std::string&,
                                                         std::optional<ocpp::v2::WriteId>) override {
        std::unique_lock lock(this->mutex);
        if (this->blocking_value == value) {
            this->blocked = true;
            this->changed.notify_all();
            this->changed.wait(lock, [this]() { return this->released; });
        }
        this->reported.push_back(value);
        return this->status;
    }

    void wait_until_blocked() {
        std::unique_lock lock(this->mutex);
        this->changed.wait(lock, [this]() { return this->blocked; });
    }

    void release() {
        std::lock_guard lock(this->mutex);
        this->released = true;
        this->changed.notify_all();
    }

    std::vector<std::string> reports() {
        std::lock_guard lock(this->mutex);
        return this->reported;
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> reported;
    bool blocked{false};
    bool released{false};
};

std::optional<std::string> vehicle_id_of(EverestDeviceModel& everest_device_model) {
    for (const auto& component : everest_device_model.get_components()) {
        if (component.component.name.get() != "ConnectedEV") {
            continue;
        }
        for (const auto& variable : component.variables) {
            if (variable.variable == ocpp::v2::ConnectedEvComponentVariables::VehicleId) {
                const auto& value = variable.attributes.front().value;
                return value.has_value() ? std::optional<std::string>{value->get()} : std::nullopt;
            }
        }
    }
    return std::nullopt;
}

class EverestDeviceModelReportsTest : public ::testing::Test {
protected:
    stubs::ModuleInterfaces interfaces;
    std::unique_ptr<EverestDeviceModel> everest_device_model;
    FakeRegistry registry;

    void SetUp() override {
        this->interfaces.add_evse_manager("evse_manager_1");
        this->everest_device_model =
            std::make_unique<EverestDeviceModel>(this->interfaces.r_evse_manager, this->interfaces.r_extensions_15118,
                                                 std::map<int32_t, types::evse_board_support::HardwareCapabilities>{},
                                                 std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>{},
                                                 std::map<int32_t, bool>{}, false, std::set<int32_t>{});
        this->everest_device_model->register_with(this->registry);
    }
};

} // namespace

TEST_F(EverestDeviceModelReportsTest, accepted_value_is_kept) {
    this->everest_device_model->update_connected_ev_vehicle_id(1, "accepted");

    EXPECT_EQ(vehicle_id_of(*this->everest_device_model), "accepted");
}

TEST_F(EverestDeviceModelReportsTest, rejected_value_is_not_kept) {
    this->registry.status = ocpp::v2::SetVariableStatusEnum::Rejected;

    this->everest_device_model->update_connected_ev_vehicle_id(1, "rejected");

    EXPECT_EQ(vehicle_id_of(*this->everest_device_model), "");
}

TEST_F(EverestDeviceModelReportsTest, concurrent_reports_reach_the_registry_in_order) {
    this->registry.blocking_value = "first";

    std::thread first([this]() { this->everest_device_model->update_connected_ev_vehicle_id(1, "first"); });
    this->registry.wait_until_blocked();
    std::thread second([this]() { this->everest_device_model->update_connected_ev_vehicle_id(1, "second"); });
    // Gives an unordered second report the chance to overtake the first
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    this->registry.release();
    first.join();
    second.join();

    EXPECT_EQ(this->registry.reports(), (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(vehicle_id_of(*this->everest_device_model), "second");
}

TEST_F(EverestDeviceModelReportsTest, report_in_progress_does_not_delay_other_variables) {
    this->registry.blocking_value = "first";

    std::thread first([this]() { this->everest_device_model->update_connected_ev_vehicle_id(1, "first"); });
    this->registry.wait_until_blocked();
    auto other = std::async(std::launch::async, [this]() {
        this->everest_device_model->update_power(1, 11000.0F);
        return this->everest_device_model->get_components().size();
    });
    const auto other_status = other.wait_for(std::chrono::seconds(5));
    this->registry.release();
    first.join();

    ASSERT_EQ(other_status, std::future_status::ready);
    EXPECT_GT(other.get(), 0u);
    EXPECT_EQ(this->registry.reports(), (std::vector<std::string>{std::to_string(11000.0F), "first"}));
}
