// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include "helper.hpp"

#include <iso15118/io/stream_view.hpp>

iso15118::ev::d20::Context& FsmStateHelper::get_context() {
    return ctx;
}

DecodedRequests drain_requests(ev::d20::MessageExchange& msg_exch) {
    DecodedRequests decoded;
    while (msg_exch.has_request()) {
        auto taken = msg_exch.take_request();
        if (not taken.has_value()) {
            break;
        }
        const auto& [bytes, payload_type] = *taken;
        decoded.add(
            std::make_unique<message_20::Variant>(payload_type, io::StreamInputView{bytes.data(), bytes.size()}));
    }
    return decoded;
}
