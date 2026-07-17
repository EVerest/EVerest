// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>

#include <iso15118/io/stream_view.hpp>

namespace iso15118::message_2 {

enum class Type {
    None,
    SessionSetupReq,
    SessionSetupRes,
    ServiceDiscoveryReq,
    ServiceDiscoveryRes,
    ServiceDetailReq,
    ServiceDetailRes,
    PaymentServiceSelectionReq,
    PaymentServiceSelectionRes,
    PaymentDetailsReq,
    PaymentDetailsRes,
    AuthorizationReq,
    AuthorizationRes,
    ChargeParameterDiscoveryReq,
    ChargeParameterDiscoveryRes,
    PowerDeliveryReq,
    PowerDeliveryRes,
    ChargingStatusReq,
    ChargingStatusRes,
    CableCheckReq,
    CableCheckRes,
    PreChargeReq,
    PreChargeRes,
    CurrentDemandReq,
    CurrentDemandRes,
    WeldingDetectionReq,
    WeldingDetectionRes,
    SessionStopReq,
    SessionStopRes,
    MeteringReceiptReq,
    MeteringReceiptRes,
    // Relay-only: the SECC does not decode/verify CertificateInstallation (Plug-and-Charge
    // provisioning). The raw request EXI is forwarded to the module and the raw response EXI is
    // spliced back verbatim, so only the request type is tracked here (no message struct / TypeTrait).
    CertificateInstallationReq,
    CertificateUpdateReq,
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
CREATE_TYPE_TRAIT(ServiceDetailRequest, ServiceDetailReq);
CREATE_TYPE_TRAIT(ServiceDetailResponse, ServiceDetailRes);
CREATE_TYPE_TRAIT(PaymentServiceSelectionRequest, PaymentServiceSelectionReq);
CREATE_TYPE_TRAIT(PaymentServiceSelectionResponse, PaymentServiceSelectionRes);
CREATE_TYPE_TRAIT(PaymentDetailsRequest, PaymentDetailsReq);
CREATE_TYPE_TRAIT(PaymentDetailsResponse, PaymentDetailsRes);
CREATE_TYPE_TRAIT(AuthorizationRequest, AuthorizationReq);
CREATE_TYPE_TRAIT(AuthorizationResponse, AuthorizationRes);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryRequest, ChargeParameterDiscoveryReq);
CREATE_TYPE_TRAIT(ChargeParameterDiscoveryResponse, ChargeParameterDiscoveryRes);
CREATE_TYPE_TRAIT(PowerDeliveryRequest, PowerDeliveryReq);
CREATE_TYPE_TRAIT(PowerDeliveryResponse, PowerDeliveryRes);
CREATE_TYPE_TRAIT(ChargingStatusRequest, ChargingStatusReq);
CREATE_TYPE_TRAIT(ChargingStatusResponse, ChargingStatusRes);
CREATE_TYPE_TRAIT(CableCheckRequest, CableCheckReq);
CREATE_TYPE_TRAIT(CableCheckResponse, CableCheckRes);
CREATE_TYPE_TRAIT(PreChargeRequest, PreChargeReq);
CREATE_TYPE_TRAIT(PreChargeResponse, PreChargeRes);
CREATE_TYPE_TRAIT(CurrentDemandRequest, CurrentDemandReq);
CREATE_TYPE_TRAIT(CurrentDemandResponse, CurrentDemandRes);
CREATE_TYPE_TRAIT(WeldingDetectionRequest, WeldingDetectionReq);
CREATE_TYPE_TRAIT(WeldingDetectionResponse, WeldingDetectionRes);
CREATE_TYPE_TRAIT(SessionStopRequest, SessionStopReq);
CREATE_TYPE_TRAIT(SessionStopResponse, SessionStopRes);
CREATE_TYPE_TRAIT(MeteringReceiptRequest, MeteringReceiptReq);
CREATE_TYPE_TRAIT(MeteringReceiptResponse, MeteringReceiptRes);

#ifdef CREATE_TYPE_TRAIT_PUSHED
#define CREATE_TYPE_TRAIT CREATE_TYPE_TRAIT_PUSHED
#else
#undef CREATE_TYPE_TRAIT
#endif

} // namespace iso15118::message_2
