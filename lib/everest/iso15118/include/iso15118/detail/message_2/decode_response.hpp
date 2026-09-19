// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/welding_detection.hpp>

namespace iso15118::message_2 {

// The SECC never receives a response, so the Variant does not dispatch to these. They are the
// cbv2g-to-C++ half of each response codec, used by the EXI tests and available to an EV side.
template <> void convert(const struct iso2_AuthorizationResType& in, AuthorizationResponse& out);
template <> void convert(const struct iso2_CableCheckResType& in, CableCheckResponse& out);
template <> void convert(const struct iso2_CertificateInstallationResType& in, CertificateInstallationResponse& out);
template <> void convert(const struct iso2_CertificateUpdateResType& in, CertificateUpdateResponse& out);
template <> void convert(const struct iso2_ChargeParameterDiscoveryResType& in, ChargeParameterDiscoveryResponse& out);
template <> void convert(const struct iso2_ChargingStatusResType& in, ChargingStatusResponse& out);
template <> void convert(const struct iso2_CurrentDemandResType& in, CurrentDemandResponse& out);
template <> void convert(const struct iso2_MeteringReceiptResType& in, MeteringReceiptResponse& out);
template <> void convert(const struct iso2_PaymentDetailsResType& in, PaymentDetailsResponse& out);
template <> void convert(const struct iso2_PaymentServiceSelectionResType& in, PaymentServiceSelectionResponse& out);
template <> void convert(const struct iso2_PowerDeliveryResType& in, PowerDeliveryResponse& out);
template <> void convert(const struct iso2_PreChargeResType& in, PreChargeResponse& out);
template <> void convert(const struct iso2_ServiceDetailResType& in, ServiceDetailResponse& out);
template <> void convert(const struct iso2_ServiceDiscoveryResType& in, ServiceDiscoveryResponse& out);
template <> void convert(const struct iso2_SessionSetupResType& in, SessionSetupResponse& out);
template <> void convert(const struct iso2_SessionStopResType& in, SessionStopResponse& out);
template <> void convert(const struct iso2_WeldingDetectionResType& in, WeldingDetectionResponse& out);

} // namespace iso15118::message_2
