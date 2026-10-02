// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "helper.hpp"

#include <iso15118/io/stream_view.hpp>

ev::din::Context& DinStateHelper::get_context() {
    return ctx;
}

DecodedRequests take_all_requests(ev::din::MessageExchange& msg_exch) {
    DecodedRequests decoded;
    while (msg_exch.has_request()) {
        auto taken = msg_exch.take_request();
        if (not taken.has_value()) {
            break;
        }
        const auto& bytes = taken->first;
        decoded.add(std::make_unique<message_din::Variant>(io::StreamInputView{bytes.data(), bytes.size()}));
    }
    return decoded;
}
