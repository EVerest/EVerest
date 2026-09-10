// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/context.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

template <typename Response> Response& response_with_code(Response& res, dt::ResponseCode code) {
    res.response_code = code;
    return res;
}

inline void setup_header(message_din::Header& header, const dt::SessionId& session_id) {
    header.session_id = session_id;
}

// So the EV sees the real level. CableCheckRes applies the same precedence inside its own builder,
// which has three legs with different fallbacks, rather than through this helper.
inline void apply_isolation_status(const Context& ctx, dt::DcEvseStatus& status) {
    if (const auto level = ctx.reported_isolation_level()) {
        status.evse_isolation_status = level.value();
    }
}

} // namespace iso15118::din::state
