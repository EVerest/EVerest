// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "include/lifecycle_type_wrapper.hpp"

namespace Everest::api::types::lifecycle {

StopModulesResultEnum_External to_external_api(StopModulesResultEnum_Internal const& val) {
    using SrcT = StopModulesResultEnum_Internal;
    using TarT = StopModulesResultEnum_External;
    switch (val) {
    case SrcT::Stopping:
        return TarT::Stopping;
    case SrcT::NoModulesToStop:
        return TarT::NoModulesToStop;
    case SrcT::Rejected:
        return TarT::Rejected;
    }
    throw std::out_of_range("Unexpected value for Everest::config::StopModulesResult");
}

StartModulesResultEnum_External to_external_api(StartModulesResultEnum_Internal const& val) {
    using SrcT = StartModulesResultEnum_Internal;
    using TarT = StartModulesResultEnum_External;
    switch (val) {
    case SrcT::Starting:
        return TarT::Starting;
    case SrcT::Restarting:
        return TarT::Restarting;
    case SrcT::NoConfigToStart:
        return TarT::NoConfigToStart;
    case SrcT::Rejected:
        return TarT::Rejected;
    }
    throw std::out_of_range("Unexpected value for Everest::config::RestartModulesResult");
}

} // namespace Everest::api::types::lifecycle
