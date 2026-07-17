// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/service_payment_selection.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

template <> void convert(const struct din_ServicePaymentSelectionReqType& in, ServicePaymentSelectionRequest& out) {
    cb_convert_enum(in.SelectedPaymentOption, out.selected_payment_option);

    out.selected_service_list.clear();
    for (uint16_t i = 0; i < in.SelectedServiceList.SelectedService.arrayLen; i++) {
        const auto& service = in.SelectedServiceList.SelectedService.array[i];
        auto& out_service = out.selected_service_list.emplace_back();
        out_service.service_id = service.ServiceID;
        CB2CPP_ASSIGN_IF_USED(service.ParameterSetID, out_service.parameter_set_id);
    }
}

template <> void convert(const ServicePaymentSelectionRequest& in, struct din_ServicePaymentSelectionReqType& out) {
    init_din_ServicePaymentSelectionReqType(&out);
    cb_convert_enum(in.selected_payment_option, out.SelectedPaymentOption);

    uint16_t index = 0;
    for (const auto& service : in.selected_service_list) {
        auto& out_service = out.SelectedServiceList.SelectedService.array[index++];
        out_service.ServiceID = service.service_id;
        CPP2CB_ASSIGN_IF_USED(service.parameter_set_id, out_service.ParameterSetID);
    }
    out.SelectedServiceList.SelectedService.arrayLen = in.selected_service_list.size();
}

template <> void convert(const struct din_ServicePaymentSelectionResType& in, ServicePaymentSelectionResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
}

template <> void convert(const ServicePaymentSelectionResponse& in, struct din_ServicePaymentSelectionResType& out) {
    init_din_ServicePaymentSelectionResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
}

template <> void insert_type(VariantAccess& va, const struct din_ServicePaymentSelectionReqType& in) {
    va.insert_type<ServicePaymentSelectionRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_ServicePaymentSelectionResType& in) {
    va.insert_type<ServicePaymentSelectionResponse>(in);
}

template <> int serialize_to_exi(const ServicePaymentSelectionRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServicePaymentSelectionReq);
    convert(in, doc.V2G_Message.Body.ServicePaymentSelectionReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const ServicePaymentSelectionResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServicePaymentSelectionRes);
    convert(in, doc.V2G_Message.Body.ServicePaymentSelectionRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const ServicePaymentSelectionRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ServicePaymentSelectionResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
