// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "helpers/Conversions.hpp"

namespace {

using types::evse_manager::SessionEventEnum;
using types::json_rpc_api::EVSEStateEnum;

EVSEStateEnum state_after(SessionEventEnum event) {
    types::evse_manager::SessionEvent session_event;
    session_event.event = event;
    return types::json_rpc_api::evse_manager_session_event_to_evse_state(session_event);
}

TEST(Conversions, resumed_transaction_is_preparing) {
    EXPECT_EQ(state_after(SessionEventEnum::TransactionResumed), EVSEStateEnum::Preparing);
}

TEST(Conversions, started_transaction_is_preparing) {
    EXPECT_EQ(state_after(SessionEventEnum::TransactionStarted), EVSEStateEnum::Preparing);
}

} // namespace
