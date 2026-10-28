// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/ocpp_module_common/error_mapping.hpp>

#include <string>
#include <unordered_map>
#include <utility>

#include <ocpp/v16/ocpp_enums.hpp>

namespace ocpp_module_common::v16 {

/// \brief The MREC vendor id, as held by MrecErrorMapping.
const std::string CHARGE_X_MREC_VENDOR_ID = MrecErrorMapping::vendor_id();

/// \brief The MREC errors, as held by MrecErrorMapping.
const std::unordered_map<std::string, std::pair<ocpp::v16::ChargePointErrorCode, std::string>> MREC_ERROR_MAP =
    MrecErrorMapping::entries();

/// \brief The errors OCPP 1.6 names directly, as held by OcppErrorMappingV16.
const std::unordered_map<std::string, ocpp::v16::ChargePointErrorCode> OCPP_ERROR_MAP = OcppErrorMappingV16::entries();

} // namespace ocpp_module_common::v16
