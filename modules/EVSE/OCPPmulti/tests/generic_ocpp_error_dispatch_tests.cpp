// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins how GenericOcpp hands errors to the chargepoint implementation: which errors are dropped,
// the fault path, the ids stamped on each event, and how errors and faults raised before ready()
// are queued and replayed.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <generic_ocpp.hpp>

#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

namespace {

using EventInfo = ocpp_multi::GenericChargePointInterface::EventInfo;
using ::testing::_;
using ::testing::Field;
using ::testing::InSequence;

constexpr const char* INOPERATIVE = "evse_manager/Inoperative";
constexpr const char* MREC_ERROR = "evse_board_support/MREC2GroundFailure";

Everest::error::Error make_error(const std::string& type, std::optional<Mapping> mapping,
                                 const std::string& uuid = "error-uuid-1") {
    Everest::error::Error error;
    error.type = type;
    error.message = "sensor reports fault";
    error.origin = ImplementationIdentifier("bsp_1", "main", mapping);
    error.uuid = Everest::error::UUID(uuid);
    return error;
}

auto raised() {
    return Field(&EventInfo::event_cleared, false);
}

auto cleared() {
    return Field(&EventInfo::event_cleared, true);
}

class GenericOcppErrorDispatch : public stubs::GenericOcppProvidesTester {
protected:
    std::vector<EventInfo> events;

    void capture_events() {
        EXPECT_CALL(chargepoint, on_event(_)).WillRepeatedly([this](const EventInfo& event) {
            events.push_back(event);
        });
    }
};

// Inoperative is reported through the per-EVSE fault handlers; the copy arriving through the global
// error subscription is dropped so it is not reported twice
TEST_F(GenericOcppErrorDispatch, InoperativeViaGlobalHandlerIsDropped) {
    EXPECT_CALL(chargepoint, on_event(_)).Times(0);
    EXPECT_CALL(chargepoint, on_faulted(_, _)).Times(0);
    EXPECT_CALL(chargepoint, on_fault_cleared(_, _)).Times(0);

    const auto error = make_error(INOPERATIVE, Mapping(1, 1));
    ocpp->cb_error_handler(error);
    ocpp->cb_error_cleared_handler(error);
}

TEST_F(GenericOcppErrorDispatch, FaultHandlerReportsEventThenFaulted) {
    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(raised()));
    EXPECT_CALL(chargepoint, on_faulted(1, 1));
    EXPECT_CALL(chargepoint, on_event(cleared()));
    EXPECT_CALL(chargepoint, on_fault_cleared(1, 1));

    const auto error = make_error(INOPERATIVE, Mapping(1, 1));
    ocpp->cb_fault_handler(1, error);
    ocpp->cb_fault_cleared_handler(1, error);
}

TEST_F(GenericOcppErrorDispatch, FaultConnectorDefaultsToOne) {
    EXPECT_CALL(chargepoint, on_event(_));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));

    ocpp->cb_fault_handler(2, make_error(INOPERATIVE, Mapping(2)));
}

TEST_F(GenericOcppErrorDispatch, FaultConnectorFromOrigin) {
    EXPECT_CALL(chargepoint, on_event(_));
    EXPECT_CALL(chargepoint, on_faulted(1, 2));

    // EVSE 1, connector 2
    ocpp->cb_fault_handler(1, make_error(INOPERATIVE, Mapping(1, 2)));
}

// a live fault and its event go to the EVSE the handler was subscribed for, whatever the origin mapping says
TEST_F(GenericOcppErrorDispatch, LiveFaultWithoutOriginMappingUsesSubscribedEvse) {
    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::evse_id, 2)));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));

    ocpp->cb_fault_handler(2, make_error(INOPERATIVE, std::nullopt));
}

TEST_F(GenericOcppErrorDispatch, LiveFaultWithMismatchedMappingUsesSubscribedEvse) {
    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::evse_id, 1)));
    EXPECT_CALL(chargepoint, on_faulted(1, 1));

    ocpp->cb_fault_handler(1, make_error(INOPERATIVE, Mapping(2, 1)));
}

