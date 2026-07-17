// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/service_discovery.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

static void convert(const struct din_ServiceTagType& in, datatypes::ServiceTag& out) {
    out.service_id = in.ServiceID;
    CB2CPP_STRING_IF_USED(in.ServiceName, out.service_name);
    cb_convert_enum(in.ServiceCategory, out.service_category);
    CB2CPP_STRING_IF_USED(in.ServiceScope, out.service_scope);
}

static void convert(const datatypes::ServiceTag& in, struct din_ServiceTagType& out) {
    init_din_ServiceTagType(&out);
    out.ServiceID = in.service_id;
    CPP2CB_STRING_IF_USED(in.service_name, out.ServiceName);
    cb_convert_enum(in.service_category, out.ServiceCategory);
    CPP2CB_STRING_IF_USED(in.service_scope, out.ServiceScope);
}

template <> void convert(const struct din_ServiceDiscoveryReqType& in, ServiceDiscoveryRequest& out) {
    CB2CPP_STRING_IF_USED(in.ServiceScope, out.service_scope);
    if (in.ServiceCategory_isUsed) {
        cb_convert_enum(in.ServiceCategory, out.service_category.emplace());
    }
}

template <> void convert(const ServiceDiscoveryRequest& in, struct din_ServiceDiscoveryReqType& out) {
    init_din_ServiceDiscoveryReqType(&out);
    CPP2CB_STRING_IF_USED(in.service_scope, out.ServiceScope);
    if (in.service_category) {
        cb_convert_enum(in.service_category.value(), out.ServiceCategory);
    }
    out.ServiceCategory_isUsed = static_cast<bool>(in.service_category);
}

template <> void convert(const struct din_ServiceDiscoveryResType& in, ServiceDiscoveryResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);

    out.payment_options.clear();
    for (uint16_t i = 0; i < in.PaymentOptions.PaymentOption.arrayLen; i++) {
        datatypes::PaymentOption option;
        cb_convert_enum(in.PaymentOptions.PaymentOption.array[i], option);
        out.payment_options.push_back(option);
    }

    convert(in.ChargeService.ServiceTag, out.charge_service.service_tag);
    out.charge_service.free_service = in.ChargeService.FreeService;
    cb_convert_enum(in.ChargeService.EnergyTransferType, out.charge_service.energy_transfer_type);

    if (in.ServiceList_isUsed) {
        auto& service = out.service_list.emplace();
        convert(in.ServiceList.Service.ServiceTag, service.service_tag);
        service.free_service = in.ServiceList.Service.FreeService;
    }
}

template <> void convert(const ServiceDiscoveryResponse& in, struct din_ServiceDiscoveryResType& out) {
    init_din_ServiceDiscoveryResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);

    uint16_t index = 0;
    for (const auto& option : in.payment_options) {
        cb_convert_enum(option, out.PaymentOptions.PaymentOption.array[index++]);
    }
    out.PaymentOptions.PaymentOption.arrayLen = in.payment_options.size();

    convert(in.charge_service.service_tag, out.ChargeService.ServiceTag);
    out.ChargeService.FreeService = in.charge_service.free_service;
    cb_convert_enum(in.charge_service.energy_transfer_type, out.ChargeService.EnergyTransferType);

    if (in.service_list) {
        convert(in.service_list->service_tag, out.ServiceList.Service.ServiceTag);
        out.ServiceList.Service.FreeService = in.service_list->free_service;
    }
    out.ServiceList_isUsed = static_cast<bool>(in.service_list);
}

template <> void insert_type(VariantAccess& va, const struct din_ServiceDiscoveryReqType& in) {
    va.insert_type<ServiceDiscoveryRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_ServiceDiscoveryResType& in) {
    va.insert_type<ServiceDiscoveryResponse>(in);
}

template <> int serialize_to_exi(const ServiceDiscoveryRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServiceDiscoveryReq);
    convert(in, doc.V2G_Message.Body.ServiceDiscoveryReq);
    return encode_din_exiDocument(&out, &doc);
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

template <> size_t serialize(const ServiceDiscoveryRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ServiceDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
