// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "helper.hpp"

#include <array>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

iso15118::ev::d20::Context& FsmStateHelper::get_context() {
    return ctx;
}

DecodedRequests take_all_requests(ev::d20::MessageExchange& msg_exch) {
    DecodedRequests decoded;
    while (msg_exch.has_request()) {
        auto taken = msg_exch.take_request();
        if (not taken.has_value()) {
            break;
        }
        const auto& [bytes, payload_type] = taken.value();
        decoded.add(std::make_unique<message_20::RequestVariant>(payload_type,
                                                                 io::StreamInputView{bytes.data(), bytes.size()}));
    }
    return decoded;
}

std::unique_ptr<message_20::ResponseVariant> make_sap_ok_response() {
    const message_20::SupportedAppProtocolResponse res{
        message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
    std::array<uint8_t, 256> buffer{};
    io::StreamOutputView out({buffer.data(), buffer.size()});
    const auto size = message_20::serialize(res, out);
    return std::make_unique<message_20::ResponseVariant>(v2gtp::PayloadType::SAP,
                                                         io::StreamInputView{buffer.data(), size});
}
