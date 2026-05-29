// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/session.hpp>
#include <iso15118/message/d20/schedule_exchange.hpp>

#include <cstdint>
#include <ctime>
#include <optional>

#include <iso15118/d20/dynamic_mode_parameters.hpp>

namespace iso15118::d20::state {

msg::d20::ScheduleExchangeResponse handle_request(const msg::d20::ScheduleExchangeRequest& req,
                                                    const d20::Session& session,
                                                    const msg::d20::datatypes::RationalNumber& max_power,
                                                    const UpdateDynamicModeParameters& dynamic_parameters,
                                                    bool timeout_reached);

} // namespace iso15118::d20::state
