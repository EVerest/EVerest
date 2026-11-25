// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>
#include <everest/ocpp_module_common/device_model/device_model_registrations.hpp>
#include <everest/ocpp_module_common/device_model/external_owners.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

#include "unique_directory.hpp"

namespace {

namespace dm = ocpp_module_common::device_model;
namespace dm_types = types::ocpp_device_model;
using ocpp::v2::AttributeEnum;
using ocpp::v2::GetVariableStatusEnum;
using ocpp::v2::SetVariableStatusEnum;

class FakeConnection : public dm::DeviceModelOwnerConnection {
public:
    explicit FakeConnection(std::string id) : id(std::move(id)) {
    }

    std::string module_id() const override {
        return this->id;
    }

    void set_registration_result(const dm_types::RegistrationResult& result) override {
        this->results.push_back(result);
        if (this->on_result) {
            auto hook = std::move(this->on_result);
            this->on_result = nullptr;
            hook();
        }
    }

    types::ocpp::SetVariableStatusEnumType set_value(const dm_types::WriteRequest& request,
                                                     const std::string& request_id) override {
        if (this->gate.has_value()) {
            this->entered.set_value();
            this->gate->wait();
        }
        dm_types::OwnerMessage message;
        message.write_result = dm_types::WriteResult{request_id, request, this->reply};
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->writes.push_back(request);
            if (this->hold_results) {
                this->held_results.push_back(message);
                return this->reply;
            }
        }
        // Published before the answer, as the module providing the interface does
        this->publish(message);
        return this->reply;
    }

    std::size_t write_count() {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->writes.size();
    }

    /// \brief Publishes the write results held so far, in order
    void release_results() {
        std::vector<dm_types::OwnerMessage> held;
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            held = std::exchange(this->held_results, {});
        }
        for (const auto& message : held) {
            this->publish(message);
        }
    }

    std::string id;
    std::vector<dm_types::RegistrationResult> results;
    std::mutex mutex;
    std::vector<dm_types::WriteRequest> writes;
    /// \brief Publishes a message of this connection, as the framework delivers it
    std::function<void(const dm_types::OwnerMessage&)> publish;
    /// \brief When set, write results are kept in held_results instead of being published, as if the owner sent
    /// other messages before it answered
    bool hold_results{false};
    std::vector<dm_types::OwnerMessage> held_results;
    types::ocpp::SetVariableStatusEnumType reply{types::ocpp::SetVariableStatusEnumType::Accepted};
    /// \brief When set, a write waits for it after signalling entered
    std::optional<std::shared_future<void>> gate;
    std::promise<void> entered;
    /// \brief Called once after the next result, as if a registration arrived at that moment
    std::function<void()> on_result;
};

/// \brief Passes everything on to a device model, and can fail the next registration, hold value reports and record
/// what is registered
class WrappingRegistry : public ocpp::v2::DeviceModelRegistryInterface {
public:
    explicit WrappingRegistry(ocpp::v2::DeviceModelRegistryInterface& registry) : registry(registry) {
    }

    ocpp::v2::ComponentRegistrationResult
    register_components(const std::string& owner, const std::vector<ocpp::v2::RegisteredComponent>& components,
                        std::shared_ptr<ocpp::v2::VariableValueProvider> provider) override {
        if (this->fail_next_registration) {
            this->fail_next_registration = false;
            throw std::runtime_error("storage failed");
        }
        if (this->register_gate.has_value()) {
            this->register_entered.set_value();
            this->register_gate->wait();
        }
        this->registered = components;
        this->providers.push_back(provider);
        return this->registry.register_components(owner, components, provider);
    }
    void unregister_components(const std::string& owner) override {
        this->registry.unregister_components(owner);
    }
    SetVariableStatusEnum notify_value_changed(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                               AttributeEnum attribute, const std::string& value,
                                               const std::string& owner) override {
        if (this->gate.has_value()) {
            this->entered.set_value();
            this->gate->wait();
        }
        return this->registry.notify_value_changed(component, variable, attribute, value, owner);
    }

    ocpp::v2::DeviceModelRegistryInterface& registry;
    bool fail_next_registration{false};
    std::optional<std::shared_future<void>> register_gate;
    std::promise<void> register_entered;
    std::vector<ocpp::v2::RegisteredComponent> registered;
    std::vector<std::shared_ptr<ocpp::v2::VariableValueProvider>> providers;
    std::optional<std::shared_future<void>> gate;
    std::promise<void> entered;
};

/// \brief A registry whose every operation fails, like a device model whose storage fails
class FailingRegistry : public ocpp::v2::DeviceModelRegistryInterface {
public:
    ocpp::v2::ComponentRegistrationResult
    register_components(const std::string& /*owner*/, const std::vector<ocpp::v2::RegisteredComponent>& /*components*/,
                        std::shared_ptr<ocpp::v2::VariableValueProvider> /*provider*/) override {
        throw std::runtime_error("storage failed");
    }
    void unregister_components(const std::string& /*owner*/) override {
        throw std::runtime_error("storage failed");
    }
    SetVariableStatusEnum notify_value_changed(const ocpp::v2::Component& /*component*/,
                                               const ocpp::v2::Variable& /*variable*/, AttributeEnum /*attribute*/,
                                               const std::string& /*value*/, const std::string& /*owner*/) override {
        throw std::runtime_error("storage failed");
    }
};

ocpp::v2::Component make_component(const std::string& name) {
    ocpp::v2::Component component;
    component.name = name;
    return component;
}

ocpp::v2::Variable make_variable(const std::string& name) {
    ocpp::v2::Variable variable;
    variable.name = name;
    return variable;
}

dm_types::Registration
make_registration(const std::optional<std::string>& owner, const std::string& component, const std::string& variable,
                  const std::string& value,
                  const std::optional<dm_types::MutabilityEnum> mutability = dm_types::MutabilityEnum::ReadWrite) {
    dm_types::VariableAttribute attribute;
    attribute.value = value;
    attribute.mutability = mutability;

    dm_types::RegisteredVariable registered_variable;
    registered_variable.variable.name = variable;
    registered_variable.characteristics.data_type = dm_types::DataEnum::integer;
    registered_variable.characteristics.supports_monitoring = true;
    registered_variable.attributes.push_back(attribute);

    dm_types::RegisteredComponent registered_component;
    registered_component.component.name = component;
    registered_component.variables.push_back(registered_variable);

    dm_types::Registration registration;
    registration.owner = owner;
    registration.components.push_back(registered_component);
    return registration;
}

