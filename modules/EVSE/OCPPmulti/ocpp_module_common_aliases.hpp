// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/ocpp_module_common/conversions.hpp>
#include <everest/ocpp_module_common/device_model/everest_device_model_storage.hpp>
#include <everest/ocpp_module_common/error_handling.hpp>
#include <everest/ocpp_module_common/transaction_handler.hpp>

namespace module {

// Shared OCPP module support code lives in lib/everest/ocpp_module_common;
// pull the names into the module namespace to keep call sites unchanged.
namespace conversions = ocpp_module_common::conversions;
namespace device_model = ocpp_module_common::device_model;
using ocpp_module_common::EVSE_MANAGER_INOPERATIVE_ERROR;
using ocpp_module_common::get_component_from_error;
using ocpp_module_common::get_event_data;
using ocpp_module_common::load_mrec_error_map_overrides;
using ocpp_module_common::MREC_ERROR_MAP;
using ocpp_module_common::MREC_ERROR_MAP_TYPE;
using ocpp_module_common::TransactionData;
using ocpp_module_common::TransactionHandler;
using ocpp_module_common::TxEvent;
using ocpp_module_common::TxEventEffect;
using ocpp_module_common::TxStartStopPoint;

} // namespace module
