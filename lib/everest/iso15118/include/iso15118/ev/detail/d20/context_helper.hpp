// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <algorithm>
#include <initializer_list>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/variant.hpp>

namespace iso15118::ev::d20 {

void setup_header(message_20::Header&, const SessionId&);

bool check_response_code(message_20::datatypes::ResponseCode response_code);

// Validate an inbound response: the expected variant, a session id matching the
// active session, and an accepted response code (check_response_code plus any code
// in extra_ok). On any failure it logs, stops the session, and returns nullptr; the
// caller returns {} straight away. Collapses the per-state validation skeleton and
// makes the session-id check impossible to omit.
template <typename Res>
const Res* expect_response(Context& ctx, const message_20::Variant& variant,
                           std::initializer_list<message_20::datatypes::ResponseCode> extra_ok = {}) {
    const auto* res = variant.get_if<Res>();
    if (res == nullptr) {
        logf_error("Unexpected response variant, got message type id: %d", static_cast<int>(variant.get_type()));
        ctx.stop_session();
        return nullptr;
    }

    if (res->header.session_id != ctx.get_session().get_id()) {
        logf_error("Response session_id does not match the active session");
        ctx.stop_session();
        return nullptr;
    }

    const auto code = res->response_code;
    const bool accepted =
        check_response_code(code) or (std::find(extra_ok.begin(), extra_ok.end(), code) != extra_ok.end());
    if (not accepted) {
        logf_error("Response rejected with response_code: %d", static_cast<int>(code));
        ctx.stop_session();
        return nullptr;
    }

    return res;
}

} // namespace iso15118::ev::d20
