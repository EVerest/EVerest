// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/context.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/variant.hpp>

namespace iso15118::d2::state {

// Answered inside the charge loop, which stays where it is. \p variant is needed for the signed EXI
// payload the Plug-and-Charge signature check runs over. The header SessionID was already validated
// by StateBase::feed(); the body SessionID checked here is the separate signed field [V2G2-909].
// \p is_dc selects the EVSE status block; ChargingStatus is AC-only, CurrentDemand DC-only.
void handle_metering_receipt(Context& ctx, const message_2::Variant& variant,
                             const message_2::MeteringReceiptRequest& req, bool is_dc);

} // namespace iso15118::d2::state
