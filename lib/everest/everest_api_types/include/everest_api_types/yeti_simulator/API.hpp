// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::yeti_simulator {

enum class Severity {
    Low,
    Medium,
    High,
};

struct RaiseError {
    std::string type;
    std::optional<std::string> sub_type;
    std::optional<std::string> message;
    std::optional<Severity> severity;
};

struct ClearError {
    std::string type;
    std::optional<std::string> sub_type;
};

} // namespace everest::lib::API::V1_0::types::yeti_simulator
