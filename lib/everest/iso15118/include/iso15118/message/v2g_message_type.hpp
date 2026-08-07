// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

#include <iso15118/message/type.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_din/type.hpp>

namespace iso15118 {

// Protocol-neutral V2G message identifier reported through the feedback callbacks. It carries the real
// message type of whichever protocol generation is running (ISO 15118-20, ISO 15118-2 or DIN SPEC
// 70121) so the module can map it to the correct name instead of collapsing pre-20 messages to
// "UnknownMessage". A bare message_20::Type / message_2::Type / message_din::Type converts implicitly.
using V2gMessageType = std::variant<message_20::Type, message_2::Type, message_din::Type>;

} // namespace iso15118