TEST_F(GenericOcppErrorDispatch, OrdinaryErrorNeverFaults) {
    capture_events();
    EXPECT_CALL(chargepoint, on_faulted(_, _)).Times(0);
    EXPECT_CALL(chargepoint, on_fault_cleared(_, _)).Times(0);

    const auto error = make_error(MREC_ERROR, Mapping(1, 1));
    ocpp->cb_error_handler(error);
    ocpp->cb_error_cleared_handler(error);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_FALSE(events[0].event_cleared);
    EXPECT_TRUE(events[1].event_cleared);
}

// only the EVSE part of the origin mapping becomes the event's evse_id; the connector is not used
TEST_F(GenericOcppErrorDispatch, EvseIdFromOrigin) {
    capture_events();

    ocpp->cb_error_handler(make_error(MREC_ERROR, std::nullopt));  // whole charging station
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(2)));    // EVSE 2, no connector
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(1, 2))); // EVSE 1, connector 2

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].evse_id, 0);
    EXPECT_EQ(events[1].evse_id, 2);
    EXPECT_EQ(events[2].evse_id, 1);
}

// every raise and every clear gets its own id
TEST_F(GenericOcppErrorDispatch, EventIdsAreDistinctAndIncreasing) {
    capture_events();

    const auto error_a = make_error(MREC_ERROR, Mapping(1, 1), "error-uuid-a");
    const auto error_b = make_error(MREC_ERROR, Mapping(2, 1), "error-uuid-b");
    ocpp->cb_error_handler(error_a);
    ocpp->cb_error_handler(error_b);
    ocpp->cb_error_cleared_handler(error_a);

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[1].event_id, events[0].event_id + 1);
    EXPECT_EQ(events[2].event_id, events[1].event_id + 1);
}

/// \returns the keys of the custom error mapping entries \p ocpp uses
std::set<std::string> custom_entry_keys(const stubs::GenericOcppTester& ocpp) {
    std::set<std::string> keys;
    if (const auto mapping = ocpp.custom_error_mapping(); mapping != nullptr) {
        for (const auto& [key, entry] : mapping->entries()) {
            keys.insert(key.to_string());
        }
    }
    return keys;
}

/// \brief Writes \p content as the custom error mapping file of \p config; removed again on destruction
class MappingFile {
public:
    MappingFile(stubs::ConfigStub& config, const std::string& content) :
        m_path(std::filesystem::temp_directory_path() /
               ("ocppmulti_" + std::string(testing::UnitTest::GetInstance()->current_test_info()->name()) + ".json")) {
        std::ofstream(m_path) << content;
        config.CustomErrorMappingPath = m_path.string();
    }
    MappingFile(const MappingFile&) = delete;
    MappingFile& operator=(const MappingFile&) = delete;
    ~MappingFile() {
        std::filesystem::remove(m_path);
    }

private:
    std::filesystem::path m_path;
};

// entries that fail a check are reported with warnings and left out, so the built-in mapping applies to their
// errors; the module starts with the remaining entries. A component unknown to the device model is only warned about.
class GenericOcppMisconfiguredMapping : public GenericOcppErrorDispatch {
protected:
    MappingFile file{config, R"({
        "evse_board_support/MREC4OverCurrentFailure": {"v16": {"error_code": "NoSuchCode"}},
        "evse_board_support/MREC5OverVoltage": {"v2": {"component_name": "NoSuchComponent"}},
        "evse_board_support/MREC6UnderVoltage": {"v2": {"component_name": "Known"}}
    })"};
    std::vector<std::string> device_model_lookups;

    GenericOcppMisconfiguredMapping() {
        EXPECT_CALL(chargepoint, get_variables(_)).WillRepeatedly([this](const auto& requests) {
            std::vector<ocpp::v2::GetVariableResult> results;
            for (const auto& request : requests) {
                device_model_lookups.push_back(request.component.name.get());
                ocpp::v2::GetVariableResult result;
                result.attributeStatus = request.component.name.get() == "Known"
                                             ? ocpp::v2::GetVariableStatusEnum::Accepted
                                             : ocpp::v2::GetVariableStatusEnum::UnknownComponent;
                result.component = request.component;
                result.variable = request.variable;
                results.push_back(result);
            }
            return results;
        });
    }
};

