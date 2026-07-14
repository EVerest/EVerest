// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest_api_types/ev_simulator/API.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "generated/types/board_support_common.hpp"
#include "generated/types/evse_manager.hpp"
#include "generated/types/iso15118.hpp"
#include "generated/types/slac.hpp"
#pragma GCC diagnostic pop

namespace everest::lib::API::V1_0::types::ev_simulator {

using PaymentOption_Internal = ::types::iso15118::PaymentOption;
using PaymentOption_External = PaymentOption;

PaymentOption_Internal to_internal_api(PaymentOption_External const& val);
PaymentOption_External to_external_api(PaymentOption_Internal const& val);

using EvInfo_Internal = ::types::evse_manager::EVInfo;
using EvInfo_External = EvInfo;

EvInfo_Internal to_internal_api(EvInfo_External const& val);
EvInfo_External to_external_api(EvInfo_Internal const& val);

using BspEvent_Internal = ::types::board_support_common::BspEvent;
using BspEvent_External = BspEvent;

BspEvent_Internal to_internal_api(BspEvent_External const& val);
BspEvent_External to_external_api(BspEvent_Internal const& val);

using SlacState_Internal = ::types::slac::State;
using SlacState_External = SlacState;

SlacState_Internal to_internal_api(SlacState_External const& val);
SlacState_External to_external_api(SlacState_Internal const& val);

} // namespace everest::lib::API::V1_0::types::ev_simulator
