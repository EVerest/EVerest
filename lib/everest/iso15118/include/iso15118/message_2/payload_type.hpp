// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/io/sdp.hpp>

namespace iso15118::message_2 {

template <typename T> struct PayloadTypeTrait;

//
// definitions of type traits
//
// All ISO 15118-2 messages use V2GTP payload type 0x8001 (SAP). Dispatch happens
// by protocol context at the session level, not by payload type.
//
#ifdef CREATE_TYPE_TRAIT
#define CREATE_TYPE_TRAIT_PUSHED CREATE_TYPE_TRAIT
#endif

#define CREATE_TYPE_TRAIT(struct_name, payload_type)                                                                   \
    struct struct_name;                                                                                                \
    template <> struct PayloadTypeTrait<struct_name> {                                                                 \
        static const io::v2gtp::PayloadType type = io::v2gtp::PayloadType::payload_type;                               \
    }

CREATE_TYPE_TRAIT(SessionSetupRequest, SAP);
CREATE_TYPE_TRAIT(SessionSetupResponse, SAP);
CREATE_TYPE_TRAIT(ServiceDiscoveryRequest, SAP);
CREATE_TYPE_TRAIT(ServiceDiscoveryResponse, SAP);
CREATE_TYPE_TRAIT(ServiceDetailRequest, SAP);
CREATE_TYPE_TRAIT(ServiceDetailResponse, SAP);
CREATE_TYPE_TRAIT(PaymentServiceSelectionRequest, SAP);
CREATE_TYPE_TRAIT(PaymentServiceSelectionResponse, SAP);
CREATE_TYPE_TRAIT(PaymentDetailsRequest, SAP);
CREATE_TYPE_TRAIT(PaymentDetailsResponse, SAP);
CREATE_TYPE_TRAIT(AuthorizationRequest, SAP);
CREATE_TYPE_TRAIT(AuthorizationResponse, SAP);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryRequest, SAP);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryResponse, SAP);
CREATE_TYPE_TRAIT(PowerDeliveryRequest, SAP);
CREATE_TYPE_TRAIT(PowerDeliveryResponse, SAP);
CREATE_TYPE_TRAIT(ChargingStatusRequest, SAP);
CREATE_TYPE_TRAIT(ChargingStatusResponse, SAP);
CREATE_TYPE_TRAIT(CableCheckRequest, SAP);
CREATE_TYPE_TRAIT(CableCheckResponse, SAP);
CREATE_TYPE_TRAIT(PreChargeRequest, SAP);
CREATE_TYPE_TRAIT(PreChargeResponse, SAP);
CREATE_TYPE_TRAIT(CurrentDemandRequest, SAP);
CREATE_TYPE_TRAIT(CurrentDemandResponse, SAP);
CREATE_TYPE_TRAIT(WeldingDetectionRequest, SAP);
CREATE_TYPE_TRAIT(WeldingDetectionResponse, SAP);
CREATE_TYPE_TRAIT(SessionStopRequest, SAP);
CREATE_TYPE_TRAIT(SessionStopResponse, SAP);
CREATE_TYPE_TRAIT(MeteringReceiptRequest, SAP);
CREATE_TYPE_TRAIT(MeteringReceiptResponse, SAP);

#ifdef CREATE_TYPE_TRAIT_PUSHED
#define CREATE_TYPE_TRAIT CREATE_TYPE_TRAIT_PUSHED
#else
#undef CREATE_TYPE_TRAIT
#endif

} // namespace iso15118::message_2
