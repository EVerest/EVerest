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
struct CausedByValue {
    /// the value itself, at most 2500 characters
    std::optional<std::string> actual_value;
};

struct Error {
    ErrorEnum type;
    std::optional<std::string> sub_type;
    std::optional<std::string> message;
    std::optional<CausedByValue> caused_by_value;
};

struct RequestReply {
    std::string replyTo;
    std::string payload;
};

} // namespace everest::lib::API::V1_0::types::generic
