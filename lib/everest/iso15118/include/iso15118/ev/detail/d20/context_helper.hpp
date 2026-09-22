// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/detail/context_helper.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/response_variant.hpp>

namespace iso15118::ev::d20 {

bool check_response_code(message_20::datatypes::ResponseCode response_code);

using ev::session::disposition_violation;

// Validate an inbound response: the expected variant, a session id matching the
// active session, and an accepted response code.
// On failure, logs it unless the decoder already did, stops the session and returns nullptr; the
// caller then returns Result::stopping().
// Collapses the per-state validation skeleton and makes the session-id check impossible
// to omit.
template <typename Res> const Res* expect_response(Context& ctx, const message_20::ResponseVariant& variant) {
    const auto* res = expect_response_type<Res>(ctx, variant);
    if (res == nullptr) {
        return nullptr;
    }

    if (res->header.session_id != ctx.get_session().get_id()) {
        logf_error("Response session_id does not match the active session");
        ctx.stop_session();
        return nullptr;
    }

    const auto code = res->response_code;
    if (not check_response_code(code)) {
        logf_error("Response rejected with response_code: %d", static_cast<int>(code));
        ctx.stop_session();
        return nullptr;
    }

    return res;
}

} // namespace iso15118::ev::d20
