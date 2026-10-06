// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::error_history {

enum class State {
    Active,
    ClearedByModule,
    ClearedByReboot,
};

enum class SeverityFilter {
    HIGH_GE,
    MEDIUM_GE,
    LOW_GE,
};

enum class Severity {
    High,
    Medium,
    Low,
};

struct Mapping {
    int32_t evse;
    std::optional<int32_t> connector;
};

struct ImplementationIdentifier {
    std::string module_id;
    std::string implementation_id;
    std::optional<Mapping> mapping;
};

struct TimeperiodFilter {
    std::string timestamp_from;
    std::string timestamp_to;
};

struct FilterArguments {
    std::optional<State> state_filter;
    std::optional<ImplementationIdentifier> origin_filter;
    std::optional<std::string> type_filter;
    std::optional<SeverityFilter> severity_filter;
    std::optional<TimeperiodFilter> timeperiod_filter;
    std::optional<std::string> handle_filter;
};

struct ErrorObject {
    std::string type;
    std::string description;
    std::string message;
    Severity severity;
    ImplementationIdentifier origin;
    std::string timestamp;
    std::string uuid;
    State state;
    std::optional<std::string> sub_type;
};

struct ErrorList {
    std::vector<ErrorObject> errors;
};

} // namespace everest::lib::API::V1_0::types::error_history
