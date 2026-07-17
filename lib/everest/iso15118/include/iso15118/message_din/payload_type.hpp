// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/io/sdp.hpp>

namespace iso15118::message_din {

template <typename T> struct PayloadTypeTrait;

//
// definitions of type traits
//
#ifdef CREATE_TYPE_TRAIT
#define CREATE_TYPE_TRAIT_PUSHED CREATE_TYPE_TRAIT
#endif

// All DIN SPEC 70121 messages use the V2GTP payload type 0x8001 (SAP). Dispatch happens
// by protocol context at session level, not by payload type.
#define CREATE_TYPE_TRAIT(struct_name)                                                                                 \
    struct struct_name;                                                                                                \
    template <> struct PayloadTypeTrait<struct_name> {                                                                 \
        static const io::v2gtp::PayloadType type = io::v2gtp::PayloadType::SAP;                                         \
    }

CREATE_TYPE_TRAIT(SessionSetupRequest);
CREATE_TYPE_TRAIT(SessionSetupResponse);
CREATE_TYPE_TRAIT(ServiceDiscoveryRequest);
CREATE_TYPE_TRAIT(ServiceDiscoveryResponse);
CREATE_TYPE_TRAIT(ServicePaymentSelectionRequest);
CREATE_TYPE_TRAIT(ServicePaymentSelectionResponse);
CREATE_TYPE_TRAIT(ContractAuthenticationRequest);
CREATE_TYPE_TRAIT(ContractAuthenticationResponse);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryRequest);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryResponse);
CREATE_TYPE_TRAIT(CableCheckRequest);
CREATE_TYPE_TRAIT(CableCheckResponse);
CREATE_TYPE_TRAIT(PreChargeRequest);
CREATE_TYPE_TRAIT(PreChargeResponse);
CREATE_TYPE_TRAIT(PowerDeliveryRequest);
CREATE_TYPE_TRAIT(PowerDeliveryResponse);
CREATE_TYPE_TRAIT(CurrentDemandRequest);
CREATE_TYPE_TRAIT(CurrentDemandResponse);
CREATE_TYPE_TRAIT(WeldingDetectionRequest);
CREATE_TYPE_TRAIT(WeldingDetectionResponse);
CREATE_TYPE_TRAIT(SessionStopRequest);
CREATE_TYPE_TRAIT(SessionStopResponse);

#ifdef CREATE_TYPE_TRAIT_PUSHED
#define CREATE_TYPE_TRAIT CREATE_TYPE_TRAIT_PUSHED
#else
#undef CREATE_TYPE_TRAIT
#endif

} // namespace iso15118::message_din
