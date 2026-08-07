// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/payment_service_selection.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_PaymentServiceSelectionReqType& in, PaymentServiceSelectionRequest& out) {
    cb_convert_enum(in.SelectedPaymentOption, out.selected_payment_option);
    out.selected_service_list.clear();
    for (uint16_t i = 0; i < in.SelectedServiceList.SelectedService.arrayLen; i++) {
        const auto& service = in.SelectedServiceList.SelectedService.array[i];
        auto& out_service = out.selected_service_list.emplace_back();
        out_service.service_id = service.ServiceID;
        CB2CPP_ASSIGN_IF_USED(service.ParameterSetID, out_service.parameter_set_id);
    }
}

template <> void convert(const struct iso2_PaymentServiceSelectionResType& in, PaymentServiceSelectionResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
}

template <> void convert(const PaymentServiceSelectionRequest& in, struct iso2_PaymentServiceSelectionReqType& out) {
    init_iso2_PaymentServiceSelectionReqType(&out);
    cb_convert_enum(in.selected_payment_option, out.SelectedPaymentOption);
    uint16_t index = 0;
    for (const auto& service : in.selected_service_list) {
        auto& out_service = out.SelectedServiceList.SelectedService.array[index++];
        init_iso2_SelectedServiceType(&out_service);
        out_service.ServiceID = service.service_id;
        CPP2CB_ASSIGN_IF_USED(service.parameter_set_id, out_service.ParameterSetID);
    }
    out.SelectedServiceList.SelectedService.arrayLen = in.selected_service_list.size();
}

template <> void convert(const PaymentServiceSelectionResponse& in, struct iso2_PaymentServiceSelectionResType& out) {
    init_iso2_PaymentServiceSelectionResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
}

template <> void insert_type(VariantAccess& va, const struct iso2_PaymentServiceSelectionReqType& in) {
    va.insert_type<PaymentServiceSelectionRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_PaymentServiceSelectionResType& in) {
    va.insert_type<PaymentServiceSelectionResponse>(in);
}

template <> int serialize_to_exi(const PaymentServiceSelectionRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PaymentServiceSelectionReq);
    convert(in, doc.V2G_Message.Body.PaymentServiceSelectionReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const PaymentServiceSelectionResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PaymentServiceSelectionRes);
    convert(in, doc.V2G_Message.Body.PaymentServiceSelectionRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const PaymentServiceSelectionRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const PaymentServiceSelectionResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
