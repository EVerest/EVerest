// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Loading and validation of ErrorMappingPath by GenericOcpp: init() checks the file itself, ready() checks it
// against the charger topology and the OCPP 2.x device model.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

namespace {

namespace fs = std::filesystem;
using ::testing::_;
using ::testing::HasSubstr;

const fs::path EXAMPLE_FILE = fs::absolute(fs::path(ERROR_MAPPING_DIR) / "error_mapping.example.json");

std::string what_of(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    ADD_FAILURE() << "no std::runtime_error thrown";
    return {};
}

class GenericOcppErrorMappingTest : public testing::Test {
protected:
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    std::unique_ptr<stubs::ModuleInterfaces> interfaces;
    std::unique_ptr<stubs::GenericOcppTester> ocpp;
    fs::path work_dir;

    void SetUp() override {
        work_dir = fs::temp_directory_path() / ("ocppmulti_error_mapping_tests_" + std::to_string(getpid())) /
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        fs::remove_all(work_dir);
        fs::create_directories(work_dir);

        interfaces = std::make_unique<stubs::ModuleInterfaces>();
        ocpp = std::make_unique<stubs::GenericOcppTester>(chargepoint, interfaces->get_module_info(), config,
                                                          interfaces->get_provides(), interfaces->get_requires());
        interfaces->add_charger_information("info");
        interfaces->add_evse_energy_sink("energy_node", 1);
        interfaces->add_evse_manager("evse_manager_1");
        interfaces->add_evse_manager("evse_manager_2");
        chargepoint.load_store("default_store.json");
    }

    void TearDown() override {
        ocpp.reset();
        interfaces.reset();
        fs::remove_all(work_dir.parent_path());
    }

    fs::path write_mapping(const std::string& content) {
        const auto path = work_dir / "error_mapping.json";
        std::ofstream(path) << content;
        return path;
    }

    void ready() {
        interfaces->publish_ready(0, true);
        interfaces->publish_ready(1, true);
        ocpp->ready(interfaces->get_config_service_client());
    }

