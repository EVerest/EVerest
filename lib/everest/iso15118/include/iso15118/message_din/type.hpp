// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/io/stream_view.hpp>

namespace iso15118::message_din {

enum class Type {
    None,
    SessionSetupReq,
    SessionSetupRes,
    ServiceDiscoveryReq,
    ServiceDiscoveryRes,
    ServicePaymentSelectionReq,
    ServicePaymentSelectionRes,
    ContractAuthenticationReq,
    ContractAuthenticationRes,
    ChargeParameterDiscoveryReq,
    ChargeParameterDiscoveryRes,
    CableCheckReq,
    CableCheckRes,
    PreChargeReq,
    PreChargeRes,
    PowerDeliveryReq,
    PowerDeliveryRes,
    CurrentDemandReq,
    CurrentDemandRes,
    WeldingDetectionReq,
    WeldingDetectionRes,
    SessionStopReq,
    SessionStopRes,
};

template <typename T> struct TypeTrait {
    static const Type type = Type::None;
};

template <typename InType, typename OutType> void convert(const InType&, OutType&);

template <typename MessageType> size_t serialize(const MessageType&, const io::StreamOutputView&);

//
// definitions of type traits
//
#ifdef CREATE_TYPE_TRAIT
#define CREATE_TYPE_TRAIT_PUSHED CREATE_TYPE_TRAIT
#endif

#define CREATE_TYPE_TRAIT(struct_name, enum_name)                                                                      \
    struct struct_name;                                                                                                \
    template <> struct TypeTrait<struct_name> {                                                                        \
        static const Type type = Type::enum_name;                                                                      \
    }

CREATE_TYPE_TRAIT(SessionSetupRequest, SessionSetupReq);
CREATE_TYPE_TRAIT(SessionSetupResponse, SessionSetupRes);
CREATE_TYPE_TRAIT(ServiceDiscoveryRequest, ServiceDiscoveryReq);
CREATE_TYPE_TRAIT(ServiceDiscoveryResponse, ServiceDiscoveryRes);
CREATE_TYPE_TRAIT(ServicePaymentSelectionRequest, ServicePaymentSelectionReq);
CREATE_TYPE_TRAIT(ServicePaymentSelectionResponse, ServicePaymentSelectionRes);
CREATE_TYPE_TRAIT(ContractAuthenticationRequest, ContractAuthenticationReq);
CREATE_TYPE_TRAIT(ContractAuthenticationResponse, ContractAuthenticationRes);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryRequest, ChargeParameterDiscoveryReq);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryResponse, ChargeParameterDiscoveryRes);
CREATE_TYPE_TRAIT(CableCheckRequest, CableCheckReq);
CREATE_TYPE_TRAIT(CableCheckResponse, CableCheckRes);
CREATE_TYPE_TRAIT(PreChargeRequest, PreChargeReq);
CREATE_TYPE_TRAIT(PreChargeResponse, PreChargeRes);
CREATE_TYPE_TRAIT(PowerDeliveryRequest, PowerDeliveryReq);
CREATE_TYPE_TRAIT(PowerDeliveryResponse, PowerDeliveryRes);
CREATE_TYPE_TRAIT(CurrentDemandRequest, CurrentDemandReq);
CREATE_TYPE_TRAIT(CurrentDemandResponse, CurrentDemandRes);
CREATE_TYPE_TRAIT(WeldingDetectionRequest, WeldingDetectionReq);
CREATE_TYPE_TRAIT(WeldingDetectionResponse, WeldingDetectionRes);
CREATE_TYPE_TRAIT(SessionStopRequest, SessionStopReq);
CREATE_TYPE_TRAIT(SessionStopResponse, SessionStopRes);

#ifdef CREATE_TYPE_TRAIT_PUSHED
#define CREATE_TYPE_TRAIT CREATE_TYPE_TRAIT_PUSHED
#else
#undef CREATE_TYPE_TRAIT
#endif

} // namespace iso15118::message_din
