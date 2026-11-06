// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
    // Relay-only on the SECC (the raw request EXI is forwarded and the raw response spliced back), so
    // its decode path only tags the type; the structs below exist for the EV direction.
    CertificateInstallationReq,
    CertificateInstallationRes,
    CertificateUpdateReq,
    CertificateUpdateRes,
};

template <typename T> struct TypeTrait {
    static const Type type = Type::None;
};

template <typename InType, typename OutType> void convert(const InType&, OutType&);

template <typename MessageType> size_t serialize(const MessageType&, const io::StreamOutputView&);

// A request and its response per message.
#define CREATE_TYPE_TRAITS(name)                                                                                       \
    struct name##Request;                                                                                              \
    struct name##Response;                                                                                             \
    template <> struct TypeTrait<name##Request> {                                                                      \
        static const Type type = Type::name##Req;                                                                      \
    };                                                                                                                 \
    template <> struct TypeTrait<name##Response> {                                                                     \
        static const Type type = Type::name##Res;                                                                      \
    };                                                                                                                 \
    static_assert(static_cast<int>(Type::name##Res) == static_cast<int>(Type::name##Req) + 1,                          \
                  "a response must follow its request in Type")

CREATE_TYPE_TRAITS(SessionSetup);
CREATE_TYPE_TRAITS(ServiceDiscovery);
CREATE_TYPE_TRAITS(ServiceDetail);
CREATE_TYPE_TRAITS(PaymentServiceSelection);
CREATE_TYPE_TRAITS(PaymentDetails);
CREATE_TYPE_TRAITS(Authorization);
CREATE_TYPE_TRAITS(ChargeParameterDiscovery);
CREATE_TYPE_TRAITS(PowerDelivery);
CREATE_TYPE_TRAITS(ChargingStatus);
CREATE_TYPE_TRAITS(CableCheck);
CREATE_TYPE_TRAITS(PreCharge);
CREATE_TYPE_TRAITS(CurrentDemand);
CREATE_TYPE_TRAITS(WeldingDetection);
CREATE_TYPE_TRAITS(SessionStop);
CREATE_TYPE_TRAITS(MeteringReceipt);
CREATE_TYPE_TRAITS(CertificateInstallation);
CREATE_TYPE_TRAITS(CertificateUpdate);

// header-local macro: drop it so the type-trait headers of the other protocols can redefine it
#undef CREATE_TYPE_TRAITS

} // namespace iso15118::message_2
