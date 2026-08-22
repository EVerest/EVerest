// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/welding_detection.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

using datatypes::Unit;

template <> void convert(const struct din_WeldingDetectionReqType& in, WeldingDetectionRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
}

template <> void convert(const WeldingDetectionRequest& in, struct din_WeldingDetectionReqType& out) {
    init_din_WeldingDetectionReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
}

template <> void convert(const struct din_WeldingDetectionResType& in, WeldingDetectionResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    out.evse_present_voltage = from_physical_value(in.EVSEPresentVoltage);
}

template <> void convert(const WeldingDetectionResponse& in, struct din_WeldingDetectionResType& out) {
    init_din_WeldingDetectionResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    out.EVSEPresentVoltage = to_physical_value(in.evse_present_voltage, Unit::V);
}

template <> void insert_type(VariantAccess& va, const struct din_WeldingDetectionReqType& in) {
    va.insert_type<WeldingDetectionRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_WeldingDetectionResType& in) {
    va.insert_type<WeldingDetectionResponse>(in);
}

template <> int serialize_to_exi(const WeldingDetectionRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.WeldingDetectionReq);
    convert(in, doc.V2G_Message.Body.WeldingDetectionReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const WeldingDetectionResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.WeldingDetectionRes);
    convert(in, doc.V2G_Message.Body.WeldingDetectionRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const WeldingDetectionRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const WeldingDetectionResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
