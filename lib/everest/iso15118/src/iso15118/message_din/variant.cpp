// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/variant.hpp>

#include <cassert>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>

namespace iso15118::message_din {

Variant::Variant(const io::StreamInputView& buffer_view) {

    VariantAccess va{
        get_exi_input_stream(buffer_view), nullptr, this->data, this->type, this->error,
    };

    din_exiDocument doc;

    const auto decode_status = decode_din_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_din_exiDocument failed with " + std::to_string(decode_status);
    } else {
        const auto& body = doc.V2G_Message.Body;
        va.header = &doc.V2G_Message.Header;

        Header header;
        convert(doc.V2G_Message.Header, header);
        session_id = header.session_id;

        if (body.SessionSetupReq_isUsed) {
            insert_type(va, body.SessionSetupReq);
        } else if (body.ServiceDiscoveryReq_isUsed) {
            insert_type(va, body.ServiceDiscoveryReq);
        } else if (body.ServicePaymentSelectionReq_isUsed) {
            insert_type(va, body.ServicePaymentSelectionReq);
        } else if (body.ContractAuthenticationReq_isUsed) {
            insert_type(va, body.ContractAuthenticationReq);
        } else if (body.ChargeParameterDiscoveryReq_isUsed) {
            insert_type(va, body.ChargeParameterDiscoveryReq);
        } else if (body.CableCheckReq_isUsed) {
            insert_type(va, body.CableCheckReq);
        } else if (body.PreChargeReq_isUsed) {
            insert_type(va, body.PreChargeReq);
        } else if (body.PowerDeliveryReq_isUsed) {
            insert_type(va, body.PowerDeliveryReq);
        } else if (body.CurrentDemandReq_isUsed) {
            insert_type(va, body.CurrentDemandReq);
        } else if (body.WeldingDetectionReq_isUsed) {
            insert_type(va, body.WeldingDetectionReq);
        } else if (body.SessionStopReq_isUsed) {
            insert_type(va, body.SessionStopReq);
        } else {
            va.error = "chosen message type unhandled";
        }
    }

    if (data) {
        // in case data was set, make sure the custom deleter and the type were set!
        assert(data.get_deleter() != nullptr);
        assert(type != Type::None);
    } else {
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

} // namespace iso15118::message_din
