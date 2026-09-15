// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/cable_check.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace cable_check {

message_2::CableCheckRequest create_request(const dt::DC_EVStatus& dc_ev_status);

struct Result {
    // EVSEProcessing is Finished; otherwise the isolation test poll repeats.
    bool finished{false};
    // Valid only when finished. [V2G2-880]: status codes without explicit requirements are
    // informational and shall not influence the charging process, and [V2G2-525] makes PreChargeReq
    // a shall once EVSEProcessing is Finished. Only a shutdown stops the session, as on the DIN side.
    bool evse_shutdown{false};
    // Valid only when finished. Deliberately stricter than ISO 15118-2, which marks
    // EVSEIsolationStatus optional (Table 96) and delegates its handling to IEC CDV 61851-23, so an
    // absent element is permitted on the wire. We refuse to close the contactor on a charger that
    // never reported a non-fault isolation result.
    bool isolation_ok{false};
};

Result handle_response(const message_2::CableCheckResponse& res);

} // namespace cable_check

} // namespace iso15118::ev::d2::state