dm_types::Registration make_release(const std::optional<std::string>& owner) {
    dm_types::Registration registration;
    registration.owner = owner;
    return registration;
}

dm_types::Registrations make_registrations(std::vector<dm_types::Registration> registrations) {
    dm_types::Registrations result;
    result.registrations = std::move(registrations);
    return result;
}

dm_types::ValueChange make_change(const std::optional<std::string>& owner, const std::string& component,
                                  const std::string& variable, const std::string& value) {
    dm_types::ValueChange change;
    change.owner = owner;
    change.component.name = component;
    change.variable.name = variable;
    change.value = value;
    return change;
}

/// \brief Binds every declared key unless it is in refused, and records the keys each owner holds
class FakeKeyBinder : public dm::Ocpp16KeyBinder {
public:
    std::vector<ocpp::v2::RejectedVariable> bind(const std::string& owner,
                                                 const std::vector<ocpp::v2::RegisteredComponent>& /*components*/,
                                                 const std::vector<dm::DeclaredOcpp16Key>& keys) override {
        this->bound.erase(owner);
        std::vector<ocpp::v2::RejectedVariable> rejected;
        for (const auto& key : keys) {
            if (this->refused.count(key.key) != 0) {
                rejected.push_back(
                    {key.component, key.variable, ocpp::v2::RegistrationRejectionReason::Ocpp16KeyInUse, {}});
            } else {
                this->bound[owner][key.variable.name.get()] = key.key;
            }
        }
        return rejected;
    }
    void unbind_variables(const std::string& owner, const std::vector<ocpp::v2::RejectedVariable>& rejected) override {
        for (const auto& variable : rejected) {
            this->bound[owner].erase(variable.variable.name.get());
        }
    }
    void unbind(const std::string& owner) override {
        this->bound.erase(owner);
    }

    std::set<std::string> refused;
    /// \brief Per owner, the key of each variable by variable name
    std::map<std::string, std::map<std::string, std::string>> bound;
};

class ExternalOwnersTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->work_dir = test_support::create_unique_directory("ocpp_module_common_external_owners");
        const auto db_path = this->work_dir / "device_model.db";
        {
            ocpp::v2::InitDeviceModelDb init_db(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
            init_db.initialize_database({}, true, false);
            init_db.close_connection();
        }
        this->device_model =
            std::make_unique<ocpp::v2::DeviceModel>(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(db_path));
        this->first = std::make_shared<FakeConnection>("api_1");
        this->second = std::make_shared<FakeConnection>("api_2");
        this->owners = std::make_unique<dm::ExternalOwners>(
            std::vector<std::shared_ptr<dm::DeviceModelOwnerConnection>>{this->first, this->second});
        this->first->publish = [this](const dm_types::OwnerMessage& message) {
            this->owners->on_owner_message(0, message);
        };
        this->second->publish = [this](const dm_types::OwnerMessage& message) {
            this->owners->on_owner_message(1, message);
        };
    }

    void TearDown() override {
        this->owners.reset();
        this->device_model.reset();
        std::filesystem::remove_all(this->work_dir);
    }

    void start(const dm_types::Registrations& first_answer, const dm_types::Registrations& second_answer = {}) {
        this->owners->set_startup_registrations(0, first_answer);
        this->owners->set_startup_registrations(1, second_answer);
        this->results = this->owners->register_with(*this->device_model);
    }

    std::optional<std::string> read(const std::string& component, const std::string& variable) {
        std::string value;
        if (this->device_model->get_variable(make_component(component), make_variable(variable), AttributeEnum::Actual,
                                             value, false,
                                             ocpp::v2::UnsetValue::NotSupported) != GetVariableStatusEnum::Accepted) {
            return std::nullopt;
        }
        return value;
    }

    SetVariableStatusEnum write(const std::string& component, const std::string& variable, const std::string& value) {
        return this->device_model->set_value(make_component(component), make_variable(variable), AttributeEnum::Actual,
                                             value, "csms");
    }

    std::filesystem::path work_dir;
    std::unique_ptr<ocpp::v2::DeviceModel> device_model;
    std::shared_ptr<FakeConnection> first;
    std::shared_ptr<FakeConnection> second;
    // declared before owners, which uses it until destroyed
    FakeKeyBinder binder;
    std::unique_ptr<dm::ExternalOwners> owners;
    std::vector<dm::OwnerRegistrationResult> results;
};

} // namespace

TEST_F(ExternalOwnersTest, RegistrationWithoutOwnerBelongsToTheModule) {
    this->start(make_registrations({make_registration(std::nullopt, "CoolingCtrlr", "FanSpeed", "40")}));

    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "40");
    ASSERT_EQ(this->results.size(), 1u);
    EXPECT_EQ(this->results.at(0).owner, "api_1");
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).owner, "api_1");
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_TRUE(this->first->results.at(0).rejected.empty());
}

TEST_F(ExternalOwnersTest, WriteReachesTheOwningConnection) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));

    EXPECT_EQ(this->write("CoolingCtrlr", "FanSpeed", "60"), SetVariableStatusEnum::Accepted);
    ASSERT_EQ(this->second->writes.size(), 1u);
    EXPECT_TRUE(this->first->writes.empty());
    const auto& request = this->second->writes.at(0);
    EXPECT_EQ(request.owner, "cooling");
    EXPECT_EQ(request.component.name, "CoolingCtrlr");
    EXPECT_EQ(request.variable.name, "FanSpeed");
    EXPECT_EQ(request.attribute_type, types::ocpp::AttributeEnum::Actual);
    EXPECT_EQ(request.value, "60");
    EXPECT_EQ(request.source, "csms");
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");

    this->second->reply = types::ocpp::SetVariableStatusEnumType::Rejected;
    EXPECT_EQ(this->write("CoolingCtrlr", "FanSpeed", "70"), SetVariableStatusEnum::Rejected);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");

    this->second->reply = types::ocpp::SetVariableStatusEnumType::RebootRequired;
    EXPECT_EQ(this->write("CoolingCtrlr", "FanSpeed", "80"), SetVariableStatusEnum::RebootRequired);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");
}

