// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/response_variant.hpp>

#include <cassert>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/message_din/decode_response.hpp>
#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>

namespace iso15118::message_din {

ResponseVariant::ResponseVariant(const io::StreamInputView& buffer_view) {

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

        if (body.SessionSetupRes_isUsed) {
            va.insert_type<SessionSetupResponse>(body.SessionSetupRes);
        } else if (body.ServiceDiscoveryRes_isUsed) {
            va.insert_type<ServiceDiscoveryResponse>(body.ServiceDiscoveryRes);
        } else if (body.ServicePaymentSelectionRes_isUsed) {
            va.insert_type<ServicePaymentSelectionResponse>(body.ServicePaymentSelectionRes);
        } else if (body.ContractAuthenticationRes_isUsed) {
            va.insert_type<ContractAuthenticationResponse>(body.ContractAuthenticationRes);
        } else if (body.ChargeParameterDiscoveryRes_isUsed) {
            va.insert_type<ChargeParameterDiscoveryResponse>(body.ChargeParameterDiscoveryRes);
        } else if (body.CableCheckRes_isUsed) {
            va.insert_type<CableCheckResponse>(body.CableCheckRes);
        } else if (body.PreChargeRes_isUsed) {
            va.insert_type<PreChargeResponse>(body.PreChargeRes);
        } else if (body.PowerDeliveryRes_isUsed) {
            va.insert_type<PowerDeliveryResponse>(body.PowerDeliveryRes);
        } else if (body.CurrentDemandRes_isUsed) {
            va.insert_type<CurrentDemandResponse>(body.CurrentDemandRes);
        } else if (body.WeldingDetectionRes_isUsed) {
            va.insert_type<WeldingDetectionResponse>(body.WeldingDetectionRes);
        } else if (body.SessionStopRes_isUsed) {
            va.insert_type<SessionStopResponse>(body.SessionStopRes);
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

Type ResponseVariant::get_type() const {
    return type;
}

const std::string& ResponseVariant::get_error() const {
    return error;
}

} // namespace iso15118::message_din
