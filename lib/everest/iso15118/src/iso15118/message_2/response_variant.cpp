// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/response_variant.hpp>

#include <cassert>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/message_2/decode_response.hpp>
#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

namespace iso15118::message_2 {

ResponseVariant::ResponseVariant(const io::StreamInputView& buffer_view) {

    exi_payload.assign(buffer_view.payload, buffer_view.payload + buffer_view.payload_len);

    auto input_stream = get_exi_input_stream(buffer_view);

    iso2_exiDocument doc{};

    const auto decode_status = decode_iso2_exiDocument(&input_stream, &doc);

    if (decode_status != 0) {
        error = "decode_iso2_exiDocument failed with " + std::to_string(decode_status);
        logf_error("Failed due to: %s\n", error.c_str());
        return;
    }

    Header header;
    convert(doc.V2G_Message.Header, header);

    VariantAccess va{header, this->data, this->type, this->error};

    const auto& body = doc.V2G_Message.Body;

    if (body.SessionSetupRes_isUsed) {
        va.insert_type<SessionSetupResponse>(body.SessionSetupRes);
    } else if (body.ServiceDiscoveryRes_isUsed) {
        va.insert_type<ServiceDiscoveryResponse>(body.ServiceDiscoveryRes);
    } else if (body.ServiceDetailRes_isUsed) {
        va.insert_type<ServiceDetailResponse>(body.ServiceDetailRes);
    } else if (body.PaymentServiceSelectionRes_isUsed) {
        va.insert_type<PaymentServiceSelectionResponse>(body.PaymentServiceSelectionRes);
    } else if (body.PaymentDetailsRes_isUsed) {
        va.insert_type<PaymentDetailsResponse>(body.PaymentDetailsRes);
    } else if (body.AuthorizationRes_isUsed) {
        va.insert_type<AuthorizationResponse>(body.AuthorizationRes);
    } else if (body.ChargeParameterDiscoveryRes_isUsed) {
        va.insert_type<ChargeParameterDiscoveryResponse>(body.ChargeParameterDiscoveryRes);
    } else if (body.PowerDeliveryRes_isUsed) {
        va.insert_type<PowerDeliveryResponse>(body.PowerDeliveryRes);
    } else if (body.ChargingStatusRes_isUsed) {
        va.insert_type<ChargingStatusResponse>(body.ChargingStatusRes);
    } else if (body.CableCheckRes_isUsed) {
        va.insert_type<CableCheckResponse>(body.CableCheckRes);
    } else if (body.PreChargeRes_isUsed) {
        va.insert_type<PreChargeResponse>(body.PreChargeRes);
    } else if (body.CurrentDemandRes_isUsed) {
        va.insert_type<CurrentDemandResponse>(body.CurrentDemandRes);
    } else if (body.WeldingDetectionRes_isUsed) {
        va.insert_type<WeldingDetectionResponse>(body.WeldingDetectionRes);
    } else if (body.SessionStopRes_isUsed) {
        va.insert_type<SessionStopResponse>(body.SessionStopRes);
    } else if (body.MeteringReceiptRes_isUsed) {
        va.insert_type<MeteringReceiptResponse>(body.MeteringReceiptRes);
    } else if (body.CertificateInstallationRes_isUsed) {
        va.insert_type<CertificateInstallationResponse>(body.CertificateInstallationRes);
    } else {
        error = "chosen message type unhandled";
    }

    if (data) {
        // in case data was set, make sure the custom deleter and the type were set!
        assert(data.get_deleter() != nullptr);
        assert(type != Type::None);
    } else {
        logf_error("Failed due to: %s\n", error.c_str());
    }
}

Type ResponseVariant::get_type() const {
    return type;
}

const std::string& ResponseVariant::get_error() const {
    return error;
}

} // namespace iso15118::message_2
