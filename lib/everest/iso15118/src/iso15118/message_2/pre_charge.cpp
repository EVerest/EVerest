// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/pre_charge.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_PreChargeReqType& in, PreChargeRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    convert(in.EVTargetVoltage, out.ev_target_voltage);
    convert(in.EVTargetCurrent, out.ev_target_current);
}

template <> void convert(const struct iso2_PreChargeResType& in, PreChargeResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    convert(in.EVSEPresentVoltage, out.evse_present_voltage);
}

template <> void convert(const PreChargeRequest& in, struct iso2_PreChargeReqType& out) {
    init_iso2_PreChargeReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    convert(in.ev_target_voltage, out.EVTargetVoltage);
    convert(in.ev_target_current, out.EVTargetCurrent);
}

template <> void convert(const PreChargeResponse& in, struct iso2_PreChargeResType& out) {
    init_iso2_PreChargeResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    convert(in.evse_present_voltage, out.EVSEPresentVoltage);
}

template <> void insert_type(VariantAccess& va, const struct iso2_PreChargeReqType& in) {
    va.insert_type<PreChargeRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_PreChargeResType& in) {
    va.insert_type<PreChargeResponse>(in);
}

template <> int serialize_to_exi(const PreChargeRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PreChargeReq);
    convert(in, doc.V2G_Message.Body.PreChargeReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const PreChargeResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PreChargeRes);
    convert(in, doc.V2G_Message.Body.PreChargeRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const PreChargeRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const PreChargeResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
