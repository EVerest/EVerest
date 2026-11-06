// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <generated/types/iso15118.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace module::conversions {

// The interface name of a libiso15118 message type. A type with no name maps to UnknownMessage.
types::iso15118::V2gMessageId to_v2g_message_id(iso15118::message_20::Type type);
types::iso15118::V2gMessageId to_v2g_message_id(iso15118::message_2::Type type);
types::iso15118::V2gMessageId to_v2g_message_id(iso15118::message_din::Type type);
types::iso15118::V2gMessageId to_v2g_message_id(const iso15118::V2gMessageType& type);

} // namespace module::conversions
