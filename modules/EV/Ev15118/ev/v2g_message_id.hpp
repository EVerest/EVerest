// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <generated/types/iso15118.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace module {

types::iso15118::V2gMessageId v2g_message_id(const iso15118::V2gMessageType& type);

} // namespace module