TEST_F(GenericOcppMisconfiguredMapping, IgnoresOnlyEntriesWithErrors) {
    EXPECT_THAT(device_model_lookups, ::testing::Contains("NoSuchComponent"));
    EXPECT_EQ(custom_entry_keys(*ocpp),
              (std::set<std::string>{"evse_board_support/MREC5OverVoltage", "evse_board_support/MREC6UnderVoltage"}));
}

class GenericOcppMissingMappingFile : public GenericOcppErrorDispatch {
protected:
    GenericOcppMissingMappingFile() {
        config.CustomErrorMappingPath = "/does/not/exist/custom_error_mapping.json";
    }
};

TEST_F(GenericOcppMissingMappingFile, StartsWithTheBuiltInMapping) {
    EXPECT_EQ(ocpp->custom_error_mapping(), nullptr);
}

class GenericOcppMalformedMappingFile : public GenericOcppErrorDispatch {
protected:
    MappingFile file{config, R"({"evse_board_support/MREC2GroundFailure": )"};
};

TEST_F(GenericOcppMalformedMappingFile, StartsWithTheBuiltInMapping) {
    EXPECT_EQ(ocpp->custom_error_mapping(), nullptr);
}

// same setup as stubs::GenericOcppProvidesTester, but stops after init(): errors raised before
// start() are queued
class GenericOcppNotStarted : public testing::Test {
protected:
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    std::unique_ptr<stubs::ModuleInterfaces> interfaces;
    std::unique_ptr<stubs::GenericOcppTester> ocpp;

    void SetUp() override {
        interfaces = std::make_unique<stubs::ModuleInterfaces>();
        ocpp = std::make_unique<stubs::GenericOcppTester>(chargepoint, interfaces->get_module_info(), config,
                                                          interfaces->get_provides(), interfaces->get_requires());
        interfaces->add_charger_information("info");
        interfaces->add_data_transfer("data_transfer");
        interfaces->add_display_message("display");
        interfaces->add_evse_energy_sink("energy_node", 1);
        interfaces->add_evse_manager("evse_manager_1");
        interfaces->add_evse_manager("evse_manager_2");
        interfaces->add_extensions_15118("evsev2g");
        interfaces->add_reservation("reservation");
        chargepoint.load_store("default_store.json");
        EXPECT_CALL(chargepoint, init(_)).Times(1);
        EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
        EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
            .Times(1);
        EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
        EXPECT_CALL(chargepoint, connect_websocket()).Times(1);
        ocpp->init();
    }

    void TearDown() override {
        interfaces.reset();
        ocpp.reset();
    }

    // replays the queue
    void start() {
        interfaces->publish_ready(0, true);
        interfaces->publish_ready(1, true);
        ocpp->ready(interfaces->get_config_service_client());
    }
};

class GenericOcppErrorQueue : public GenericOcppNotStarted {
protected:
    std::vector<EventInfo> events;

    void SetUp() override {
        EXPECT_CALL(chargepoint, on_event(_)).WillRepeatedly([this](const EventInfo& event) {
            events.push_back(event);
        });
        EXPECT_CALL(chargepoint, on_faulted(_, _)).Times(0);
        EXPECT_CALL(chargepoint, on_fault_cleared(_, _)).Times(0);
        GenericOcppNotStarted::SetUp();
    }
};

// EvseManager Inoperative faults raised before ready() are queued under the EVSE the handler was
// subscribed for and replayed on it, whatever the origin mapping says. libocpp rejects EVSE 0 in
// on_faulted/on_fault_cleared with EvseOutOfRangeException.
using GenericOcppFaultQueue = GenericOcppNotStarted;

