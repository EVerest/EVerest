// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/d2/variant.hpp>

#include <cassert>
#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

#include <iso15118/message/d2/session_setup.hpp>
#include <iso15118/message/d2/session_stop.hpp>

namespace iso15118::msg::d2 {

struct Variant::Impl {
    std::variant<SessionSetupRequest, SessionSetupResponse, SessionStopRequest, SessionStopResponse> data;
};

Variant::Variant() : pimpl(std::make_unique<Impl>()) {
}

Variant::~Variant() = default;

Variant::Variant(const io::StreamInputView& buffer_view) : pimpl(std::make_unique<Impl>()) {
    auto input_stream = get_exi_input_stream(buffer_view);

    iso2_exiDocument doc{};

    const auto decode_status = decode_iso2_exiDocument(&input_stream, &doc);

    if (decode_status != 0) {
        // va.error = "decode_appHand_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.V2G_Message.Body.SessionSetupReq_isUsed) {
        auto& msg = pimpl->data.emplace<msg::d2::SessionSetupRequest>();
        type = msg::d2::Type::SessionSetupReq;
        convert(doc.V2G_Message.Header, msg.header);
        convert(doc.V2G_Message.Body.SessionSetupReq, msg);
    } else if (doc.V2G_Message.Body.SessionSetupRes_isUsed) {
        auto& msg = pimpl->data.emplace<msg::d2::SessionSetupResponse>();
        type = msg::d2::Type::SessionSetupRes;
        convert(doc.V2G_Message.Header, msg.header);
        // convert(doc.V2G_Message.Body.SessionSetupRes, msg);
    } else {
        // va.error = "chosen message type unhandled";
    }
}

template <typename MessageType>
Variant::Variant(MessageType&& message, Type type_) :
    pimpl(std::make_unique<Impl>(Impl{std::forward<MessageType>(message)})), type(type_) {
}

template <typename T> T Variant::get() {
    return std::get<T>(pimpl->data);
}

template <typename T> T* Variant::get_if() {
    return std::get_if<T>(&pimpl->data);
}

template SessionSetupRequest Variant::get<SessionSetupRequest>();
template SessionSetupRequest* Variant::get_if<SessionSetupRequest>();

template SessionSetupResponse Variant::get<SessionSetupResponse>();
template SessionSetupResponse* Variant::get_if<SessionSetupResponse>();

template SessionStopRequest Variant::get<SessionStopRequest>();
template SessionStopRequest* Variant::get_if<SessionStopRequest>();

template SessionStopResponse Variant::get<SessionStopResponse>();
template SessionStopResponse* Variant::get_if<SessionStopResponse>();

template Variant::Variant(SessionSetupRequest&&, Type);
template Variant::Variant(SessionSetupResponse&&, Type);
template Variant::Variant(SessionStopRequest&&, Type);
template Variant::Variant(SessionStopResponse&&, Type);

Type Variant::get_type() const {
    return type;
}

} // namespace iso15118::msg::d2
