// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <gmock/gmock.h>
#include <ocpp/v2/functional_blocks/firmware_update.hpp>

namespace ocpp::v2 {
class FirmwareUpdateMock : public FirmwareUpdateInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, on_firmware_update_status_notification, (std::int32_t, const FirmwareStatusEnum&, const bool),
                (override));
    MOCK_METHOD(void, on_firmware_status_notification_request, (), (override));
    MOCK_METHOD(void, on_transaction_finished, (), (override));
    MOCK_METHOD(void, on_registration_accepted, (), (override));
};
} // namespace ocpp::v2
