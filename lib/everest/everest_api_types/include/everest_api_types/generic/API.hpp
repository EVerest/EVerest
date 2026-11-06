// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::generic {

enum class ErrorEnum {
    CommunicationFault,
    VendorError,
    VendorWarning
};

/// \brief The value that caused an error
struct TriggerValue {
    /// text representation of the value as the raising entity would print it, truncated to 2500 characters
    /// when raised
    std::optional<std::string> text_repr;
};

struct Error {
    ErrorEnum type;
    std::optional<std::string> sub_type;
    std::optional<std::string> message;
    std::optional<TriggerValue> trigger_value;
};

struct RequestReply {
    std::optional<std::string> replyTo;
    std::string payload;
};

} // namespace everest::lib::API::V1_0::types::generic
