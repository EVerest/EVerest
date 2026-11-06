// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/io/stream_view.hpp>

namespace iso15118::message_20 {

enum class Type {
    None,
    SupportedAppProtocolReq,
    SupportedAppProtocolRes,
    SessionSetupReq,
    SessionSetupRes,
    AuthorizationSetupReq,
    AuthorizationSetupRes,
    AuthorizationReq,
    AuthorizationRes,
    CertificateInstallationReq,
    CertificateInstallationRes,
    ServiceDiscoveryReq,
    ServiceDiscoveryRes,
    ServiceDetailReq,
    ServiceDetailRes,
    ServiceSelectionReq,
    ServiceSelectionRes,
    DC_ChargeParameterDiscoveryReq,
    DC_ChargeParameterDiscoveryRes,
    ScheduleExchangeReq,
    ScheduleExchangeRes,
    DC_CableCheckReq,
    DC_CableCheckRes,
    DC_PreChargeReq,
    DC_PreChargeRes,
    PowerDeliveryReq,
    PowerDeliveryRes,
    DC_ChargeLoopReq,
    DC_ChargeLoopRes,
    DC_WeldingDetectionReq,
    DC_WeldingDetectionRes,
    SessionStopReq,
    SessionStopRes,
    AC_ChargeParameterDiscoveryReq,
    AC_ChargeParameterDiscoveryRes,
    AC_ChargeLoopReq,
    AC_ChargeLoopRes,
    DER_AC_ChargeParameterDiscoveryReq, // TODO(SL): Add IEC
    DER_AC_ChargeParameterDiscoveryRes,
    DER_AC_ChargeLoopReq,
    DER_AC_ChargeLoopRes,
    DER_SAE_AC_ChargeParameterDiscoveryReq,
    DER_SAE_AC_ChargeParameterDiscoveryRes,
    DER_SAE_AC_ChargeLoopReq,
    DER_SAE_AC_ChargeLoopRes,
};

template <typename T> struct TypeTrait {
    static const Type type = Type::None;
    static constexpr bool is_request = false;
    static constexpr bool is_response = false;
};

template <typename InType, typename OutType> void convert(const InType&, OutType&);

template <typename MessageType> size_t serialize(const MessageType&, const io::StreamOutputView&);

//
// definitions of type traits, a request and its response per message
//
#define CREATE_TYPE_TRAITS(name)                                                                                       \
    struct name##Request;                                                                                              \
    struct name##Response;                                                                                             \
    template <> struct TypeTrait<name##Request> {                                                                      \
        static const Type type = Type::name##Req;                                                                      \
        static constexpr bool is_request = true;                                                                       \
        static constexpr bool is_response = false;                                                                     \
    };                                                                                                                 \
    template <> struct TypeTrait<name##Response> {                                                                     \
        static const Type type = Type::name##Res;                                                                      \
        static constexpr bool is_request = false;                                                                      \
        static constexpr bool is_response = true;                                                                      \
    };                                                                                                                 \
    static_assert(static_cast<int>(Type::name##Res) == static_cast<int>(Type::name##Req) + 1,                          \
                  "a response must follow its request in Type")

CREATE_TYPE_TRAITS(SupportedAppProtocol);
CREATE_TYPE_TRAITS(SessionSetup);
CREATE_TYPE_TRAITS(AuthorizationSetup);
CREATE_TYPE_TRAITS(Authorization);
CREATE_TYPE_TRAITS(CertificateInstallation);
CREATE_TYPE_TRAITS(ServiceDiscovery);
CREATE_TYPE_TRAITS(ServiceDetail);
CREATE_TYPE_TRAITS(ServiceSelection);
CREATE_TYPE_TRAITS(DC_ChargeParameterDiscovery);
CREATE_TYPE_TRAITS(ScheduleExchange);
CREATE_TYPE_TRAITS(DC_CableCheck);
CREATE_TYPE_TRAITS(DC_PreCharge);
CREATE_TYPE_TRAITS(PowerDelivery);
CREATE_TYPE_TRAITS(DC_ChargeLoop);
CREATE_TYPE_TRAITS(DC_WeldingDetection);
CREATE_TYPE_TRAITS(SessionStop);
CREATE_TYPE_TRAITS(AC_ChargeParameterDiscovery);
CREATE_TYPE_TRAITS(AC_ChargeLoop);
CREATE_TYPE_TRAITS(DER_AC_ChargeParameterDiscovery); // TODO(SL): Add IEC
CREATE_TYPE_TRAITS(DER_AC_ChargeLoop);
CREATE_TYPE_TRAITS(DER_SAE_AC_ChargeParameterDiscovery);
CREATE_TYPE_TRAITS(DER_SAE_AC_ChargeLoop);

// header-local macro
#undef CREATE_TYPE_TRAITS

} // namespace iso15118::message_20
