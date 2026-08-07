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

    // Retain the raw EXI payload so PnC signature verification can re-decode the request into the
    // cbv2g iso2 structs (needed to rebuild the signed AuthorizationReq fragment).
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

    VariantAccess va{
        header, this->data, this->type, this->custom_deleter, this->error,
    };

    auto& body = doc.V2G_Message.Body;

    if (body.SessionSetupReq_isUsed) {
        insert_type(va, body.SessionSetupReq);
    } else if (body.SessionSetupRes_isUsed) {
        insert_type(va, body.SessionSetupRes);
    } else if (body.ServiceDiscoveryReq_isUsed) {
        insert_type(va, body.ServiceDiscoveryReq);
    } else if (body.ServiceDiscoveryRes_isUsed) {
        insert_type(va, body.ServiceDiscoveryRes);
    } else if (body.ServiceDetailReq_isUsed) {
        insert_type(va, body.ServiceDetailReq);
    } else if (body.ServiceDetailRes_isUsed) {
        insert_type(va, body.ServiceDetailRes);
    } else if (body.PaymentServiceSelectionReq_isUsed) {
        insert_type(va, body.PaymentServiceSelectionReq);
    } else if (body.PaymentServiceSelectionRes_isUsed) {
        insert_type(va, body.PaymentServiceSelectionRes);
    } else if (body.PaymentDetailsReq_isUsed) {
        insert_type(va, body.PaymentDetailsReq);
    } else if (body.PaymentDetailsRes_isUsed) {
        insert_type(va, body.PaymentDetailsRes);
    } else if (body.AuthorizationReq_isUsed) {
        insert_type(va, body.AuthorizationReq);
    } else if (body.AuthorizationRes_isUsed) {
        insert_type(va, body.AuthorizationRes);
    } else if (body.ChargeParameterDiscoveryReq_isUsed) {
        insert_type(va, body.ChargeParameterDiscoveryReq);
    } else if (body.ChargeParameterDiscoveryRes_isUsed) {
        insert_type(va, body.ChargeParameterDiscoveryRes);
    } else if (body.PowerDeliveryReq_isUsed) {
        insert_type(va, body.PowerDeliveryReq);
    } else if (body.PowerDeliveryRes_isUsed) {
        insert_type(va, body.PowerDeliveryRes);
    } else if (body.ChargingStatusReq_isUsed) {
        insert_type(va, body.ChargingStatusReq);
    } else if (body.ChargingStatusRes_isUsed) {
        insert_type(va, body.ChargingStatusRes);
    } else if (body.CableCheckReq_isUsed) {
        insert_type(va, body.CableCheckReq);
    } else if (body.CableCheckRes_isUsed) {
        insert_type(va, body.CableCheckRes);
    } else if (body.PreChargeReq_isUsed) {
        insert_type(va, body.PreChargeReq);
    } else if (body.PreChargeRes_isUsed) {
        insert_type(va, body.PreChargeRes);
    } else if (body.CurrentDemandReq_isUsed) {
        insert_type(va, body.CurrentDemandReq);
    } else if (body.CurrentDemandRes_isUsed) {
        insert_type(va, body.CurrentDemandRes);
    } else if (body.WeldingDetectionReq_isUsed) {
        insert_type(va, body.WeldingDetectionReq);
    } else if (body.WeldingDetectionRes_isUsed) {
        insert_type(va, body.WeldingDetectionRes);
    } else if (body.SessionStopReq_isUsed) {
        insert_type(va, body.SessionStopReq);
    } else if (body.SessionStopRes_isUsed) {
        insert_type(va, body.SessionStopRes);
    } else if (body.MeteringReceiptReq_isUsed) {
        insert_type(va, body.MeteringReceiptReq);
    } else if (body.CertificateInstallationRes_isUsed) {
        // EVCC direction: fully decoded (contract chain, encrypted key, DHpublickey, eMAID) so the EV
        // can verify + install the contract certificate.
        insert_type(va, body.CertificateInstallationRes);
    } else if (body.CertificateInstallationReq_isUsed) {
        // Relay-only: mark the type so the d2 SECC engine can forward the raw request EXI
        // (get_exi_payload()) to the module and splice the raw CertificateInstallationRes back. No
        // message struct is decoded here (data stays null, no custom deleter).
        type = Type::CertificateInstallationReq;
    } else if (body.CertificateUpdateReq_isUsed) {
        // Relay-only, same as CertificateInstallationReq: forward the raw request EXI and splice the raw
        // CertificateUpdateRes back. The action (Update) is derived from this type in the relay state.
        type = Type::CertificateUpdateReq;
    } else {
        // MeteringReceipt is out of scope
        error = "chosen message type unhandled";
    }

    if (data) {
        // in case data was set, make sure the custom deleter and the type were set!
        assert(custom_deleter != nullptr);
        assert(type != Type::None);
    } else if (type == Type::None) {
        // A relay-only type (e.g. CertificateInstallationReq) carries no data but a valid type; only a
        // genuinely unhandled message (type still None) is an error.
        logf_error("Failed due to: %s\n", error.c_str());
    }
}

Variant::~Variant() {
    if (data) {
        custom_deleter(data);
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