    // the calls of a ready() that runs to completion
    void expect_start() {
        EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
        EXPECT_CALL(chargepoint, set_message_queue_resume_delay(_)).Times(1);
        EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
        EXPECT_CALL(chargepoint, connect_websocket()).Times(1);
    }
};

ocpp::v2::GetVariableResult variable_result(ocpp::v2::GetVariableStatusEnum status) {
    ocpp::v2::GetVariableResult result;
    result.attributeStatus = status;
    return result;
}

TEST_F(GenericOcppErrorMappingTest, NoMappingWithoutPath) {
    ocpp->init();
    EXPECT_FALSE(ocpp->loaded_error_mapping().has_value());
}

TEST_F(GenericOcppErrorMappingTest, LoadsValidFile) {
    config.ErrorMappingPath = EXAMPLE_FILE.string();
    ocpp->init();
    ASSERT_TRUE(ocpp->loaded_error_mapping().has_value());
    EXPECT_EQ(ocpp->loaded_error_mapping()->entries().size(), 4U);
}

TEST_F(GenericOcppErrorMappingTest, ResolvesRelativePathAgainstModuleShare) {
    // the stub's module share path is ./OCPP201, so a relative path resolves to ./OCPPmulti/<path>
    const auto share = fs::path("OCPPmulti") / work_dir.filename();
    fs::create_directories(share);
    fs::copy_file(EXAMPLE_FILE, share / "mapping.json", fs::copy_options::overwrite_existing);
    config.ErrorMappingPath = (fs::path(work_dir.filename()) / "mapping.json").string();

    ocpp->init();
    fs::remove_all(share);
    EXPECT_TRUE(ocpp->loaded_error_mapping().has_value());
}

TEST_F(GenericOcppErrorMappingTest, InitThrowsOnSchemaViolationNamingTheEntry) {
    config.ErrorMappingPath = write_mapping(R"({"generic/VendorError": {"v16": {"error_code": "Bad"}}})").string();
    const auto what = what_of([this]() { ocpp->init(); });
    EXPECT_THAT(what, HasSubstr("generic/VendorError"));
    EXPECT_THAT(what, HasSubstr("error_code"));
}

TEST_F(GenericOcppErrorMappingTest, InitThrowsOnMissingFile) {
    config.ErrorMappingPath = (work_dir / "missing.json").string();
    EXPECT_THAT(what_of([this]() { ocpp->init(); }), HasSubstr("cannot open"));
}

TEST_F(GenericOcppErrorMappingTest, LoadsEntryReplacingBuiltin) {
    config.ErrorMappingPath =
        write_mapping(R"({"evse_board_support/MREC3HighTemperature": {"v2": {"tech_code": "T"}}})").string();
    ocpp->init();
    ASSERT_TRUE(ocpp->loaded_error_mapping().has_value());
    EXPECT_NE(ocpp->loaded_error_mapping()->find("evse_board_support/MREC3HighTemperature", ""), nullptr);
}

TEST_F(GenericOcppErrorMappingTest, InitThrowsOnUnknownErrorType) {
    config.ErrorMappingPath =
        write_mapping(R"({"evse_board_support/MREC3HighTemprature": {"v2": {"tech_code": "T"}}})").string();
    EXPECT_THAT(what_of([this]() { ocpp->init(); }),
                HasSubstr("unknown error type 'evse_board_support/MREC3HighTemprature'"));
}

TEST_F(GenericOcppErrorMappingTest, InitThrowsOnUnknownErrorNamespace) {
    config.ErrorMappingPath = write_mapping(R"({"genric/VendorError": {"v2": {"tech_code": "T"}}})").string();
    EXPECT_THAT(what_of([this]() { ocpp->init(); }), HasSubstr("unknown error namespace 'genric'"));
}

TEST_F(GenericOcppErrorMappingTest, InitThrowsWhenErrorsDirIsMissing) {
    config.ErrorMappingPath = EXAMPLE_FILE.string();
    interfaces->set_errors_dir(work_dir / "no_errors_dir");
    EXPECT_THAT(what_of([this]() { ocpp->init(); }), HasSubstr("errors directory"));
}

TEST_F(GenericOcppErrorMappingTest, ReadyThrowsOnUnknownEvse) {
    config.ErrorMappingPath = write_mapping(R"({"generic/VendorError": {"tier_mapping": {"evse": 3}}})").string();
    EXPECT_CALL(chargepoint, init(_)).Times(1);
    ocpp->init();
    EXPECT_THAT(what_of([this]() { ready(); }), HasSubstr("no EVSE 3"));
}

TEST_F(GenericOcppErrorMappingTest, StrictReadyThrowsOnVariableMissingFromDeviceModel) {
    config.ErrorMappingPath = write_mapping(
                                  R"({"generic/VendorError": {"v2": {"component_name": "Connector",
                                      "variable_name": "Temperature"}}})")
                                  .string();
    config.ErrorMappingStrictValidation = true;
    std::vector<ocpp::v2::GetVariableData> requests;
    EXPECT_CALL(chargepoint, init(_)).Times(1);
    EXPECT_CALL(chargepoint, get_variables(_)).WillRepeatedly([&requests](const auto& data) {
        requests.insert(requests.end(), data.begin(), data.end());
        return std::vector{variable_result(ocpp::v2::GetVariableStatusEnum::UnknownVariable)};
    });
    ocpp->init();

    EXPECT_THAT(what_of([this]() { ready(); }), HasSubstr("no variable 'Temperature' on component 'Connector'"));
    ASSERT_FALSE(requests.empty());
    EXPECT_EQ(requests.front().component.name.get(), "Connector");
    EXPECT_EQ(requests.front().variable.name.get(), "Temperature");
}

TEST_F(GenericOcppErrorMappingTest, NonStrictReadyStartsDespiteVariableMissingFromDeviceModel) {
    config.ErrorMappingPath =
        write_mapping(R"({"generic/VendorError": {"v2": {"component_name": "Connector"}}})").string();
    EXPECT_CALL(chargepoint, init(_)).Times(1);
    EXPECT_CALL(chargepoint, get_variables(_)).WillRepeatedly([](const auto&) {
        return std::vector{variable_result(ocpp::v2::GetVariableStatusEnum::UnknownVariable)};
    });
    expect_start();
    ocpp->init();
    ready();
}

TEST_F(GenericOcppErrorMappingTest, ReportingIgnoresEntryMapping) {
    config.ErrorMappingPath = write_mapping(R"({"generic/VendorError#Spd": {"tier_mapping": {"evse": 2}}})").string();
    EXPECT_CALL(chargepoint, init(_)).Times(1);
    expect_start();
    ocpp->init();
    ready();

    std::vector<ocpp_multi::GenericChargePointInterface::EventInfo> events;
    EXPECT_CALL(chargepoint, on_event(_)).WillRepeatedly([&events](const auto& event) { events.push_back(event); });
    Everest::error::Error error;
    error.type = "generic/VendorError";
    error.sub_type = "Spd";
    error.origin.module_id = "api";
    error.origin.implementation_id = "main";
    error.origin.mapping = Mapping{1};
    ocpp->cb_error_handler(error);
    ocpp->cb_error_handler(error);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events.front().evse_id, 1);
}

} // namespace
