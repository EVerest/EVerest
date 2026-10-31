// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/variant.hpp>

namespace iso15118::message_20 {

// The request's type, or the response's when the request decoder does not handle the frame. Logs one error
// when neither decoder does.
Type received_message_type(const RequestVariant& request, v2gtp::PayloadType payload_type,
                           const io::StreamInputView& view);

} // namespace iso15118::message_20
