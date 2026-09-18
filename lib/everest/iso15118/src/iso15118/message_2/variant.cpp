// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/variant.hpp>

#include <cassert>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

namespace iso15118::message_2 {

Variant::Variant(const io::StreamInputView& buffer_view) {

    // So PnC signature verification can re-decode the request into the cbv2g iso2 structs.
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
    session_id = header.session_id;

    VariantAccess va{header, this->data, this->type, this->error};

    auto& body = doc.V2G_Message.Body;

    if (body.SessionSetupReq_isUsed) {
        insert_type(va, body.SessionSetupReq);
    } else if (body.ServiceDiscoveryReq_isUsed) {
        insert_type(va, body.ServiceDiscoveryReq);
    } else if (body.ServiceDetailReq_isUsed) {
        insert_type(va, body.ServiceDetailReq);
    } else if (body.PaymentServiceSelectionReq_isUsed) {
        insert_type(va, body.PaymentServiceSelectionReq);
    } else if (body.PaymentDetailsReq_isUsed) {
        insert_type(va, body.PaymentDetailsReq);
    } else if (body.AuthorizationReq_isUsed) {
        insert_type(va, body.AuthorizationReq);
    } else if (body.ChargeParameterDiscoveryReq_isUsed) {
        insert_type(va, body.ChargeParameterDiscoveryReq);
    } else if (body.PowerDeliveryReq_isUsed) {
        insert_type(va, body.PowerDeliveryReq);
    } else if (body.ChargingStatusReq_isUsed) {
        insert_type(va, body.ChargingStatusReq);
    } else if (body.CableCheckReq_isUsed) {
        insert_type(va, body.CableCheckReq);
    } else if (body.PreChargeReq_isUsed) {
        insert_type(va, body.PreChargeReq);
    } else if (body.CurrentDemandReq_isUsed) {
        insert_type(va, body.CurrentDemandReq);
    } else if (body.WeldingDetectionReq_isUsed) {
        insert_type(va, body.WeldingDetectionReq);
    } else if (body.SessionStopReq_isUsed) {
        insert_type(va, body.SessionStopReq);
    } else if (body.MeteringReceiptReq_isUsed) {
        insert_type(va, body.MeteringReceiptReq);
    } else if (body.CertificateInstallationReq_isUsed) {
        // Relay-only: mark the type so the engine can forward the raw request EXI and splice the raw
        // response back. No message struct is decoded (data stays null, no custom deleter).
        type = Type::CertificateInstallationReq;
    } else if (body.CertificateUpdateReq_isUsed) {
        // Relay-only as well; the Update action is derived from this type in the relay state.
        type = Type::CertificateUpdateReq;
    } else {
        error = "chosen message type unhandled";
    }

    if (data) {
        // in case data was set, make sure the custom deleter and the type were set!
        assert(data.get_deleter() != nullptr);
        assert(type != Type::None);
    } else if (type == Type::None) {
        // A relay-only type carries no data but a valid type; only a genuinely unhandled message (type
        // still None) is an error.
        logf_error("Failed due to: %s\n", error.c_str());
    }
}

Type Variant::get_type() const {
    return type;
}

const std::string& Variant::get_error() const {
    return error;
}

const datatypes::SessionId& Variant::get_session_id() const {
    return session_id;
}

} // namespace iso15118::message_2
