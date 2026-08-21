// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Drives LifecycleAPI through its registered MQTT handlers and its config-service status
// callback, backed by a FakeConfigService and the recording MockMQTTAbstraction.

#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <lifecycle_api.hpp>
#include <tests/mock_mqtt_abstraction.hpp>

#include "api_test_helpers.hpp"
#include "fake_config_service.hpp"

using json = nlohmann::json;
using namespace Everest::tests;
using Everest::api::lifecycle::ConfigurationApiStatus;
using Everest::api::lifecycle::LifecycleAPI;
using ActiveSlotStatus = Everest::config::ActiveSlotStatus;

namespace {

const std::string STATUS_TOPIC = "everest_api/1/lifecycle/e2m/status";

std::string command_topic(const std::string& command) {
    return "everest_api/1/lifecycle/m2e/" + command;
}

void invoke_command(MockMQTTAbstraction& mock, const std::string& command, const json& request) {
    invoke(mock, command_topic(command), request);
}

Everest::config::ActiveSlotUpdate make_slot_update(ActiveSlotStatus status) {
    Everest::config::ActiveSlotUpdate update;
    update.timestamp = "2026-07-31T00:00:00Z";
    update.active_slot_id = 0;
    update.next_boot_slot_id = std::nullopt;
    update.status = status;
    return update;
}

} // namespace

// Forwarding the stop_fn/restart_fn result and the read-only rejections are covered end-to-end by
// tests/management_api_tests/lifecycle_api_tests.py, which sees more of the result enum than a fake
// can (Stopping, NoModulesToStop, Starting, Restarting, Rejected) and additionally asserts via the
// manager status fifo that a rejected command left the modules alone. Only the fallback below has no
// counterpart there: the manager always supplies both callbacks.
TEST_CASE("LifecycleAPI stop_modules without a stop_fn falls back to Rejected", "[lifecycle_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;
    LifecycleAPI api(mock, svc, ConfigurationApiStatus::AvailableRW, /*readonly=*/false);
    mock.clear_published();

    invoke_command(mock, "stop_modules", make_request(json::object()));

    CHECK(last_reply(mock).at("status") == "Rejected");
}

TEST_CASE("LifecycleAPI maps every ActiveSlotStatus to the client-visible execution status", "[lifecycle_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;
    LifecycleAPI api(mock, svc, ConfigurationApiStatus::AvailableRW, /*readonly=*/false);
    REQUIRE(svc.active_slot_handler);
    mock.clear_published();

    // Ordered so each status differs from the previous one (the handler drops repeats, and it
    // starts from an internal 'Stopped'); this way every mapping is actually published.
    const std::vector<std::pair<ActiveSlotStatus, std::string>> sequence = {
        {ActiveSlotStatus::Starting, "Starting"},           {ActiveSlotStatus::Running, "Running"},
        {ActiveSlotStatus::Stopping, "Stopping"},           {ActiveSlotStatus::Stopped, "NotRunning"},
        {ActiveSlotStatus::FailedToStart, "FailedToStart"}, {ActiveSlotStatus::RestartTriggered, "RestartTriggered"},
    };

    for (const auto& [internal_status, expected] : sequence) {
        const auto before = mock.published().size();
        svc.active_slot_handler(make_slot_update(internal_status));
        REQUIRE(mock.published().size() == before + 1);

        const auto& record = mock.publish_records().back();
        CHECK(record.topic == STATUS_TOPIC);
        const auto status = json::parse(record.payload.get<std::string>());
        CHECK(status.at("module_status") == expected);
        CHECK(status.at("everest_running") == true);
    }
}

TEST_CASE("LifecycleAPI status callback drops repeated statuses", "[lifecycle_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;
    LifecycleAPI api(mock, svc, ConfigurationApiStatus::AvailableRW, /*readonly=*/false);
    REQUIRE(svc.active_slot_handler);
    mock.clear_published();

    svc.active_slot_handler(make_slot_update(ActiveSlotStatus::Running));
    REQUIRE(mock.published().size() == 1);
    // Same status again -> no additional publish.
    svc.active_slot_handler(make_slot_update(ActiveSlotStatus::Running));
    CHECK(mock.published().size() == 1);
}

TEST_CASE("LifecycleAPI status publishes are retained but not recorded as retained", "[lifecycle_api]") {
    MockMQTTAbstraction mock("everest/");
    FakeConfigService svc;

    SECTION("the initial status published from the constructor") {
        // AvailableRO here and AvailableRW in the section below, so the two sections together
        // check that each instance reports its own configuration-API status.
        LifecycleAPI api(mock, svc, ConfigurationApiStatus::AvailableRO, /*readonly=*/true);

        REQUIRE_FALSE(mock.publish_records().empty());
        const auto& record = mock.publish_records().front();
        CHECK(record.topic == STATUS_TOPIC);
        CHECK(record.retain == true);
        CHECK(record.record_retained == false);

        const auto status = json::parse(record.payload.get<std::string>());
        CHECK(status.at("everest_running") == true);
        CHECK(status.at("module_status") == "NotRunning");
        CHECK(status.at("lifecycle_api_ro") == true);
        CHECK(status.at("configuration_api_available") == "RO");
    }

    SECTION("a status published from the config-service callback") {
        LifecycleAPI api(mock, svc, ConfigurationApiStatus::AvailableRW, /*readonly=*/false);
        REQUIRE(svc.active_slot_handler);
        mock.clear_published();

        svc.active_slot_handler(make_slot_update(ActiveSlotStatus::Running));

        REQUIRE(mock.publish_records().size() == 1);
        const auto& record = mock.publish_records().back();
        CHECK(record.retain == true);
        CHECK(record.record_retained == false);
        const auto status = json::parse(record.payload.get<std::string>());
        CHECK(status.at("lifecycle_api_ro") == false);
        CHECK(status.at("configuration_api_available") == "RW");
    }
}

TEST_CASE("LifecycleAPI not-running status: last will and shutdown publish", "[lifecycle_api]") {
    CHECK(LifecycleAPI::Lwt::get_topic() == STATUS_TOPIC);

    const auto lwt = json::parse(LifecycleAPI::Lwt::get_data());
    CHECK(lwt.at("everest_running") == false);
    CHECK_FALSE(lwt.contains("module_status"));

    MockMQTTAbstraction mock;
    LifecycleAPI::publish_shutdown_status(mock);

    REQUIRE(mock.publish_records().size() == 1);
    const auto& record = mock.publish_records().back();
    CHECK(record.topic == STATUS_TOPIC);
    CHECK(record.qos == Everest::QOS::QOS2);
    CHECK(record.retain == true);
    // as for every status update: must survive the manager's clear_retained_topics()
    CHECK(record.record_retained == false);

    // byte-identical to the last will, so a subscriber cannot tell a clean manager shutdown from
    // an unclean death - which is what lets the manager fall back to the will on the exit paths
    // that never reach disconnect_mqtt()
    CHECK(record.payload.get<std::string>() == LifecycleAPI::Lwt::get_data());
}