TEST_F(GenericOcppErrorQueue, QueuedErrorsAreHeldUntilReady) {
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(1, 1)));
    EXPECT_TRUE(events.empty());

    start();

    ASSERT_EQ(events.size(), 1U);
    EXPECT_FALSE(events[0].event_cleared);
}

TEST_F(GenericOcppErrorQueue, QueuedRaiseAndClearKeepTheirOrder) {
    const auto error = make_error(MREC_ERROR, Mapping(1, 1));
    ocpp->cb_error_handler(error);
    ocpp->cb_error_cleared_handler(error);

    start();

    ASSERT_EQ(events.size(), 2U);
    EXPECT_FALSE(events[0].event_cleared);
    EXPECT_TRUE(events[1].event_cleared);
}

TEST_F(GenericOcppErrorQueue, QueuedInoperativeViaGlobalHandlerIsNotQueued) {
    ocpp->cb_error_handler(make_error(INOPERATIVE, Mapping(1, 1)));

    start();

    EXPECT_TRUE(events.empty());
}

// the queue is kept per EVSE and replayed in EVSE order, not in arrival order
TEST_F(GenericOcppErrorQueue, QueuedErrorsReplayByEvseNotArrival) {
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(2, 1), "error-uuid-evse-2"));
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(1, 1), "error-uuid-evse-1"));
    ocpp->cb_error_handler(make_error(MREC_ERROR, std::nullopt, "error-uuid-station"));

    start();

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].evse_id, 0);
    EXPECT_EQ(events[1].evse_id, 1);
    EXPECT_EQ(events[2].evse_id, 2);
}

// Ids are stamped on arrival, so the per-EVSE replay can send them in decreasing order. OCPP does not
// require eventIds to increase.
TEST_F(GenericOcppErrorQueue, QueuedReplayCanSendDecreasingEventIds) {
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(2, 1), "error-uuid-evse-2"));
    ocpp->cb_error_handler(make_error(MREC_ERROR, Mapping(1, 1), "error-uuid-evse-1"));
    ocpp->cb_error_handler(make_error(MREC_ERROR, std::nullopt, "error-uuid-station"));

    start();

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[1].event_id, events[0].event_id - 1);
    EXPECT_EQ(events[2].event_id, events[1].event_id - 1);
}

TEST_F(GenericOcppFaultQueue, QueuedFaultWithoutOriginMappingFaultsSubscribedEvse) {
    ocpp->cb_fault_handler(2, make_error(INOPERATIVE, std::nullopt));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(raised()));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));
    start();
}

TEST_F(GenericOcppFaultQueue, QueuedFaultClearWithoutOriginMappingClearsSubscribedEvse) {
    const auto error = make_error(INOPERATIVE, std::nullopt);
    ocpp->cb_fault_handler(2, error);
    ocpp->cb_fault_cleared_handler(2, error);

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(raised()));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));
    EXPECT_CALL(chargepoint, on_event(cleared()));
    EXPECT_CALL(chargepoint, on_fault_cleared(2, 1));
    start();
}

TEST_F(GenericOcppFaultQueue, QueuedFaultWithMismatchedMappingFaultsSubscribedEvse) {
    ocpp->cb_fault_handler(1, make_error(INOPERATIVE, Mapping(2, 1)));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(_));
    EXPECT_CALL(chargepoint, on_faulted(1, 1));
    start();
}

TEST_F(GenericOcppFaultQueue, QueuedFaultWithMatchingMapping) {
    ocpp->cb_fault_handler(1, make_error(INOPERATIVE, Mapping(1, 1)));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(_));
    EXPECT_CALL(chargepoint, on_faulted(1, 1));
    start();
}

// the reported event carries the subscribed EVSE, as on the live path
TEST_F(GenericOcppFaultQueue, QueuedFaultCarriesSubscribedEvseInEvent) {
    ocpp->cb_fault_handler(2, make_error(INOPERATIVE, std::nullopt));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::evse_id, 2)));
    EXPECT_CALL(chargepoint, on_faulted(_, _));
    start();
}

} // namespace
