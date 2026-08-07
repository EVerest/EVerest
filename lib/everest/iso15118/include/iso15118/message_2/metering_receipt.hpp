// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include "common_types.hpp"

namespace iso15118::message_2 {

// ISO 15118-2 MeteringReceiptReq. In Plug-and-Charge the whole MeteringReceiptReq element is signed
// (xmldsig, referenced by its Id attribute) with the contract certificate key; the raw request EXI is
// re-decoded for signature verification (see crypto::verify_metering_receipt_signature), so the
// signature itself is not modelled in this wrapper (mirrors AuthorizationRequest).
struct MeteringReceiptRequest {
    Header header;
    // Body-level SessionID (part of the signed MeteringReceiptReq element, distinct from the message
    // header SessionID). Decoded so the SECC can verify it matches the assigned session id [V2G2-909].
    datatypes::SessionId session_id{};
    datatypes::MeterInfo meter_info;
    std::optional<uint8_t> sa_schedule_tuple_id;
};

struct MeteringReceiptResponse {
    Header header;
    datatypes::ResponseCode response_code;
    // Exactly one is populated, matching the session's energy-transfer mode (DC vs AC).
    std::optional<datatypes::AC_EVSEStatus> ac_evse_status;
    std::optional<datatypes::DC_EVSEStatus> dc_evse_status;
};

} // namespace iso15118::message_2
