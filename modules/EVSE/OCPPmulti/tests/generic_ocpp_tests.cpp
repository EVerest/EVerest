// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <generic_ocpp.hpp>

#include "mrec_fixture.hpp"
#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

namespace {
using namespace stubs;

TEST(GenericOcppTester, init) {
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    std::vector<json> received;
    interfaces.subscribe_var("evse_manager", "call_external_ready_to_start_charging",
                             [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    // connect required interfaces
    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    // Chargepoint expected calls
    InSequence seq;
    EXPECT_CALL(chargepoint, init(_));
    EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _));
    EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)));
    EXPECT_CALL(chargepoint, start(_, _, false));
    EXPECT_CALL(chargepoint, connect_websocket());

    // GenericOcpp object
    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());

    interfaces.subscribe_global_all_errors(
        [&ocpp](const Everest::error::Error& arg) { ocpp.cb_error_handler(arg); },
        [&ocpp](const Everest::error::Error& arg) { ocpp.cb_error_cleared_handler(arg); });

    ocpp.init();

    // ocpp.ready() waits for the EVSE managers to be ready
    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);

    ocpp.ready(interfaces.get_config_service_client());

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0], json{});
    EXPECT_EQ(received[1], json{});
}

TEST_F(GenericOcppProvidesTester, errorTypeNotRemapped) {
    // the error type must reach the chargepoint implementations unmodified: the v16
    // error-code map and the v2 map_error() lookup are keyed on the full type
    using ::testing::_;

    std::optional<ocpp_multi::GenericChargePointInterface::EventInfo> event;
    EXPECT_CALL(chargepoint, on_event(_)).WillOnce([&event](const auto& arg) { event = arg; });

    Everest::error::Error error;
    error.type = "evse_board_support/MREC2GroundFailure";
    ocpp->cb_error_handler(error);

    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->error.has_value());
    EXPECT_EQ(event->error->type, "evse_board_support/MREC2GroundFailure");
    EXPECT_FALSE(event->event_cleared);
}

TEST_F(GenericOcppProvidesTester, mrecErrorsForwardedUnmodifiedOnRaiseAndClear) {
    // every MREC error reaches the chargepoint implementation unchanged, with the
    // cleared flag matching the direction; the protocol-specific mapping happens there
    using ::testing::_;

    std::vector<ocpp_multi::GenericChargePointInterface::EventInfo> events;
    EXPECT_CALL(chargepoint, on_event(_)).WillRepeatedly([&events](const auto& arg) { events.push_back(arg); });

    for (const auto& entry : mrec_fixture::ENTRIES) {
        SCOPED_TRACE(std::string(entry.type));
        events.clear();

        Everest::error::Error error;
        error.type = std::string(entry.type);
        error.sub_type = "some_sub_type";
        error.message = "sensor reports fault";
        ocpp->cb_error_handler(error);
        ocpp->cb_error_cleared_handler(error);

        ASSERT_EQ(events.size(), 2U);
        for (std::size_t i = 0; i < events.size(); ++i) {
            ASSERT_TRUE(events[i].error.has_value());
            EXPECT_EQ(events[i].error->type, entry.type);
            EXPECT_EQ(events[i].error->sub_type, "some_sub_type");
            EXPECT_EQ(events[i].error->message, "sensor reports fault");
            EXPECT_EQ(events[i].error->uuid, error.uuid);
            EXPECT_EQ(events[i].event_cleared, i == 1);
        }
    }
}

// CustomMrecErrorMapPath: relative paths resolve against the module share directory

class CustomMrecErrorMapPathTester : public testing::Test {
protected:
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;
    std::filesystem::path share_root;
    std::filesystem::path module_share;

    void SetUp() override {
        // mkdtemp creates a fresh, uniquely named directory, so concurrent runs never share it
        std::string tmpl = (std::filesystem::temp_directory_path() / "ocppmulti_mrec_XXXXXX").string();
        ASSERT_NE(::mkdtemp(tmpl.data()), nullptr) << "mkdtemp failed for " << tmpl;
        share_root = tmpl;
        module_share = share_root / "OCPPmulti";
        std::filesystem::create_directories(module_share);
        interfaces.set_share_path(module_share);

        interfaces.add_charger_information("info");
        interfaces.add_data_transfer("data_transfer");
        interfaces.add_display_message("display");
        interfaces.add_evse_energy_sink("energy_node", 1);
        interfaces.add_evse_manager("evse_manager_1");
        interfaces.add_evse_manager("evse_manager_2");
        interfaces.add_extensions_15118("evsev2g");
        interfaces.add_reservation("reservation");
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(share_root, ec);
    }

    static void write_overrides(const std::filesystem::path& file) {
        std::ofstream out(file);
        out << R"({"evse_manager/MREC4OverCurrentFailure": "OVERRIDE_CX004"})";
    }

    std::unique_ptr<stubs::GenericOcppTester> make_and_init() {
        auto ocpp = std::make_unique<stubs::GenericOcppTester>(chargepoint, interfaces.get_module_info(), config,
                                                               interfaces.get_provides(), interfaces.get_requires());
        ocpp->init();
        return ocpp;
    }
};

TEST_F(CustomMrecErrorMapPathTester, RelativeResolvesAgainstShareDir) {
    write_overrides(module_share / "overrides.json");
    config.CustomMrecErrorMapPath = "overrides.json";

    const auto ocpp = make_and_init();

    std::string mapped;
    ASSERT_TRUE(ocpp->map_error("evse_manager/MREC4OverCurrentFailure", mapped));
    EXPECT_EQ(mapped, "OVERRIDE_CX004");
}

TEST_F(CustomMrecErrorMapPathTester, AbsolutePathUnchanged) {
    const auto file = share_root / "elsewhere.json";
    write_overrides(file);
    config.CustomMrecErrorMapPath = file.string();

    const auto ocpp = make_and_init();

    std::string mapped;
    ASSERT_TRUE(ocpp->map_error("evse_manager/MREC4OverCurrentFailure", mapped));
    EXPECT_EQ(mapped, "OVERRIDE_CX004");
}

TEST_F(CustomMrecErrorMapPathTester, MissingFileClearError) {
    config.CustomMrecErrorMapPath = "missing.json";
    const auto expected_path = (module_share / "missing.json").string();

    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());
    try {
        ocpp.init();
        FAIL() << "init() must throw for a missing CustomMrecErrorMapPath file";
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        EXPECT_NE(what.find("CustomMrecErrorMapPath"), std::string::npos) << what;
        EXPECT_NE(what.find(expected_path), std::string::npos) << what;
    }
}

TEST_F(CustomMrecErrorMapPathTester, EmptyUsesDefaults) {
    config.CustomMrecErrorMapPath = "";

    const auto ocpp = make_and_init();

    std::string mapped;
    ASSERT_TRUE(ocpp->map_error("evse_manager/MREC4OverCurrentFailure", mapped));
    EXPECT_EQ(mapped, "CX004");
}

} // namespace
