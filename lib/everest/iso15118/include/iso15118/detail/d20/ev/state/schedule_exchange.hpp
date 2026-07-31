// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message/schedule_exchange.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace schedule_exchange {

// Dynamic/Scheduled request parameters (see flow spec §3 ScheduleExchange).
struct Params {
    uint16_t max_supporting_points{1024};
    uint32_t departure_time{7200};
    std::optional<dt::PercentValue> minimum_soc{std::nullopt};
    std::optional<dt::PercentValue> target_soc{std::nullopt};
    dt::RationalNumber target_energy{};
    dt::RationalNumber max_energy{};
    dt::RationalNumber min_energy{};
    std::optional<dt::RationalNumber> max_v2x_energy{std::nullopt};
    std::optional<dt::RationalNumber> min_v2x_energy{std::nullopt};
};

message_20::ScheduleExchangeRequest create_dynamic_request(const Params& params);
message_20::ScheduleExchangeRequest create_scheduled_request(const Params& params);

struct Result {
    bool valid{false};
    bool finished{false};
};

Result handle_response(const message_20::ScheduleExchangeResponse& res);

} // namespace schedule_exchange

} // namespace iso15118::d20::ev::state
