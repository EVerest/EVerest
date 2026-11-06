// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/v2gtp/sdp.hpp>

#include "type.hpp"

namespace iso15118::message_20 {

// Decodes one ISO 15118-20 or SupportedAppProtocol response, for either role. Empty when the payload type
// is not the response's, the EXI does not decode, it carries another message or its body does not convert.
// Defined for every response.
template <typename Message> std::optional<Message> decode(v2gtp::PayloadType, const io::StreamInputView&);

// The type of the message the EXI carries, without converting its body: every request and response the
// stack decodes, Type::None for any other message, an undecodable frame or another payload type.
Type peek_type(v2gtp::PayloadType, const io::StreamInputView&);

} // namespace iso15118::message_20
