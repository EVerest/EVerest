// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string_view>

#include <iso15118/detail/helper.hpp>

namespace iso15118::message_2 {
class ResponseVariant;
}
namespace iso15118::message_din {
class ResponseVariant;
}
namespace iso15118::message_20 {
class ResponseVariant;
}

namespace iso15118::ev {

// A message type id indexes the enum of its own protocol.
inline std::string_view protocol_name(const message_2::ResponseVariant&) {
    return "ISO 15118-2";
}
inline std::string_view protocol_name(const message_din::ResponseVariant&) {
    return "DIN 70121";
}
inline std::string_view protocol_name(const message_20::ResponseVariant&) {
    return "ISO 15118-20";
}

// Stop on decode or type failure. The decoder already logged decode failures.
template <typename Res, typename Context, typename Variant>
const Res* expect_response_type(Context& ctx, const Variant& variant) {
    if (not variant.get_error().empty()) {
        ctx.stop_session();
        return nullptr;
    }
    const auto* res = variant.template get_if<Res>();
    if (res == nullptr) {
        const auto protocol = protocol_name(variant);
        logf_error("Unexpected %.*s response, got message type id: %d", static_cast<int>(protocol.size()),
                   protocol.data(), static_cast<int>(variant.get_type()));
        ctx.stop_session();
    }
    return res;
}

} // namespace iso15118::ev
