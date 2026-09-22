// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/message/response_variant.hpp>

#include <cassert>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/received_message_type.hpp>
#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/app_handshake/appHand_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_IEC_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_Decoder.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Decoder.h>
#include <cbv2g/iso_20/iso20_DC_Decoder.h>

using PayloadType = iso15118::v2gtp::PayloadType;

namespace iso15118::message_20 {

static void handle_sap(VariantAccess& va) {
    appHand_exiDocument doc;

    const auto decode_status = decode_appHand_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_appHand_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.supportedAppProtocolRes_isUsed) {
        insert_type(va, doc.supportedAppProtocolRes);
    } else {
        va.mark_unhandled();
    }
}

static void handle_main(VariantAccess& va) {
    iso20_exiDocument doc;

    const auto decode_status = decode_iso20_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_iso20_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.SessionSetupRes_isUsed) {
        insert_type(va, doc.SessionSetupRes);
    } else if (doc.AuthorizationSetupRes_isUsed) {
        insert_type(va, doc.AuthorizationSetupRes);
    } else if (doc.AuthorizationRes_isUsed) {
        insert_type(va, doc.AuthorizationRes);
    } else if (doc.CertificateInstallationRes_isUsed) {
        insert_type(va, doc.CertificateInstallationRes);
    } else if (doc.ServiceDiscoveryRes_isUsed) {
        insert_type(va, doc.ServiceDiscoveryRes);
    } else if (doc.ServiceDetailRes_isUsed) {
        insert_type(va, doc.ServiceDetailRes);
    } else if (doc.ServiceSelectionRes_isUsed) {
        insert_type(va, doc.ServiceSelectionRes);
    } else if (doc.ScheduleExchangeRes_isUsed) {
        insert_type(va, doc.ScheduleExchangeRes);
    } else if (doc.PowerDeliveryRes_isUsed) {
        insert_type(va, doc.PowerDeliveryRes);
    } else if (doc.SessionStopRes_isUsed) {
        insert_type(va, doc.SessionStopRes);
    } else {
        va.mark_unhandled();
    }
}

static void handle_dc(VariantAccess& va) {
    iso20_dc_exiDocument doc;

    const auto decode_status = decode_iso20_dc_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_iso20_dc_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.DC_ChargeParameterDiscoveryRes_isUsed) {
        insert_type(va, doc.DC_ChargeParameterDiscoveryRes);
    } else if (doc.DC_CableCheckRes_isUsed) {
        insert_type(va, doc.DC_CableCheckRes);
    } else if (doc.DC_PreChargeRes_isUsed) {
        insert_type(va, doc.DC_PreChargeRes);
    } else if (doc.DC_ChargeLoopRes_isUsed) {
        insert_type(va, doc.DC_ChargeLoopRes);
    } else if (doc.DC_WeldingDetectionRes_isUsed) {
        insert_type(va, doc.DC_WeldingDetectionRes);
    } else {
        va.mark_unhandled();
    }
}

static void handle_ac(VariantAccess& va) {
    iso20_ac_exiDocument doc;

    const auto decode_status = decode_iso20_ac_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_iso20_ac_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.AC_ChargeParameterDiscoveryRes_isUsed) {
        insert_type(va, doc.AC_ChargeParameterDiscoveryRes);
    } else if (doc.AC_ChargeLoopRes_isUsed) {
        insert_type(va, doc.AC_ChargeLoopRes);
    } else {
        va.mark_unhandled();
    }
}

static void handle_ac_iec_der(VariantAccess& va) {
    iso20_ac_der_iec_exiDocument doc{};

    const auto decode_status = decode_iso20_ac_der_iec_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_iso20_ac_der_iec_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.AC_ChargeParameterDiscoveryRes_isUsed) {
        insert_type(va, doc.AC_ChargeParameterDiscoveryRes);
    } else if (doc.AC_ChargeLoopRes_isUsed) {
        insert_type(va, doc.AC_ChargeLoopRes);
    } else {
        va.mark_unhandled();
    }
}

static void handle_ac_sae_der(VariantAccess& va) {
    iso20_ac_der_sae_exiDocument doc{};

    const auto decode_status = decode_iso20_ac_der_sae_exiDocument(&va.input_stream, &doc);

    if (decode_status != 0) {
        va.error = "decode_iso20_ac_der_sae_exiDocument failed with " + std::to_string(decode_status);
        return;
    }

    if (doc.AC_ChargeParameterDiscoveryRes_isUsed) {
        insert_type(va, doc.AC_ChargeParameterDiscoveryRes);
    } else if (doc.AC_ChargeLoopRes_isUsed) {
        insert_type(va, doc.AC_ChargeLoopRes);
    } else {
        va.mark_unhandled();
    }
}

ResponseVariant::ResponseVariant(v2gtp::PayloadType payload_type, const io::StreamInputView& buffer_view) :
    ResponseVariant(payload_type, buffer_view, true) {
}

ResponseVariant::ResponseVariant(v2gtp::PayloadType payload_type, const io::StreamInputView& buffer_view,
                                 bool log_errors) {

    if (buffer_view.payload == nullptr or buffer_view.payload_len == 0) {
        error = "empty EXI payload";
        logf_error("Failed due to: %s\n", error.c_str());
        return;
    }

    // Only RequestVariant tells undecodable and unhandled frames apart; a response reports both through error.
    bool undecodable{false};
    bool unhandled_message{false};
    VariantAccess va{
        get_exi_input_stream(buffer_view), this->data, this->type, this->error, undecodable, unhandled_message,
    };

    if (payload_type == PayloadType::SAP) {
        handle_sap(va);
    } else if (payload_type == PayloadType::Part20Main) {
        handle_main(va);
    } else if (payload_type == PayloadType::Part20DC) {
        handle_dc(va);
    } else if (payload_type == PayloadType::Part20AC) {
        handle_ac(va);
    } else if (payload_type == v2gtp::PayloadType::Part20DerIec) {
        handle_ac_iec_der(va);
    } else if (payload_type == v2gtp::PayloadType::Part20DerSae) {
        handle_ac_sae_der(va);
    } else {
        logf_warning("Unknown type");
    }

    if (data != nullptr) {
        assert(type != Type::None);
    } else if (log_errors) {
        logf_error("Failed due to: %s\n", error.c_str());
    }
}

Type ResponseVariant::get_type() const {
    return type;
}

const std::string& ResponseVariant::get_error() const {
    return error;
}

Type received_message_type(const RequestVariant& request, v2gtp::PayloadType payload_type,
                           const io::StreamInputView& view) {
    if (not request.has_unhandled_message()) {
        return request.get_type();
    }
    const ResponseVariant response{payload_type, view, false};
    if (response.get_type() == Type::None) {
        logf_error("Failed due to: %s\n", request.get_error().c_str());
    }
    return response.get_type();
}

} // namespace iso15118::message_20