TEST_F(ExternalOwnersTest, AttributeWithoutMutabilityIsReadOnly) {
    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5", std::nullopt)}));

    EXPECT_EQ(this->write("AuxMeter", "Energy", "6"), SetVariableStatusEnum::Rejected);
    EXPECT_TRUE(this->first->writes.empty());
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, OwnerOfAnotherConnectionIsInUse) {
    this->start(make_registrations({make_registration("shared", "CoolingCtrlr", "FanSpeed", "40")}),
                make_registrations({make_registration("shared", "OtherCtrlr", "Level", "1")}));

    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "40");
    EXPECT_EQ(this->read("OtherCtrlr", "Level"), std::nullopt);
    ASSERT_EQ(this->second->results.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).owner, "shared");
    EXPECT_EQ(this->second->results.at(0).status, dm_types::RegistrationStatus::OwnerInUse);
}

TEST_F(ExternalOwnersTest, OwnerRegisteredTwiceInOneAnswerIsInUse) {
    this->start(make_registrations(
        {make_registration("meter", "AuxMeter", "Energy", "5"), make_registration("meter", "AuxMeter", "Power", "1")}));

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
    EXPECT_EQ(this->read("AuxMeter", "Power"), std::nullopt);
    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::OwnerInUse);
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Accepted);
}

TEST_F(ExternalOwnersTest, OwnersOfTheOcppModuleAreInUse) {
    this->start(make_registrations(
        {make_registration(std::string{dm::VARIABLE_OWNER_EVEREST}, "AuxMeter", "Energy", "5"),
         make_registration(std::string{dm::VARIABLE_OWNER_EVEREST_CONFIG}, "AuxMeter", "Power", "1")}));

    EXPECT_TRUE(this->results.empty());
    ASSERT_EQ(this->first->results.size(), 2u);
    for (const auto& result : this->first->results) {
        EXPECT_EQ(result.status, dm_types::RegistrationStatus::OwnerInUse);
    }
}

TEST_F(ExternalOwnersTest, ReleasedOwnerCanBeTakenByAnotherConnection) {
    this->start(make_registrations({make_registration("shared", "CoolingCtrlr", "FanSpeed", "40")}));

    this->owners->on_registration(1, make_registration("shared", "OtherCtrlr", "Level", "1"));
    ASSERT_EQ(this->second->results.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).status, dm_types::RegistrationStatus::OwnerInUse);

    this->owners->on_registration(0, make_release("shared"));
    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), std::nullopt);

    this->owners->on_registration(1, make_registration("shared", "OtherCtrlr", "Level", "1"));
    ASSERT_EQ(this->second->results.size(), 2u);
    EXPECT_EQ(this->second->results.at(1).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_EQ(this->read("OtherCtrlr", "Level"), "1");

    EXPECT_EQ(this->write("OtherCtrlr", "Level", "2"), SetVariableStatusEnum::Accepted);
    EXPECT_TRUE(this->first->writes.empty());
    ASSERT_EQ(this->second->writes.size(), 1u);
}

TEST_F(ExternalOwnersTest, RegistrationAfterStartReplacesTheOwnersVariables) {
    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));

    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Power", "7"));
    EXPECT_EQ(this->read("AuxMeter", "Energy"), std::nullopt);
    EXPECT_EQ(this->read("AuxMeter", "Power"), "7");
    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Accepted);
}

TEST_F(ExternalOwnersTest, RejectedVariablesAreReported) {
    auto registration = make_registration("meter", "AuxMeter", "Energy", "5");
    auto no_attributes = registration.components.at(0).variables.at(0);
    no_attributes.variable.name = "Empty";
    no_attributes.attributes.clear();
    registration.components.at(0).variables.push_back(no_attributes);
    this->start(make_registrations({registration}));

    ASSERT_EQ(this->first->results.size(), 1u);
    const auto& result = this->first->results.at(0);
    EXPECT_EQ(result.status, dm_types::RegistrationStatus::Accepted);
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.at(0).variable.name, "Empty");
    EXPECT_EQ(result.rejected.at(0).reason, dm_types::RegistrationRejectionReason::NoAttributes);
    EXPECT_FALSE(result.rejected.at(0).defined_by.has_value());
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, VariableOfAnotherOwnerIsAlreadyDefined) {
    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}),
                make_registrations({make_registration("other", "AuxMeter", "Energy", "9")}));

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
    ASSERT_EQ(this->second->results.size(), 1u);
    ASSERT_EQ(this->second->results.at(0).rejected.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).rejected.at(0).reason, dm_types::RegistrationRejectionReason::AlreadyDefined);
    EXPECT_EQ(this->second->results.at(0).rejected.at(0).defined_by, "meter");
}

TEST_F(ExternalOwnersTest, RegistrationPublishedBeforeTheStartupAnswerIsNewer) {
    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "6"));
    EXPECT_TRUE(this->first->results.empty());

    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Accepted);
}

TEST_F(ExternalOwnersTest, ConnectionWithoutStartupAnswerHasItsLiveRegistrationsApplied) {
    this->owners->on_registration(1, make_registration("late", "AuxMeter", "Energy", "3"));
    this->owners->set_startup_registrations(0, {});
    this->owners->register_with(*this->device_model);

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "3");
    ASSERT_EQ(this->second->results.size(), 1u);
}

TEST_F(ExternalOwnersTest, ValueChangeOfTheOwnerIsReported) {
    this->start(make_registrations({make_registration(std::nullopt, "AuxMeter", "Energy", "5")}));

    this->owners->on_value_changed(0, make_change(std::nullopt, "AuxMeter", "Energy", "8"));
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "8");

    // Not an integer
    this->owners->on_value_changed(0, make_change(std::nullopt, "AuxMeter", "Energy", "eight"));
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "8");
}

TEST_F(ExternalOwnersTest, ValueChangeForAnOwnerOfAnotherConnectionIsIgnored) {
    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));

    this->owners->on_value_changed(1, make_change("meter", "AuxMeter", "Energy", "8"));
    this->owners->on_value_changed(0, make_change(std::nullopt, "AuxMeter", "Energy", "9"));
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, ValueChangeBeforeTheStartIsRegistered) {
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "8"));
    this->owners->register_with(*this->device_model);

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "8");
}

