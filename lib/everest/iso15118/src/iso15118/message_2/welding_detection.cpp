// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/welding_detection.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_WeldingDetectionReqType& in, WeldingDetectionRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
}

template <> void convert(const struct iso2_WeldingDetectionResType& in, WeldingDetectionResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    convert(in.EVSEPresentVoltage, out.evse_present_voltage);
}

template <> void convert(const WeldingDetectionRequest& in, struct iso2_WeldingDetectionReqType& out) {
    init_iso2_WeldingDetectionReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
}

template <> void convert(const WeldingDetectionResponse& in, struct iso2_WeldingDetectionResType& out) {
    init_iso2_WeldingDetectionResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    convert(in.evse_present_voltage, out.EVSEPresentVoltage);
}

template <> void insert_type(VariantAccess& va, const struct iso2_WeldingDetectionReqType& in) {
    va.insert_type<WeldingDetectionRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_WeldingDetectionResType& in) {
    va.insert_type<WeldingDetectionResponse>(in);
}

template <> int serialize_to_exi(const WeldingDetectionRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.WeldingDetectionReq);
    convert(in, doc.V2G_Message.Body.WeldingDetectionReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const WeldingDetectionResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.WeldingDetectionRes);
    convert(in, doc.V2G_Message.Body.WeldingDetectionRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const WeldingDetectionRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const WeldingDetectionResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
