// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/din/service_payment_selection.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din::msg {

template <> void convert(const struct din_ServicePaymentSelectionReqType& in, ServicePaymentSelectionRequest& out) {
    cb_convert_enum(in.SelectedPaymentOption, out.selected_payment_option);

    out.selected_service_list.reserve(data_types::SelectedServiceListMaxLength);
    for (int i = 0; i < in.SelectedServiceList.SelectedService.arrayLen; i++) {
        const auto& in_service = in.SelectedServiceList.SelectedService.array[i];
        data_types::SelectedService service;
        service.service_id = in_service.ServiceID;
        out.selected_service_list.push_back(service);
    }
}

template <>
void insert_type(VariantAccess& va, const struct din_ServicePaymentSelectionReqType& in,
                 const struct din_MessageHeaderType& header) {
    va.insert_type<ServicePaymentSelectionRequest>(in, header);
}

template <> void convert(const ServicePaymentSelectionResponse& in, struct din_ServicePaymentSelectionResType& out) {
    init_din_ServicePaymentSelectionResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);
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

template <> size_t serialize(const ServicePaymentSelectionResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::din::msg