TEST_F(ExternalOwnersTest, EndingTheRegistrationsDetachesTheOwners) {
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    {
        dm::DeviceModelRegistrations registrations;
        registrations.register_external_owners(*this->owners, *this->device_model);
        ASSERT_EQ(registrations.results().size(), 1u);
        EXPECT_EQ(registrations.results().at(0).owner, "meter");
        EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
    }
    EXPECT_EQ(this->read("AuxMeter", "Energy"), std::nullopt);

    // The owners are registered again with the next device model
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "8"));
    this->owners->register_with(*this->device_model);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "8");
}

TEST_F(ExternalOwnersTest, RegistrationWithVariablesThatDoNotFitOcppIsInvalid) {
    auto registration = make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40");
    auto long_unit = registration.components.at(0).variables.at(0);
    long_unit.variable.name = "Temperature";
    long_unit.characteristics.unit = std::string(17, 'C');
    registration.components.at(0).variables.push_back(long_unit);
    auto long_name = registration.components.at(0);
    long_name.component.name = std::string(51, 'L');
    registration.components.push_back(long_name);
    this->start(make_registrations({registration}));

    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), std::nullopt);
    EXPECT_EQ(this->read("CoolingCtrlr", "Temperature"), std::nullopt);
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Invalid);
    const auto& rejected = this->first->results.at(0).rejected;
    ASSERT_EQ(rejected.size(), 3u);
    EXPECT_EQ(rejected.at(0).variable.name, "Temperature");
    for (const auto& variable : rejected) {
        EXPECT_EQ(variable.reason, dm_types::RegistrationRejectionReason::InvalidNameKey);
    }

    this->owners->on_value_changed(0, make_change("cooling", "CoolingCtrlr", "FanSpeed", "50"));
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), std::nullopt);
}

TEST_F(ExternalOwnersTest, ValueThatDoesNotFitOcppIsIgnoredBeforeTheStart) {
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    EXPECT_NO_THROW(
        this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", std::string(2501, '5'))));
    this->owners->register_with(*this->device_model);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, FailingRegistryDoesNotEscape) {
    FailingRegistry registry;
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    std::vector<dm::OwnerRegistrationResult> results;
    EXPECT_NO_THROW(results = this->owners->register_with(registry));
    EXPECT_TRUE(results.empty());
    EXPECT_NO_THROW(this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Power", "1")));
    EXPECT_NO_THROW(this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Power", "2")));
    EXPECT_NO_THROW(this->owners->on_registration(0, make_release("meter")));
    EXPECT_NO_THROW(this->owners->detach());
}

TEST_F(ExternalOwnersTest, ReleaseWaitingForAWriteBlocksNoOtherConnection) {
    this->start(make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}),
                make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));

    std::promise<void> open;
    this->first->gate = open.get_future().share();
    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    this->first->entered.get_future().wait();

    // The release waits in libocpp for the write in progress
    auto release =
        std::async(std::launch::async, [this]() { this->owners->on_registration(0, make_release("cooling")); });
    EXPECT_EQ(release.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);

    // Meanwhile the other connection is served, and the name being released is still in use
    auto other = std::async(std::launch::async, [this]() {
        this->owners->on_value_changed(1, make_change("meter", "AuxMeter", "Energy", "6"));
        this->owners->on_registration(1, make_registration("cooling", "OtherCtrlr", "Level", "1"));
    });
    ASSERT_EQ(other.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");
    ASSERT_EQ(this->second->results.size(), 2u);
    EXPECT_EQ(this->second->results.at(1).status, dm_types::RegistrationStatus::OwnerInUse);

    open.set_value();
    EXPECT_EQ(write.get(), SetVariableStatusEnum::Accepted);
    release.get();
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), std::nullopt);
    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Accepted);

    this->owners->on_registration(1, make_registration("cooling", "OtherCtrlr", "Level", "1"));
    EXPECT_EQ(this->second->results.back().status, dm_types::RegistrationStatus::Accepted);
}

TEST_F(ExternalOwnersTest, FailedRegistrationFreesTheOwnerName) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->set_startup_registrations(1, {});
    registry.fail_next_registration = true;
    EXPECT_TRUE(this->owners->register_with(registry).empty());
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).owner, "meter");
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Rejected);

    this->owners->on_registration(1, make_registration("meter", "AuxMeter", "Energy", "7"));
    ASSERT_EQ(this->second->results.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "7");

    // At runtime as well
    registry.fail_next_registration = true;
    this->owners->on_registration(1, make_registration("meter", "AuxMeter", "Energy", "8"));
    ASSERT_EQ(this->second->results.size(), 2u);
    EXPECT_EQ(this->second->results.at(1).status, dm_types::RegistrationStatus::Rejected);
    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "9"));
    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Accepted);
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, ResultsArriveInTheOrderOfTheRegistrations) {
    // The startup registration of meter has a rejected variable, the one that arrives while the startup results are
    // sent has none. Its result is sent after the startup result of meter, so the last result matches the device model.
    auto startup = make_registration("meter", "AuxMeter", "Energy", "5");
    auto no_attributes = startup.components.at(0).variables.at(0);
    no_attributes.variable.name = "Empty";
    no_attributes.attributes.clear();
    startup.components.at(0).variables.push_back(no_attributes);
    this->first->on_result = [this]() {
        this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Power", "1"));
    };
    this->start(make_registrations({make_registration("a_cooling", "CoolingCtrlr", "FanSpeed", "40"), startup}));

    ASSERT_EQ(this->first->results.size(), 3u);
    EXPECT_EQ(this->first->results.at(0).owner, "a_cooling");
    EXPECT_EQ(this->first->results.at(1).owner, "meter");
    EXPECT_EQ(this->first->results.at(1).rejected.size(), 1u);
    EXPECT_EQ(this->first->results.at(2).owner, "meter");
    EXPECT_TRUE(this->first->results.at(2).rejected.empty());
    EXPECT_EQ(this->read("AuxMeter", "Power"), "1");
    EXPECT_EQ(this->read("AuxMeter", "Energy"), std::nullopt);
}

