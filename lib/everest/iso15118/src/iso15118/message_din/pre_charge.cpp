// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/pre_charge.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

using datatypes::Unit;

template <> void convert(const struct din_PreChargeReqType& in, PreChargeRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    out.ev_target_voltage = from_physical_value(in.EVTargetVoltage);
    out.ev_target_current = from_physical_value(in.EVTargetCurrent);
}

template <> void convert(const PreChargeRequest& in, struct din_PreChargeReqType& out) {
    init_din_PreChargeReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    out.EVTargetVoltage = to_physical_value(in.ev_target_voltage, Unit::V);
    out.EVTargetCurrent = to_physical_value(in.ev_target_current, Unit::A);
}

template <> void convert(const struct din_PreChargeResType& in, PreChargeResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    out.evse_present_voltage = from_physical_value(in.EVSEPresentVoltage);
}

template <> void convert(const PreChargeResponse& in, struct din_PreChargeResType& out) {
    init_din_PreChargeResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    out.EVSEPresentVoltage = to_physical_value(in.evse_present_voltage, Unit::V);
}

template <> void insert_type(VariantAccess& va, const struct din_PreChargeReqType& in) {
    va.insert_type<PreChargeRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_PreChargeResType& in) {
    va.insert_type<PreChargeResponse>(in);
}

template <> int serialize_to_exi(const PreChargeRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PreChargeReq);
    convert(in, doc.V2G_Message.Body.PreChargeReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const PreChargeResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PreChargeRes);
    convert(in, doc.V2G_Message.Body.PreChargeRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const PreChargeRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const PreChargeResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
