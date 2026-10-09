// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/metering_receipt.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace metering_receipt {

// The MeterInfo comes from the charge-loop response that set ReceiptRequired; the body-level SessionID
// repeats the header one [V2G2-909].
message_2::MeteringReceiptRequest create_request(const dt::SessionId& session_id, const dt::MeterInfo& meter_info,
                                                 std::optional<uint8_t> sa_schedule_tuple_id);

} // namespace metering_receipt

} // namespace iso15118::ev::d2::state