TEST_F(ExternalOwnersTest, ValueReportBlocksNoOtherConnection) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(registry);

    std::promise<void> open;
    registry.gate = open.get_future().share();
    auto report = std::async(std::launch::async, [this]() {
        this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "6"));
    });
    registry.entered.get_future().wait();
    registry.gate.reset();

    auto other = std::async(std::launch::async, [this]() {
        this->owners->on_registration(1, make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40"));
    });
    ASSERT_EQ(other.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_EQ(this->second->results.size(), 1u);

    // Detaching waits for the report in progress
    auto detach = std::async(std::launch::async, [this]() { this->owners->detach(); });
    EXPECT_EQ(detach.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);
    open.set_value();
    report.get();
    detach.get();
    EXPECT_EQ(this->read("AuxMeter", "Energy"), std::nullopt);
    this->owners->register_with(*this->device_model);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");
}

TEST_F(ExternalOwnersTest, InvalidRegistrationAtRuntimeKeepsTheEarlierOne) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(registry);
    const auto registered = registry.registered;

    auto registration = make_registration("meter", "AuxMeter", "Energy", "6");
    auto long_name = registration.components.at(0).variables.at(0);
    long_name.variable.name = std::string(51, 'V');
    registration.components.at(0).variables.push_back(long_name);
    this->owners->on_registration(0, registration);

    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Invalid);
    ASSERT_EQ(this->first->results.at(1).rejected.size(), 1u);
    EXPECT_EQ(this->first->results.at(1).rejected.at(0).reason, dm_types::RegistrationRejectionReason::InvalidNameKey);
    EXPECT_EQ(registry.registered.size(), registered.size());
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
    EXPECT_EQ(this->write("AuxMeter", "Energy", "7"), SetVariableStatusEnum::Accepted);
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, InvalidRegistrationOfANewOwnerLeavesTheNameFree) {
    this->start({});

    auto registration = make_registration("meter", "AuxMeter", "Energy", "5");
    registration.components.at(0).variables.at(0).variable.name = std::string(51, 'V');
    this->owners->on_registration(0, registration);
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Invalid);

    this->owners->on_registration(1, make_registration("meter", "AuxMeter", "Energy", "6"));
    ASSERT_EQ(this->second->results.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");
}

TEST_F(ExternalOwnersTest, ValueChangeBeforeTheStartupAnswerIsApplied) {
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "7"));
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "8"));
    this->owners->on_value_changed(0, make_change("other", "AuxMeter", "Energy", "9"));
    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "8");
}

TEST_F(ExternalOwnersTest, FailedRegistrationAtRuntimeRemovesTheEarlierOne) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(registry);
    ASSERT_EQ(this->read("AuxMeter", "Energy"), "5");

    registry.fail_next_registration = true;
    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Power", "1"));
    ASSERT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->first->results.at(1).owner, "meter");
    EXPECT_EQ(this->first->results.at(1).status, dm_types::RegistrationStatus::Rejected);
    EXPECT_TRUE(this->first->results.at(1).rejected.empty());
    EXPECT_EQ(this->read("AuxMeter", "Energy"), std::nullopt);
    EXPECT_EQ(this->read("AuxMeter", "Power"), std::nullopt);
    EXPECT_EQ(this->write("AuxMeter", "Energy", "6"), SetVariableStatusEnum::UnknownComponent);

    this->owners->on_registration(1, make_registration("meter", "AuxMeter", "Power", "2"));
    ASSERT_EQ(this->second->results.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).status, dm_types::RegistrationStatus::Accepted);
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, RegistrationAtRuntimeBlocksNoOtherConnection) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(0, {});
    this->owners->set_startup_registrations(
        1, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->register_with(registry);

    std::promise<void> open;
    registry.register_gate = open.get_future().share();
    auto registration = std::async(std::launch::async, [this]() {
        this->owners->on_registration(0, make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40"));
    });
    registry.register_entered.get_future().wait();
    registry.register_gate.reset();

    auto other = std::async(std::launch::async, [this]() {
        this->owners->on_value_changed(1, make_change("meter", "AuxMeter", "Energy", "6"));
    });
    ASSERT_EQ(other.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");

    auto detach = std::async(std::launch::async, [this]() { this->owners->detach(); });
    EXPECT_EQ(detach.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);
    open.set_value();
    registration.get();
    detach.get();
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Accepted);
}

TEST_F(ExternalOwnersTest, AnsweredRegistrationsAreNotAnsweredAgain) {
    this->start(make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    ASSERT_EQ(this->first->results.size(), 1u);

    this->owners->detach();
    EXPECT_EQ(this->owners->register_with(*this->device_model).size(), 1u);
    EXPECT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, ValueChangeBeforeTheRegistrationOfItsOwnerIsIgnored) {
    this->start({});

    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "8"));
    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "5"));

    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, ValueChangeDuringARuntimeRegistrationIsAppliedAfterIt) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(registry);

    auto registration = make_registration("meter", "AuxMeter", "Energy", "5");
    auto power = registration.components.at(0).variables.at(0);
    power.variable.name = "Power";
    power.attributes.at(0).value = "1";
    registration.components.at(0).variables.push_back(power);

    std::promise<void> open;
    registry.register_gate = open.get_future().share();
    auto registering =
        std::async(std::launch::async, [this, &registration]() { this->owners->on_registration(0, registration); });
    registry.register_entered.get_future().wait();
    registry.register_gate.reset();

    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Power", "9"));
    open.set_value();
    registering.get();

    EXPECT_EQ(this->first->results.size(), 2u);
    EXPECT_EQ(this->read("AuxMeter", "Power"), "9");
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, ValueChangeForARefusedOwnerIsDropped) {
    this->start({});

    this->owners->on_value_changed(1, make_change("meter", "AuxMeter", "Energy", "8"));
    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "5"));
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");

    this->owners->on_registration(1, make_registration("meter", "AuxMeter", "Energy", "6"));
    ASSERT_EQ(this->second->results.size(), 1u);
    EXPECT_EQ(this->second->results.at(0).status, dm_types::RegistrationStatus::OwnerInUse);

    this->owners->on_registration(0, make_release("meter"));
    this->owners->on_registration(1, make_registration("meter", "AuxMeter", "Energy", "7"));
    ASSERT_EQ(this->second->results.size(), 2u);
    EXPECT_EQ(this->second->results.at(1).status, dm_types::RegistrationStatus::Accepted);
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "7");
}

