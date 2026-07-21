// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelConcurrencyTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* dm;
    const RequiredComponentVariable cv = ControllerComponentVariables::AlignedDataInterval;
    std::atomic<std::uint64_t> listener_calls{0};

    DeviceModelConcurrencyTest() : device_model_test_helper(), dm(device_model_test_helper.get_device_model()) {
    }
};

/// \brief Hammer overlapping public methods from multiple threads and assert no crash / data race and a consistent
/// final read.
TEST_F(DeviceModelConcurrencyTest, concurrent_access_no_data_race) {
    const Component component = cv.component;
    const Variable variable = cv.variable.value();

    // Register a listener up front so set_value / clear_value take the copy-listeners-and-notify path,
    // exercising it against concurrent re-registration.
    dm->register_variable_listener([this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                          const Component&, const Variable&, const VariableCharacteristics&,
                                          const VariableAttribute&, const std::string&,
                                          const std::string&) { this->listener_calls.fetch_add(1); });

    constexpr int num_threads = 6;
    constexpr int iterations = 500;

    auto worker = [&](int seed) {
        for (int i = 0; i < iterations; ++i) {
            switch ((seed + i) % 9) {
            case 0: {
                // Locked public read entry point.
                std::string value;
                dm->get_variable(component, variable, AttributeEnum::Actual, value);
                break;
            }
            case 1: {
                dm->set_value(component, variable, AttributeEnum::Actual, std::to_string((i % 800) + 1), "test");
                break;
            }
            case 2: {
                SetMonitoringData request;
                request.value = 0.0;
                request.type = MonitorEnum::PeriodicClockAligned;
                request.severity = 7;
                request.component = component;
                request.variable = variable;
                dm->set_monitors({request});
                break;
            }
            case 3: {
                dm->clear_custom_monitors();
                break;
            }
            case 4: {
                // A concurrent clear_value (case 7) may leave the Actual value absent, so tolerate the throw.
                try {
                    (void)dm->get_value<int>(component, variable, AttributeEnum::Actual);
                } catch (const std::exception&) {
                }
                break;
            }
            case 5: {
                (void)dm->get_optional_value<int>(component, variable, AttributeEnum::Actual);
                break;
            }
            case 6: {
                // Concurrent listener re-registration mutating variable_listener under the lock.
                dm->register_variable_listener([this](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                                      const Component&, const Variable&, const VariableCharacteristics&,
                                                      const VariableAttribute&, const std::string&,
                                                      const std::string&) { this->listener_calls.fetch_add(1); });
                break;
            }
            case 7: {
                dm->clear_value(component, variable, AttributeEnum::Actual, "test");
                break;
            }
            case 8: {
                // Wrapper path: delegates through set_read_only_value into set_value (which locks).
                dm->set_security_ctrl_security_profile(1, "test");
                break;
            }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& thread : threads) {
        thread.join();
    }

    // Deterministic final-state assertion: set a known value and read it back through both the public entry point
    // and the template read path.
    const auto set_result = dm->set_value(component, variable, AttributeEnum::Actual, "123", "test");
    ASSERT_EQ(set_result, SetVariableStatusEnum::Accepted);

    std::string value;
    const auto get_result = dm->get_variable(component, variable, AttributeEnum::Actual, value);
    ASSERT_EQ(get_result, GetVariableStatusEnum::Accepted);
    ASSERT_EQ(value, "123");
    ASSERT_EQ(dm->get_value<int>(component, variable, AttributeEnum::Actual), 123);
}

/// \brief Listeners must be invoked without the DeviceModel mutex held. Regression test for a lock-order
/// inversion with consumer locks (e.g. OCPP201's monitor_list_mutex): a listener waits on a resource guarded
/// by a lock whose holder concurrently calls register_variable_listener. If set_value still owned the mutex
/// while notifying, the helper below could never register and the bounded wait would time out.
TEST_F(DeviceModelConcurrencyTest, listener_invoked_without_device_model_lock) {
    const Component component = cv.component;
    const Variable variable = cv.variable.value();

    std::mutex m;
    std::condition_variable cond;
    bool in_listener = false;
    bool registration_completed = false;
    std::atomic<bool> listener_saw_registration{false};

    std::thread helper([&] {
        {
            std::unique_lock<std::mutex> lk(m);
            cond.wait(lk, [&] { return in_listener; });
        }
        // Blocks forever if the notifying thread still holds the DeviceModel mutex.
        dm->register_variable_listener([](auto&&...) {});
        {
            std::lock_guard<std::mutex> lk(m);
            registration_completed = true;
        }
        cond.notify_all();
    });

    dm->register_variable_listener([&](const std::unordered_map<std::int64_t, VariableMonitoringMeta>&,
                                       const Component&, const Variable&, const VariableCharacteristics&,
                                       const VariableAttribute&, const std::string&, const std::string&) {
        std::unique_lock<std::mutex> lk(m);
        in_listener = true;
        cond.notify_all();
        if (cond.wait_for(lk, std::chrono::seconds(10), [&] { return registration_completed; })) {
            listener_saw_registration = true;
        }
    });

    // Two writes with distinct values so at least one differs from the stored value and fires the listener.
    dm->set_value(component, variable, AttributeEnum::Actual, "41", "test");
    dm->set_value(component, variable, AttributeEnum::Actual, "42", "test");

    helper.join();
    EXPECT_TRUE(listener_saw_registration);
}

} // namespace v2
} // namespace ocpp
