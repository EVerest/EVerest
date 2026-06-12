// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "gmock/gmock.h"

#include <ocpp/v2/functional_blocks/smart_charging.hpp>
#include <ocpp/v2/messages/SetChargingProfile.hpp>

namespace ocpp::v2 {
class SmartChargingMock : public SmartChargingInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message));
    MOCK_METHOD(std::vector<EnhancedCompositeSchedule>, get_all_composite_schedules,
                (const std::int32_t duration, const ChargingRateUnitEnum& unit));
    MOCK_METHOD(void, delete_transaction_tx_profiles, (const std::string& transaction_id));
    MOCK_METHOD(SetChargingProfileResponse, conform_validate_and_add_profile,
                (ChargingProfile & profile, std::int32_t evse_id, CiString<20> charging_limit_source,
                 AddChargingProfileSource source_of_request));
    MOCK_METHOD(ProfileValidationResultEnum, conform_and_validate_profile,
                (ChargingProfile & profile, std::int32_t evse_id, AddChargingProfileSource source_of_request));
    MOCK_METHOD(EnhancedCompositeScheduleResponse, get_composite_schedule,
                (const GetCompositeScheduleRequest& request));
    MOCK_METHOD(std::optional<EnhancedCompositeSchedule>, get_composite_schedule,
                (std::int32_t evse_id, std::chrono::seconds duration, ChargingRateUnitEnum unit));
    MOCK_METHOD(void, notify_ev_charging_needs_req, (const NotifyEVChargingNeedsRequest& req));
};
} // namespace ocpp::v2
