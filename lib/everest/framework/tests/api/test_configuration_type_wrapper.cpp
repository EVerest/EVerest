// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Unit tests for the enum mappings in configuration_type_wrapper.cpp.
//
// The wrapper only converts internal -> external for these enums (the manager answers requests, it
// never reads one back), so every check here is one-way by necessity. That also happens to be the
// stronger check: a round-trip stays green when both switch directions carry the same copy-paste
// error, which is the mistake worth catching.
//
// Only the values the integration suite (tests/management_api_tests) cannot produce need to be here;
// the ones it does produce are covered end-to-end on the real wire. The full tables are pinned anyway
// because the cost is a line each and a wrong mapping is otherwise only visible to a client.

#include <catch2/catch_all.hpp>

#include <configuration_type_wrapper.hpp>

namespace wrapper = Everest::api::types::configuration;
namespace ext = everest::lib::API::V1_0::types::configuration;

TEST_CASE("configuration_type_wrapper enum mappings", "[configuration][type_wrapper]") {

    SECTION("MarkActiveSlotResultEnum") {
        using SetSlot = Everest::config::SetActiveSlotStatus;
        CHECK(wrapper::to_external_api(SetSlot::Success) == ext::MarkActiveSlotResultEnum::Success);
        // NoChangeRequired and Failed are never produced by the integration suite
        CHECK(wrapper::to_external_api(SetSlot::NoChangeRequired) == ext::MarkActiveSlotResultEnum::NoChangeRequired);
        CHECK(wrapper::to_external_api(SetSlot::DoesNotExist) == ext::MarkActiveSlotResultEnum::DoesNotExist);
        CHECK(wrapper::to_external_api(SetSlot::Failed) == ext::MarkActiveSlotResultEnum::Failed);
    }

    SECTION("DeleteSlotResultEnum") {
        using DeleteSlot = Everest::config::DeleteSlotStatus;
        CHECK(wrapper::to_external_api(DeleteSlot::Success) == ext::DeleteSlotResultEnum::Success);
        CHECK(wrapper::to_external_api(DeleteSlot::CannotDeleteActiveSlot) ==
              ext::DeleteSlotResultEnum::CannotDeleteActiveSlot);
        CHECK(wrapper::to_external_api(DeleteSlot::DoesNotExist) == ext::DeleteSlotResultEnum::DoesNotExist);
        // Failed is never produced by the integration suite
        CHECK(wrapper::to_external_api(DeleteSlot::Failed) == ext::DeleteSlotResultEnum::Failed);
    }

    SECTION("ConfigurationParameterUpdateResultEnum") {
        using UpdateResult = Everest::config::SetConfigParameterResultEnum;
        CHECK(wrapper::to_external_api(UpdateResult::Applied) == ext::ConfigurationParameterUpdateResultEnum::Applied);
        CHECK(wrapper::to_external_api(UpdateResult::WillApplyOnRestart) ==
              ext::ConfigurationParameterUpdateResultEnum::WillApplyOnRestart);
        CHECK(wrapper::to_external_api(UpdateResult::Rejected) ==
              ext::ConfigurationParameterUpdateResultEnum::Rejected);
        // DoesNotExist, RetryLater and AccessDenied are never produced by the integration suite
        CHECK(wrapper::to_external_api(UpdateResult::DoesNotExist) ==
              ext::ConfigurationParameterUpdateResultEnum::DoesNotExist);
        CHECK(wrapper::to_external_api(UpdateResult::RetryLater) ==
              ext::ConfigurationParameterUpdateResultEnum::RetryLater);
        CHECK(wrapper::to_external_api(UpdateResult::AccessDenied) ==
              ext::ConfigurationParameterUpdateResultEnum::AccessDenied);
    }

    SECTION("ActiveSlotStatusEnum") {
        using Status = Everest::config::ActiveSlotStatus;
        CHECK(wrapper::to_external_api(Status::Running) == ext::ActiveSlotStatusEnum::Running);
        CHECK(wrapper::to_external_api(Status::Stopped) == ext::ActiveSlotStatusEnum::Stopped);
        CHECK(wrapper::to_external_api(Status::Starting) == ext::ActiveSlotStatusEnum::Starting);
        CHECK(wrapper::to_external_api(Status::Stopping) == ext::ActiveSlotStatusEnum::Stopping);
        // FailedToStart needs a config that fails to boot, which the integration suite never sets up
        CHECK(wrapper::to_external_api(Status::FailedToStart) == ext::ActiveSlotStatusEnum::FailedToStart);
        CHECK(wrapper::to_external_api(Status::RestartTriggered) == ext::ActiveSlotStatusEnum::RestartTriggered);
    }

    SECTION("ConfigurationParameterDatatype") {
        using Datatype = everest::config::Datatype;
        CHECK(wrapper::to_external_api(Datatype::Integer) == ext::ConfigurationParameterDatatype::Integer);
        CHECK(wrapper::to_external_api(Datatype::Decimal) == ext::ConfigurationParameterDatatype::Decimal);
        CHECK(wrapper::to_external_api(Datatype::String) == ext::ConfigurationParameterDatatype::String);
        CHECK(wrapper::to_external_api(Datatype::Boolean) == ext::ConfigurationParameterDatatype::Boolean);
        CHECK(wrapper::to_external_api(Datatype::Unknown) == ext::ConfigurationParameterDatatype::Unknown);
    }

    SECTION("ConfigurationParameterMutability") {
        using Mutability = everest::config::Mutability;
        CHECK(wrapper::to_external_api(Mutability::ReadOnly) == ext::ConfigurationParameterMutability::ReadOnly);
        CHECK(wrapper::to_external_api(Mutability::ReadWrite) == ext::ConfigurationParameterMutability::ReadWrite);
        CHECK(wrapper::to_external_api(Mutability::WriteOnly) == ext::ConfigurationParameterMutability::WriteOnly);
    }

    SECTION("GetConfigurationStatusEnum") {
        using GetStatus = Everest::config::GetConfigurationStatus;
        CHECK(wrapper::to_external_api(GetStatus::Success) == ext::GetConfigurationStatusEnum::Success);
        // SlotDoesNotExist and Failed are never produced by the integration suite
        CHECK(wrapper::to_external_api(GetStatus::SlotDoesNotExist) ==
              ext::GetConfigurationStatusEnum::SlotDoesNotExist);
        CHECK(wrapper::to_external_api(GetStatus::Failed) == ext::GetConfigurationStatusEnum::Failed);
    }
}
