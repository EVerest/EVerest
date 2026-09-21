// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/service_detail.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

static void convert_parameter(const struct iso2_ParameterType& in, datatypes::Parameter& out) {
    out.name = CB2CPP_STRING(in.Name);
    if (in.boolValue_isUsed) {
        out.bool_value = static_cast<bool>(in.boolValue);
    }
    CB2CPP_ASSIGN_IF_USED(in.byteValue, out.byte_value);
    CB2CPP_ASSIGN_IF_USED(in.shortValue, out.short_value);
    CB2CPP_ASSIGN_IF_USED(in.intValue, out.int_value);
    if (in.physicalValue_isUsed) {
        convert(in.physicalValue, out.physical_value.emplace());
    }
    if (in.stringValue_isUsed) {
        out.string_value = CB2CPP_STRING(in.stringValue);
    }
}

static void convert_parameter(const datatypes::Parameter& in, struct iso2_ParameterType& out) {
    init_iso2_ParameterType(&out);
    CPP2CB_STRING(in.name, out.Name);
    if (in.bool_value) {
        out.boolValue = in.bool_value.value();
        CB_SET_USED(out.boolValue);
    }
    CPP2CB_ASSIGN_IF_USED(in.byte_value, out.byteValue);
    CPP2CB_ASSIGN_IF_USED(in.short_value, out.shortValue);
    CPP2CB_ASSIGN_IF_USED(in.int_value, out.intValue);
    if (in.physical_value) {
        convert(in.physical_value.value(), out.physicalValue);
        CB_SET_USED(out.physicalValue);
    }
    if (in.string_value) {
        CPP2CB_STRING(in.string_value.value(), out.stringValue);
        CB_SET_USED(out.stringValue);
    }
}

template <> void convert(const struct iso2_ServiceDetailReqType& in, ServiceDetailRequest& out) {
    out.service_id = in.ServiceID;
}

template <> void convert(const struct iso2_ServiceDetailResType& in, ServiceDetailResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    out.service_id = in.ServiceID;
    if (in.ServiceParameterList_isUsed) {
        auto& list = out.service_parameter_list.emplace();
        for (uint16_t i = 0; i < in.ServiceParameterList.ParameterSet.arrayLen; i++) {
            const auto& set = in.ServiceParameterList.ParameterSet.array[i];
            auto& out_set = list.emplace_back();
            out_set.parameter_set_id = set.ParameterSetID;
            for (uint16_t j = 0; j < set.Parameter.arrayLen; j++) {
                convert_parameter(set.Parameter.array[j], out_set.parameter.emplace_back());
            }
        }
    }
}

template <> void convert(const ServiceDetailRequest& in, struct iso2_ServiceDetailReqType& out) {
    init_iso2_ServiceDetailReqType(&out);
    out.ServiceID = in.service_id;
}

template <> void convert(const ServiceDetailResponse& in, struct iso2_ServiceDetailResType& out) {
    init_iso2_ServiceDetailResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    out.ServiceID = in.service_id;
    if (in.service_parameter_list) {
        uint16_t set_index = 0;
        for (const auto& set : *in.service_parameter_list) {
            auto& out_set = out.ServiceParameterList.ParameterSet.array[set_index++];
            init_iso2_ParameterSetType(&out_set);
            out_set.ParameterSetID = set.parameter_set_id;
            uint16_t param_index = 0;
            for (const auto& param : set.parameter) {
                convert_parameter(param, out_set.Parameter.array[param_index++]);
            }
            out_set.Parameter.arrayLen = set.parameter.size();
        }
        out.ServiceParameterList.ParameterSet.arrayLen = in.service_parameter_list->size();
        CB_SET_USED(out.ServiceParameterList);
    }
}

template <> void insert_type(VariantAccess& va, const struct iso2_ServiceDetailReqType& in) {
    va.insert_type<ServiceDetailRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_ServiceDetailResType& in) {
    va.insert_type<ServiceDetailResponse>(in);
}

template <> int serialize_to_exi(const ServiceDetailRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServiceDetailReq);
    convert(in, doc.V2G_Message.Body.ServiceDetailReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const ServiceDetailResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ServiceDetailRes);
    convert(in, doc.V2G_Message.Body.ServiceDetailRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const ServiceDetailRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ServiceDetailResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