TEST_F(ExternalOwnersTest, ValueChangeWhileHeldChangesAreReportedIsReportedAfterThem) {
    auto registration = make_registration("meter", "AuxMeter", "Energy", "5");
    auto power = registration.components.at(0).variables.at(0);
    power.variable.name = "Power";
    power.attributes.at(0).value = "1";
    registration.components.at(0).variables.push_back(power);

    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(0, make_registrations({registration}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(registry);

    std::promise<void> open_registration;
    registry.register_gate = open_registration.get_future().share();
    auto registering =
        std::async(std::launch::async, [this, &registration]() { this->owners->on_registration(0, registration); });
    registry.register_entered.get_future().wait();
    registry.register_gate.reset();
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "6"));
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Power", "7"));

    // The report of the first held change waits while a newer change of the second arrives
    std::promise<void> open_report;
    registry.gate = open_report.get_future().share();
    open_registration.set_value();
    const bool reporting = registry.entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    EXPECT_TRUE(reporting);
    registry.gate.reset();
    if (reporting) {
        this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Power", "8"));
        open_report.set_value();
    }
    registering.get();

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");
    EXPECT_EQ(this->read("AuxMeter", "Power"), "8");
    EXPECT_EQ(this->first->results.size(), 2u);
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, ValueChangeBeforeTheStartupAnswerForAnUnregisteredOwnerIsIgnored) {
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "8"));
    this->start({});

    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "5"));
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "5");
}

TEST_F(ExternalOwnersTest, MessagesBeforeTheStartupAnswerAreAppliedInOrder) {
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "7"));
    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "6"));
    this->owners->on_value_changed(0, make_change("meter", "AuxMeter", "Energy", "8"));
    this->start({});

    EXPECT_EQ(this->read("AuxMeter", "Energy"), "8");
}

TEST_F(ExternalOwnersTest, AcceptedWriteIsStoredWhenItsResultIsHandled) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    EXPECT_EQ(write.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "40");

    this->second->release_results();
    EXPECT_EQ(write.get(), SetVariableStatusEnum::Accepted);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");
}

TEST_F(ExternalOwnersTest, RegistrationSentBeforeTheReplyKeepsTheWrittenValue) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    // The owner registers again with the value it had before it applied the write, then answers the write
    this->owners->on_registration(1, make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40"));
    this->second->release_results();

    EXPECT_EQ(write.get(), SetVariableStatusEnum::Accepted);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");
}

TEST_F(ExternalOwnersTest, ValueChangeSentAfterTheReplyWins) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    this->second->release_results();
    this->owners->on_value_changed(1, make_change("cooling", "CoolingCtrlr", "FanSpeed", "70"));

    EXPECT_EQ(write.get(), SetVariableStatusEnum::Accepted);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "70");
}

TEST_F(ExternalOwnersTest, ReleaseBeforeTheWriteResultEndsTheWait) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    // The release waits in libocpp for the write, which waits for its result, queued behind the release
    auto release =
        std::async(std::launch::async, [this]() { this->owners->on_registration(1, make_release("cooling")); });
    ASSERT_EQ(release.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(write.get(), SetVariableStatusEnum::Rejected);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), std::nullopt);

    EXPECT_NO_THROW(this->second->release_results());
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), std::nullopt);
}

TEST_F(ExternalOwnersTest, DetachEndsTheWaitForWriteResults) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    auto detach = std::async(std::launch::async, [this]() { this->owners->detach(); });
    ASSERT_EQ(detach.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(write.get(), SetVariableStatusEnum::Rejected);

    // The owner applied the value, which the next device model gets
    this->second->release_results();
    this->owners->register_with(*this->device_model);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");
}

TEST_F(ExternalOwnersTest, FailedRegistrationEndsTheWaitForWriteResults) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(0, {});
    this->owners->set_startup_registrations(
        1, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->owners->register_with(registry);
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    registry.fail_next_registration = true;
    auto registering = std::async(std::launch::async, [this]() {
        this->owners->on_registration(1, make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40"));
    });
    const bool ended = registering.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    this->second->release_results();
    registering.get();

    EXPECT_TRUE(ended);
    EXPECT_EQ(write.get(), SetVariableStatusEnum::Rejected);
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, WriteFromTheHandlerOfTheOwnersMessagesDoesNotWaitForItsResult) {
    auto registration = make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40");
    auto mode = registration.components.at(0).variables.at(0);
    mode.variable.name = "Mode";
    mode.attributes.at(0).value = "1";
    registration.components.at(0).variables.push_back(mode);
    this->start({}, make_registrations({registration}));
    // A listener that writes Mode of the same owner when the owner reports FanSpeed
    this->device_model->register_variable_listener(
        [this](const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>&, const ocpp::v2::Component&,
               const ocpp::v2::Variable& variable, const ocpp::v2::VariableCharacteristics&,
               const ocpp::v2::VariableAttribute&, const std::string&, const std::string&) {
            if (variable.name.get() == "FanSpeed") {
                this->write("CoolingCtrlr", "Mode", "2");
            }
        });
    this->second->hold_results = true;

    dm_types::OwnerMessage message;
    message.value_change = make_change("cooling", "CoolingCtrlr", "FanSpeed", "70");
    auto handling = std::async(std::launch::async, [this, &message]() { this->owners->on_owner_message(1, message); });
    const bool returned = handling.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    this->second->release_results();
    handling.get();

    EXPECT_TRUE(returned);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "70");
    EXPECT_EQ(this->read("CoolingCtrlr", "Mode"), "2");
}

TEST_F(ExternalOwnersTest, WriteResultStoresTheWriteItAnswers) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));
    this->second->hold_results = true;

    auto write = std::async(std::launch::async, [this]() { return this->write("CoolingCtrlr", "FanSpeed", "60"); });
    while (this->second->write_count() == 0) {
        std::this_thread::yield();
    }
    {
        std::lock_guard<std::mutex> lock(this->second->mutex);
        this->second->held_results.at(0).write_result->request.value = "99";
    }
    this->second->release_results();

    EXPECT_EQ(write.get(), SetVariableStatusEnum::Accepted);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");
}

