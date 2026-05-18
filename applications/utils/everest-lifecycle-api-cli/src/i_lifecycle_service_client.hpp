// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <optional>
#include <string>

#include <everest_api_types/lifecycle/codec.hpp>

namespace API_types_lc = everest::lib::API::V1_0::types::lifecycle;

namespace everest::lifecycle_cli {

class ILifecycleServiceClient {
public:
    virtual ~ILifecycleServiceClient() = default;

    virtual std::optional<API_types_lc::StopModulesResult> stop_modules() = 0;
    virtual std::optional<API_types_lc::StartModulesResult> start_modules() = 0;

    using StatusCallback = std::function<void(const API_types_lc::ExecutionStatusUpdateNotice&)>;

    virtual void subscribe_to_status_updates(StatusCallback callback) = 0;
};

} // namespace everest::lifecycle_cli
