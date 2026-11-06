// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
CREATE_TYPE_TRAITS(ServicePaymentSelection);
CREATE_TYPE_TRAITS(ContractAuthentication);
CREATE_TYPE_TRAITS(ChargeParameterDiscovery);
CREATE_TYPE_TRAITS(CableCheck);
CREATE_TYPE_TRAITS(PreCharge);
CREATE_TYPE_TRAITS(PowerDelivery);
CREATE_TYPE_TRAITS(CurrentDemand);
CREATE_TYPE_TRAITS(WeldingDetection);
CREATE_TYPE_TRAITS(SessionStop);

// header-local macro: drop it so the type-trait headers of the other protocols can redefine it
#undef CREATE_TYPE_TRAITS

} // namespace iso15118::message_din