TEST_F(ExternalOwnersTest, AcceptedResultOfAWriteNoLongerWaitingIsApplied) {
    this->start({}, make_registrations({make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40")}));

    dm_types::WriteRequest request;
    request.owner = "cooling";
    request.component.name = "CoolingCtrlr";
    request.variable.name = "FanSpeed";
    request.attribute_type = types::ocpp::AttributeEnum::Actual;
    request.value = "60";
    request.source = "csms";
    dm_types::OwnerMessage message;
    message.write_result = dm_types::WriteResult{"unknown", request, types::ocpp::SetVariableStatusEnumType::Accepted};
    this->owners->on_owner_message(1, message);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");

    message.write_result->request.value = "70";
    message.write_result->status = types::ocpp::SetVariableStatusEnumType::Rejected;
    this->owners->on_owner_message(1, message);
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "60");
}

TEST_F(ExternalOwnersTest, MessageWithMoreThanOnePartIsIgnored) {
    this->start({});

    dm_types::OwnerMessage message;
    message.registration = make_registration("meter", "AuxMeter", "Energy", "5");
    message.value_change = make_change("meter", "AuxMeter", "Energy", "6");
    this->owners->on_owner_message(0, message);
    this->owners->on_owner_message(0, dm_types::OwnerMessage{});

    EXPECT_TRUE(this->first->results.empty());
    EXPECT_EQ(this->read("AuxMeter", "Energy"), std::nullopt);
}

TEST_F(ExternalOwnersTest, RegisteringAgainKeepsTheProvider) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_registration("meter", "AuxMeter", "Energy", "5")}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(registry);

    this->owners->on_registration(0, make_registration("meter", "AuxMeter", "Energy", "6"));
    this->owners->detach();
    const auto results = this->owners->register_with(registry);

    ASSERT_EQ(registry.providers.size(), 3u);
    EXPECT_EQ(registry.providers.at(1), registry.providers.at(0));
    EXPECT_EQ(registry.providers.at(2), registry.providers.at(0));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.at(0).result.rejected.empty());
    EXPECT_EQ(this->read("AuxMeter", "Energy"), "6");
    this->owners->detach();
}

TEST(ExternalOwnerConversions, ProviderChangedIsReportedAsDefinedByTheOwner) {
    ocpp::v2::RejectedVariable rejected{make_component("CoolingCtrlr"), make_variable("FanSpeed"),
                                        ocpp::v2::RegistrationRejectionReason::ProviderChanged, "cooling"};
    dm_types::RejectedVariable converted;
    ASSERT_NO_THROW(converted = dm::to_everest_rejected_variable(rejected));
    EXPECT_EQ(converted.reason, dm_types::RegistrationRejectionReason::AlreadyDefined);
    EXPECT_EQ(converted.defined_by, "cooling");
}

TEST(ExternalOwnerNames, NamesOfOcppAndLibocppAreNotExternalOwners) {
    EXPECT_TRUE(dm::is_external_owner("meter"));
    EXPECT_FALSE(dm::is_external_owner(dm::VARIABLE_OWNER_EVEREST));
    EXPECT_FALSE(dm::is_external_owner(dm::VARIABLE_OWNER_EVEREST_CONFIG));
    EXPECT_FALSE(dm::is_external_owner(ocpp::v2::VARIABLE_OWNER_COMPONENT_CONFIG));
    EXPECT_FALSE(dm::is_external_owner(ocpp::v2::VARIABLE_OWNER_OCPP_STACK));
}

namespace {

/// \brief A registration of \p owner with one integer variable per entry of \p keys on \p component, each named by the
/// entry's first and declaring its second as OCPP 1.6 key, if any
dm_types::Registration
make_keyed_registration(const std::string& owner, const std::string& component,
                        const std::vector<std::pair<std::string, std::optional<std::string>>>& keys) {
    auto registration = make_registration(owner, component, keys.at(0).first, "1");
    auto& variables = registration.components.at(0).variables;
    const auto prototype = variables.at(0);
    variables.clear();
    for (const auto& [variable, key] : keys) {
        auto registered = prototype;
        registered.variable.name = variable;
        registered.ocpp16_key = key;
        variables.push_back(registered);
    }
    return registration;
}

} // namespace

TEST_F(ExternalOwnersTest, DeclaredKeysAreBoundWithTheStartupRegistration) {
    this->owners->set_startup_registrations(
        0, make_registrations({make_keyed_registration("cooling", "CoolingUnit",
                                                       {{"FanSpeed", "CoolingFanSpeed"}, {"Mode", std::nullopt}})}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);

    EXPECT_EQ(binder.bound["cooling"], (std::map<std::string, std::string>{{"FanSpeed", "CoolingFanSpeed"}}));
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_TRUE(this->first->results.at(0).rejected.empty());
    EXPECT_EQ(this->read("CoolingUnit", "FanSpeed"), "1");
    EXPECT_EQ(this->read("CoolingUnit", "Mode"), "1");
}

TEST_F(ExternalOwnersTest, VariableWithARefusedKeyIsNotRegistered) {
    binder.refused.insert("TakenKey");
    this->owners->set_startup_registrations(
        0, make_registrations({make_keyed_registration("cooling", "CoolingUnit",
                                                       {{"FanSpeed", "TakenKey"}, {"Mode", "CoolingMode"}})}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);

    EXPECT_FALSE(this->read("CoolingUnit", "FanSpeed").has_value());
    EXPECT_EQ(this->read("CoolingUnit", "Mode"), "1");
    ASSERT_EQ(this->first->results.size(), 1u);
    const auto& result = this->first->results.at(0);
    EXPECT_EQ(result.status, dm_types::RegistrationStatus::Accepted);
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.at(0).variable.name, "FanSpeed");
    EXPECT_EQ(result.rejected.at(0).reason, dm_types::RegistrationRejectionReason::Ocpp16KeyInUse);
}

TEST_F(ExternalOwnersTest, RuntimeRegistrationReplacesAndReleaseRemovesTheKeys) {
    this->owners->set_startup_registrations(0, {});
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);

    this->owners->on_registration(0, make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "FirstKey"}}));
    EXPECT_EQ(binder.bound["cooling"], (std::map<std::string, std::string>{{"FanSpeed", "FirstKey"}}));

    this->owners->on_registration(0, make_keyed_registration("cooling", "CoolingUnit", {{"Mode", "SecondKey"}}));
    EXPECT_EQ(binder.bound["cooling"], (std::map<std::string, std::string>{{"Mode", "SecondKey"}}));

    this->owners->on_registration(0, make_release("cooling"));
    EXPECT_EQ(binder.bound.count("cooling"), 0u);
}

