// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/service_discovery.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_ServiceDiscoveryReqType& in, ServiceDiscoveryRequest& out) {
    CB2CPP_STRING_IF_USED(in.ServiceScope, out.service_scope);
    if (in.ServiceCategory_isUsed) {
        cb_convert_enum(in.ServiceCategory, out.service_category.emplace());
    }
}

template <> void convert(const struct iso2_ServiceDiscoveryResType& in, ServiceDiscoveryResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);

    out.payment_option_list.clear();
    for (uint16_t i = 0; i < in.PaymentOptionList.PaymentOption.arrayLen; i++) {
        datatypes::PaymentOption option;
        cb_convert_enum(in.PaymentOptionList.PaymentOption.array[i], option);
        out.payment_option_list.push_back(option);
    }

    const auto& cs = in.ChargeService;
    out.charge_service.service_id = cs.ServiceID;
    if (cs.ServiceName_isUsed) {
        out.charge_service.service_name = CB2CPP_STRING(cs.ServiceName);
    }
    cb_convert_enum(cs.ServiceCategory, out.charge_service.service_category);
    if (cs.ServiceScope_isUsed) {
        out.charge_service.service_scope = CB2CPP_STRING(cs.ServiceScope);
    }
    out.charge_service.free_service = cs.FreeService;
    out.charge_service.supported_energy_transfer_mode.clear();
    for (uint16_t i = 0; i < cs.SupportedEnergyTransferMode.EnergyTransferMode.arrayLen; i++) {
        datatypes::EnergyTransferMode mode;
        cb_convert_enum(cs.SupportedEnergyTransferMode.EnergyTransferMode.array[i], mode);
        out.charge_service.supported_energy_transfer_mode.push_back(mode);
    }

    if (in.ServiceList_isUsed) {
        auto& list = out.service_list.emplace();
        for (uint16_t i = 0; i < in.ServiceList.Service.arrayLen; i++) {
            const auto& service = in.ServiceList.Service.array[i];
            auto& out_service = list.emplace_back();
            out_service.service_id = service.ServiceID;
            if (service.ServiceName_isUsed) {
                out_service.service_name = CB2CPP_STRING(service.ServiceName);
            }
            cb_convert_enum(service.ServiceCategory, out_service.service_category);
            if (service.ServiceScope_isUsed) {
                out_service.service_scope = CB2CPP_STRING(service.ServiceScope);
            }
            out_service.free_service = service.FreeService;
        }
    }
}

template <> void convert(const ServiceDiscoveryRequest& in, struct iso2_ServiceDiscoveryReqType& out) {
    init_iso2_ServiceDiscoveryReqType(&out);
    CPP2CB_STRING_IF_USED(in.service_scope, out.ServiceScope);
    if (in.service_category) {
        cb_convert_enum(in.service_category.value(), out.ServiceCategory);
    }
    out.ServiceCategory_isUsed = static_cast<bool>(in.service_category);
}

template <> void convert(const ServiceDiscoveryResponse& in, struct iso2_ServiceDiscoveryResType& out) {
    init_iso2_ServiceDiscoveryResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);

    uint16_t index = 0;
    for (const auto& option : in.payment_option_list) {
        cb_convert_enum(option, out.PaymentOptionList.PaymentOption.array[index++]);
    }
    out.PaymentOptionList.PaymentOption.arrayLen = in.payment_option_list.size();

    auto& cs = out.ChargeService;
    init_iso2_ChargeServiceType(&cs);
    cs.ServiceID = in.charge_service.service_id;
    if (in.charge_service.service_name) {
        CPP2CB_STRING(in.charge_service.service_name.value(), cs.ServiceName);
        CB_SET_USED(cs.ServiceName);
    }
    cb_convert_enum(in.charge_service.service_category, cs.ServiceCategory);
    if (in.charge_service.service_scope) {
        CPP2CB_STRING(in.charge_service.service_scope.value(), cs.ServiceScope);
        CB_SET_USED(cs.ServiceScope);
    }
    cs.FreeService = in.charge_service.free_service;
    index = 0;
    for (const auto& mode : in.charge_service.supported_energy_transfer_mode) {
        cb_convert_enum(mode, cs.SupportedEnergyTransferMode.EnergyTransferMode.array[index++]);
    }
    cs.SupportedEnergyTransferMode.EnergyTransferMode.arrayLen =
        in.charge_service.supported_energy_transfer_mode.size();

    if (in.service_list) {
        index = 0;
        for (const auto& service : *in.service_list) {
            auto& out_service = out.ServiceList.Service.array[index++];
            init_iso2_ServiceType(&out_service);
            out_service.ServiceID = service.service_id;
            if (service.service_name) {
                CPP2CB_STRING(service.service_name.value(), out_service.ServiceName);
                CB_SET_USED(out_service.ServiceName);
            }
            cb_convert_enum(service.service_category, out_service.ServiceCategory);
            if (service.service_scope) {
                CPP2CB_STRING(service.service_scope.value(), out_service.ServiceScope);
                CB_SET_USED(out_service.ServiceScope);
            }
            out_service.FreeService = service.free_service;
        }
        out.ServiceList.Service.arrayLen = in.service_list->size();
        CB_SET_USED(out.ServiceList);
    }
}

template <> void insert_type(VariantAccess& va, const struct iso2_ServiceDiscoveryReqType& in) {
    va.insert_type<ServiceDiscoveryRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_ServiceDiscoveryResType& in) {
    va.insert_type<ServiceDiscoveryResponse>(in);
}

template <> int serialize_to_exi(const ServiceDiscoveryRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServiceDiscoveryReq);
    convert(in, doc.V2G_Message.Body.ServiceDiscoveryReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const ServiceDiscoveryResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServiceDiscoveryRes);
    convert(in, doc.V2G_Message.Body.ServiceDiscoveryRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const ServiceDiscoveryRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ServiceDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
