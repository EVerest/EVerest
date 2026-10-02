// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cbv2g/din/din_msgDefDatatypes.h>

#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

namespace iso15118::message_din {

// The SECC never receives a response, so the Variant does not dispatch to these. They are the
// cbv2g-to-C++ half of each response codec, used by the EXI tests and available to an EV side.
template <> void convert(const struct din_CableCheckResType& in, CableCheckResponse& out);
template <> void convert(const struct din_ChargeParameterDiscoveryResType& in, ChargeParameterDiscoveryResponse& out);
template <> void convert(const struct din_ContractAuthenticationResType& in, ContractAuthenticationResponse& out);
template <> void convert(const struct din_CurrentDemandResType& in, CurrentDemandResponse& out);
template <> void convert(const struct din_PowerDeliveryResType& in, PowerDeliveryResponse& out);
template <> void convert(const struct din_PreChargeResType& in, PreChargeResponse& out);
template <> void convert(const struct din_ServiceDiscoveryResType& in, ServiceDiscoveryResponse& out);
template <> void convert(const struct din_ServicePaymentSelectionResType& in, ServicePaymentSelectionResponse& out);
template <> void convert(const struct din_SessionSetupResType& in, SessionSetupResponse& out);
template <> void convert(const struct din_SessionStopResType& in, SessionStopResponse& out);
template <> void convert(const struct din_WeldingDetectionResType& in, WeldingDetectionResponse& out);

} // namespace iso15118::message_din