TEST_F(ExternalOwnersTest, KeyOfAVariableTheDeviceModelRejectsIsRemoved) {
    this->start({}, make_registrations({make_registration("meter", "CoolingUnit", "FanSpeed", "1")}));
    this->owners->detach();
    this->owners->register_with(*this->device_model, &binder);

    this->owners->on_registration(
        0, make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "FanKey"}, {"Mode", "ModeKey"}}));

    EXPECT_EQ(binder.bound["cooling"], (std::map<std::string, std::string>{{"Mode", "ModeKey"}}));
    ASSERT_FALSE(this->first->results.empty());
    const auto& rejected = this->first->results.back().rejected;
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected.at(0).reason, dm_types::RegistrationRejectionReason::AlreadyDefined);
}

TEST_F(ExternalOwnersTest, DetachRemovesTheKeys) {
    this->owners->set_startup_registrations(
        0, make_registrations({make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "FanKey"}})}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);
    ASSERT_EQ(binder.bound.count("cooling"), 1u);

    this->owners->detach();
    EXPECT_EQ(binder.bound.count("cooling"), 0u);
}

TEST_F(ExternalOwnersTest, KeysAreIgnoredWithoutABinder) {
    this->start(make_registrations({make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "FanKey"}})}));

    EXPECT_EQ(this->read("CoolingUnit", "FanSpeed"), "1");
    ASSERT_EQ(this->first->results.size(), 1u);
    EXPECT_TRUE(this->first->results.at(0).rejected.empty());
}

TEST_F(ExternalOwnersTest, KeysAreRemovedWhenTheRegistrationFails) {
    WrappingRegistry registry(*this->device_model);
    this->owners->set_startup_registrations(
        0, make_registrations({make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "FanKey"}})}));
    this->owners->set_startup_registrations(1, {});
    registry.fail_next_registration = true;
    this->owners->register_with(registry, &binder);
    EXPECT_EQ(binder.bound.count("cooling"), 0u);

    this->owners->on_registration(0, make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "FanKey"}}));
    ASSERT_EQ(binder.bound.count("cooling"), 1u);

    // At runtime as well
    registry.fail_next_registration = true;
    this->owners->on_registration(0, make_keyed_registration("cooling", "CoolingUnit", {{"Mode", "ModeKey"}}));
    EXPECT_EQ(binder.bound.count("cooling"), 0u);
    this->owners->detach();
}

TEST_F(ExternalOwnersTest, RefusedKeysAndRejectedVariablesAreReportedTogether) {
    binder.refused.insert("TakenKey");
    this->start({}, make_registrations({make_registration("meter", "CoolingUnit", "FanSpeed", "1")}));
    this->owners->detach();
    this->owners->register_with(*this->device_model, &binder);

    this->owners->on_registration(
        0, make_keyed_registration("cooling", "CoolingUnit",
                                   {{"FanSpeed", "FanKey"}, {"Mode", "TakenKey"}, {"Level", "LevelKey"}}));

    EXPECT_EQ(binder.bound["cooling"], (std::map<std::string, std::string>{{"Level", "LevelKey"}}));
    EXPECT_EQ(this->read("CoolingUnit", "Level"), "1");
    EXPECT_FALSE(this->read("CoolingUnit", "Mode").has_value());
    ASSERT_FALSE(this->first->results.empty());
    const auto& result = this->first->results.back();
    EXPECT_EQ(result.status, dm_types::RegistrationStatus::Accepted);
    std::map<std::string, dm_types::RegistrationRejectionReason> reasons;
    for (const auto& rejected : result.rejected) {
        reasons.emplace(rejected.variable.name, rejected.reason);
    }
    EXPECT_EQ(reasons, (std::map<std::string, dm_types::RegistrationRejectionReason>{
                           {"FanSpeed", dm_types::RegistrationRejectionReason::AlreadyDefined},
                           {"Mode", dm_types::RegistrationRejectionReason::Ocpp16KeyInUse}}));
}

TEST_F(ExternalOwnersTest, ComponentWhoseVariablesAreAllRefusedIsLeftOut) {
    binder.refused.insert("TakenKey");
    auto registration = make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "TakenKey"}});
    registration.components.push_back(
        make_keyed_registration("cooling", "Pump", {{"Flow", std::nullopt}}).components.at(0));
    this->owners->set_startup_registrations(0, make_registrations({registration}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);

    EXPECT_FALSE(this->read("CoolingUnit", "FanSpeed").has_value());
    EXPECT_EQ(this->read("Pump", "Flow"), "1");
    ASSERT_EQ(this->first->results.size(), 1u);
    ASSERT_EQ(this->first->results.at(0).rejected.size(), 1u);
    EXPECT_EQ(this->first->results.at(0).rejected.at(0).component.name, "CoolingUnit");
}

TEST_F(ExternalOwnersTest, RegistrationWhoseKeysAreAllRefusedRegistersNothing) {
    binder.refused.insert("TakenKey");
    this->owners->set_startup_registrations(
        0, make_registrations({make_keyed_registration("cooling", "CoolingUnit", {{"FanSpeed", "TakenKey"}})}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);

    EXPECT_FALSE(this->read("CoolingUnit", "FanSpeed").has_value());
    ASSERT_EQ(this->first->results.size(), 1u);
    const auto& result = this->first->results.at(0);
    EXPECT_EQ(result.status, dm_types::RegistrationStatus::Accepted);
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.at(0).reason, dm_types::RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_EQ(binder.bound.count("cooling"), 0u);
}

TEST_F(ExternalOwnersTest, RegistrationWithKeysThatDoNotFitOcppIsInvalid) {
    this->owners->set_startup_registrations(
        0,
        make_registrations({make_keyed_registration(
            "cooling", "CoolingUnit", {{"FanSpeed", ""}, {"Mode", std::string(51, 'K')}, {"Speed", "CoolingSpeed"}})}));
    this->owners->set_startup_registrations(1, {});
    this->owners->register_with(*this->device_model, &binder);

    EXPECT_FALSE(this->read("CoolingUnit", "Speed").has_value());
    ASSERT_EQ(this->first->results.size(), 1u);
    const auto& result = this->first->results.at(0);
    EXPECT_EQ(result.status, dm_types::RegistrationStatus::Invalid);
    ASSERT_EQ(result.rejected.size(), 2u);
    EXPECT_EQ(result.rejected.at(0).variable.name, "FanSpeed");
    EXPECT_EQ(result.rejected.at(1).variable.name, "Mode");
    for (const auto& variable : result.rejected) {
        EXPECT_EQ(variable.reason, dm_types::RegistrationRejectionReason::InvalidNameKey);
    }
    EXPECT_EQ(binder.bound.count("cooling"), 0u);
}
