// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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
    }

    types::ocpp::SetVariableStatusEnumType set_value(const dm_types::WriteRequest& request) override {
        this->writes.push_back(request);
        if (this->gate.has_value()) {
            this->entered.set_value();
            this->gate->wait();
        }
        return this->reply;
    }

    std::string id;
    std::vector<dm_types::RegistrationResult> results;
    std::vector<dm_types::WriteRequest> writes;
    types::ocpp::SetVariableStatusEnumType reply{types::ocpp::SetVariableStatusEnumType::Accepted};
    /// \brief When set, a write waits for it after signalling entered
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

TEST_F(ExternalOwnersTest, VariablesThatDoNotFitOcppAreRejected) {
    auto registration = make_registration("cooling", "CoolingCtrlr", "FanSpeed", "40");
    auto long_unit = registration.components.at(0).variables.at(0);
    long_unit.variable.name = "Temperature";
    long_unit.characteristics.unit = std::string(17, 'C');
    registration.components.at(0).variables.push_back(long_unit);
    auto long_name = registration.components.at(0);
    long_name.component.name = std::string(51, 'L');
    registration.components.push_back(long_name);
    this->start(make_registrations({registration}));

    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "40");
    EXPECT_EQ(this->read("CoolingCtrlr", "Temperature"), std::nullopt);
    ASSERT_EQ(this->first->results.size(), 1u);
    const auto& rejected = this->first->results.at(0).rejected;
    ASSERT_EQ(rejected.size(), 3u);
    EXPECT_EQ(rejected.at(0).variable.name, "Temperature");
    for (const auto& variable : rejected) {
        EXPECT_EQ(variable.reason, dm_types::RegistrationRejectionReason::InvalidNameKey);
    }

    // A value change after a rejected variable reaches the stored registration without failing
    this->owners->on_value_changed(0, make_change("cooling", "CoolingCtrlr", "FanSpeed", "50"));
    EXPECT_EQ(this->read("CoolingCtrlr", "FanSpeed"), "50");
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
