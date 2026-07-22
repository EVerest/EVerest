// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <cstddef>
#include <iso15118/message/din/service_discovery.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din::msg {

template <> void convert(const data_types::ServiceTag& in, struct din_ServiceTagType& out) {
    init_din_ServiceTagType(&out);
    out.ServiceID = in.id;
    CPP2CB_STRING_IF_USED(in.name, out.ServiceName);
    cb_convert_enum(in.category, out.ServiceCategory);
    CPP2CB_STRING_IF_USED(in.scope, out.ServiceScope);
}

template <> void convert(const data_types::Service& in, struct din_ServiceType& out) {
    init_din_ServiceType(&out);
    convert(in.service_tag, out.ServiceTag);
    out.FreeService = in.free_service;
}

template <> void convert(const data_types::ChargeService& in, struct din_ServiceChargeType& out) {
    init_din_ServiceChargeType(&out);
    convert(in.service_tag, out.ServiceTag);
    out.FreeService = in.free_service;
    cb_convert_enum(in.energy_transfer_type, out.EnergyTransferType);
}

template <> void convert(const struct din_ServiceDiscoveryReqType& in, ServiceDiscoveryRequest& out) {
    if (in.ServiceCategory_isUsed) {
        out.service_category.emplace();
        cb_convert_enum(in.ServiceCategory, out.service_category.value());
    }
    CB2CPP_STRING_IF_USED(in.ServiceScope, out.service_scope);
}

template <>
void insert_type(VariantAccess& va, const struct din_ServiceDiscoveryReqType& in,
                 const struct din_MessageHeaderType& header) {
    va.insert_type<ServiceDiscoveryRequest>(in, header);
}

template <> void convert(const ServiceDiscoveryResponse& in, struct din_ServiceDiscoveryResType& out) {
    init_din_ServiceDiscoveryResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);

    const auto payment_option_length =
        std::min(static_cast<size_t>(din_paymentOptionType_2_ARRAY_SIZE), in.payment_option_list.size());
    for (size_t i = 0; i < payment_option_length; i++) {
        cb_convert_enum(in.payment_option_list.at(i), out.PaymentOptions.PaymentOption.array[i]);
    }
    out.PaymentOptions.PaymentOption.arrayLen = payment_option_length;

    if (in.service_list.has_value() && in.service_list.value().size() > 0) {
        // ServicesList size is bound to 1 in libcbv2g
        const auto& in_service = in.service_list.value().at(0);
        convert(in_service, out.ServiceList.Service);
        CB_SET_USED(out.ServiceList);
    }

    convert(in.charge_service, out.ChargeService);
}

template <> int serialize_to_exi(const ServiceDiscoveryResponse& in, exi_bitstream_t& out) {

    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);

    convert(in.header, doc.V2G_Message.Header);

    CB_SET_USED(doc.V2G_Message.Body.ServiceDiscoveryRes);
    convert(in, doc.V2G_Message.Body.ServiceDiscoveryRes);

    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const ServiceDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::din::msg
