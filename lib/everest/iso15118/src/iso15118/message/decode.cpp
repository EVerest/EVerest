// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/message/decode.hpp>

#include <exception>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/certificate_installation.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <cbv2g/app_handshake/appHand_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_IEC_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_Decoder.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Decoder.h>
#include <cbv2g/iso_20/iso20_DC_Decoder.h>

namespace iso15118::message_20 {

using PayloadType = v2gtp::PayloadType;

namespace {

// The cbv2g document each payload type carries.
template <PayloadType> struct Document;

#define DOCUMENT(payload_type, document_type, decoder)                                                                 \
    template <> struct Document<PayloadType::payload_type> {                                                           \
        using type = document_type;                                                                                    \
        static int decode(exi_bitstream_t* stream, type* doc) { return decoder(stream, doc); }                         \
    }

DOCUMENT(SAP, appHand_exiDocument, decode_appHand_exiDocument);
DOCUMENT(Part20Main, iso20_exiDocument, decode_iso20_exiDocument);
DOCUMENT(Part20DC, iso20_dc_exiDocument, decode_iso20_dc_exiDocument);
DOCUMENT(Part20AC, iso20_ac_exiDocument, decode_iso20_ac_exiDocument);
DOCUMENT(Part20DerIec, iso20_ac_der_iec_exiDocument, decode_iso20_ac_der_iec_exiDocument);
DOCUMENT(Part20DerSae, iso20_ac_der_sae_exiDocument, decode_iso20_ac_der_sae_exiDocument);

#undef DOCUMENT

template <PayloadType payload_type>
bool decode_document(const io::StreamInputView& view, typename Document<payload_type>::type& doc) {
    if (view.payload == nullptr or view.payload_len == 0) {
        return false;
    }
    auto stream = get_exi_input_stream(view);
    return Document<payload_type>::decode(&stream, &doc) == 0;
}

// The body of a response in its document, nullptr when the document carries another message.
template <typename Message> struct Body;

// Every response, with the document member that holds it.
#define RESPONSES(X)                                                                                                   \
    X(SupportedAppProtocolResponse, supportedAppProtocolRes)                                                           \
    X(SessionSetupResponse, SessionSetupRes)                                                                           \
    X(AuthorizationSetupResponse, AuthorizationSetupRes)                                                               \
    X(AuthorizationResponse, AuthorizationRes)                                                                         \
    X(CertificateInstallationResponse, CertificateInstallationRes)                                                     \
    X(ServiceDiscoveryResponse, ServiceDiscoveryRes)                                                                   \
    X(ServiceDetailResponse, ServiceDetailRes)                                                                         \
    X(ServiceSelectionResponse, ServiceSelectionRes)                                                                   \
    X(ScheduleExchangeResponse, ScheduleExchangeRes)                                                                   \
    X(PowerDeliveryResponse, PowerDeliveryRes)                                                                         \
    X(SessionStopResponse, SessionStopRes)                                                                             \
    X(DC_ChargeParameterDiscoveryResponse, DC_ChargeParameterDiscoveryRes)                                             \
    X(DC_CableCheckResponse, DC_CableCheckRes)                                                                         \
    X(DC_PreChargeResponse, DC_PreChargeRes)                                                                           \
    X(DC_ChargeLoopResponse, DC_ChargeLoopRes)                                                                         \
    X(DC_WeldingDetectionResponse, DC_WeldingDetectionRes)                                                             \
    X(AC_ChargeParameterDiscoveryResponse, AC_ChargeParameterDiscoveryRes)                                             \
    X(AC_ChargeLoopResponse, AC_ChargeLoopRes)                                                                         \
    X(DER_AC_ChargeParameterDiscoveryResponse, AC_ChargeParameterDiscoveryRes)                                         \
    X(DER_AC_ChargeLoopResponse, AC_ChargeLoopRes)                                                                     \
    X(DER_SAE_AC_ChargeParameterDiscoveryResponse, AC_ChargeParameterDiscoveryRes)                                     \
    X(DER_SAE_AC_ChargeLoopResponse, AC_ChargeLoopRes)

#define BODY(message, member)                                                                                          \
    template <> struct Body<message> {                                                                                 \
        template <typename Doc> static const auto* get(const Doc& doc) {                                               \
            return doc.member##_isUsed ? &doc.member : nullptr;                                                        \
        }                                                                                                              \
    };
RESPONSES(BODY)
#undef BODY

// The response a document carries, from the rows of RESPONSES whose payload type is the document's.
template <PayloadType payload_type, typename Doc> Type peek_response(const Doc& doc) {
#define PEEK_RESPONSE(message, member)                                                                                 \
    if constexpr (PayloadTypeTrait<message>::type == payload_type) {                                                   \
        if (doc.member##_isUsed) {                                                                                     \
            return TypeTrait<message>::type;                                                                           \
        }                                                                                                              \
    }
    RESPONSES(PEEK_RESPONSE)
#undef PEEK_RESPONSE
    return Type::None;
}

// The requests each document carries; the responses come from peek_response.
#define PEEK(member, message_type)                                                                                     \
    if (doc.member##_isUsed) {                                                                                         \
        return Type::message_type;                                                                                     \
    }

Type peek_sap(const appHand_exiDocument& doc) {
    PEEK(supportedAppProtocolReq, SupportedAppProtocolReq)
    return Type::None;
}

Type peek_main(const iso20_exiDocument& doc) {
    PEEK(SessionSetupReq, SessionSetupReq)
    PEEK(AuthorizationSetupReq, AuthorizationSetupReq)
    PEEK(AuthorizationReq, AuthorizationReq)
    PEEK(CertificateInstallationReq, CertificateInstallationReq)
    PEEK(ServiceDiscoveryReq, ServiceDiscoveryReq)
    PEEK(ServiceDetailReq, ServiceDetailReq)
    PEEK(ServiceSelectionReq, ServiceSelectionReq)
    PEEK(ScheduleExchangeReq, ScheduleExchangeReq)
    PEEK(PowerDeliveryReq, PowerDeliveryReq)
    PEEK(SessionStopReq, SessionStopReq)
    return Type::None;
}

Type peek_dc(const iso20_dc_exiDocument& doc) {
    PEEK(DC_ChargeParameterDiscoveryReq, DC_ChargeParameterDiscoveryReq)
    PEEK(DC_CableCheckReq, DC_CableCheckReq)
    PEEK(DC_PreChargeReq, DC_PreChargeReq)
    PEEK(DC_ChargeLoopReq, DC_ChargeLoopReq)
    PEEK(DC_WeldingDetectionReq, DC_WeldingDetectionReq)
    return Type::None;
}

Type peek_ac(const iso20_ac_exiDocument& doc) {
    PEEK(AC_ChargeParameterDiscoveryReq, AC_ChargeParameterDiscoveryReq)
    PEEK(AC_ChargeLoopReq, AC_ChargeLoopReq)
    return Type::None;
}

Type peek_der_iec(const iso20_ac_der_iec_exiDocument& doc) {
    PEEK(AC_ChargeParameterDiscoveryReq, DER_AC_ChargeParameterDiscoveryReq)
    PEEK(AC_ChargeLoopReq, DER_AC_ChargeLoopReq)
    return Type::None;
}

Type peek_der_sae(const iso20_ac_der_sae_exiDocument& doc) {
    PEEK(AC_ChargeParameterDiscoveryReq, DER_SAE_AC_ChargeParameterDiscoveryReq)
    PEEK(AC_ChargeLoopReq, DER_SAE_AC_ChargeLoopReq)
    return Type::None;
}

#undef PEEK

template <PayloadType payload_type, typename Peek> Type peek_as(const io::StreamInputView& view, Peek peek) {
    typename Document<payload_type>::type doc;
    if (not decode_document<payload_type>(view, doc)) {
        return Type::None;
    }
    const auto type = peek(doc);
    return type != Type::None ? type : peek_response<payload_type>(doc);
}

} // namespace

template <typename Message> std::optional<Message> decode(PayloadType payload_type, const io::StreamInputView& view) {
    constexpr auto expected = PayloadTypeTrait<Message>::type;
    if (payload_type != expected) {
        return std::nullopt;
    }
    typename Document<expected>::type doc;
    if (not decode_document<expected>(view, doc)) {
        return std::nullopt;
    }
    const auto* body = Body<Message>::get(doc);
    if (body == nullptr) {
        return std::nullopt;
    }
    Message message;
    try {
        convert(*body, message);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return message;
}

#define INSTANTIATE(message, member)                                                                                   \
    template std::optional<message> decode<message>(PayloadType, const io::StreamInputView&);
RESPONSES(INSTANTIATE)
#undef INSTANTIATE
#undef RESPONSES

Type peek_type(PayloadType payload_type, const io::StreamInputView& view) {
    switch (payload_type) {
    case PayloadType::SAP:
        return peek_as<PayloadType::SAP>(view, peek_sap);
    case PayloadType::Part20Main:
        return peek_as<PayloadType::Part20Main>(view, peek_main);
    case PayloadType::Part20DC:
        return peek_as<PayloadType::Part20DC>(view, peek_dc);
    case PayloadType::Part20AC:
        return peek_as<PayloadType::Part20AC>(view, peek_ac);
    case PayloadType::Part20DerIec:
        return peek_as<PayloadType::Part20DerIec>(view, peek_der_iec);
    case PayloadType::Part20DerSae:
        return peek_as<PayloadType::Part20DerSae>(view, peek_der_sae);
    default:
        return Type::None;
    }
}

} // namespace iso15118::message_20
