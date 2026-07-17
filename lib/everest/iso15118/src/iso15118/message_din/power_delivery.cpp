// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/power_delivery.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

static void convert(const struct din_DC_EVPowerDeliveryParameterType& in, datatypes::DcEvPowerDeliveryParameter& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    CB2CPP_ASSIGN_IF_USED(in.BulkChargingComplete, out.bulk_charging_complete);
    out.charging_complete = in.ChargingComplete;
}

static void convert(const datatypes::DcEvPowerDeliveryParameter& in, struct din_DC_EVPowerDeliveryParameterType& out) {
    init_din_DC_EVPowerDeliveryParameterType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    CPP2CB_ASSIGN_IF_USED(in.bulk_charging_complete, out.BulkChargingComplete);
    out.ChargingComplete = in.charging_complete;
}

template <> void convert(const struct din_PowerDeliveryReqType& in, PowerDeliveryRequest& out) {
    out.ready_to_charge_state = in.ReadyToChargeState;
    if (in.DC_EVPowerDeliveryParameter_isUsed) {
        convert(in.DC_EVPowerDeliveryParameter, out.dc_ev_power_delivery_parameter.emplace());
    }
}

template <> void convert(const PowerDeliveryRequest& in, struct din_PowerDeliveryReqType& out) {
    init_din_PowerDeliveryReqType(&out);
    out.ReadyToChargeState = in.ready_to_charge_state;
    if (in.dc_ev_power_delivery_parameter) {
        convert(in.dc_ev_power_delivery_parameter.value(), out.DC_EVPowerDeliveryParameter);
        CB_SET_USED(out.DC_EVPowerDeliveryParameter);
    }
}

template <> void convert(const struct din_PowerDeliveryResType& in, PowerDeliveryResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    if (in.DC_EVSEStatus_isUsed) {
        convert(in.DC_EVSEStatus, out.dc_evse_status.emplace());
    }
    if (in.AC_EVSEStatus_isUsed) {
        convert(in.AC_EVSEStatus, out.ac_evse_status.emplace());
    }
}

template <> void convert(const PowerDeliveryResponse& in, struct din_PowerDeliveryResType& out) {
    init_din_PowerDeliveryResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    if (in.dc_evse_status) {
        convert(in.dc_evse_status.value(), out.DC_EVSEStatus);
        CB_SET_USED(out.DC_EVSEStatus);
    }
    if (in.ac_evse_status) {
        convert(in.ac_evse_status.value(), out.AC_EVSEStatus);
        CB_SET_USED(out.AC_EVSEStatus);
    }
}

template <> void insert_type(VariantAccess& va, const struct din_PowerDeliveryReqType& in) {
    va.insert_type<PowerDeliveryRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_PowerDeliveryResType& in) {
    va.insert_type<PowerDeliveryResponse>(in);
}

template <> int serialize_to_exi(const PowerDeliveryRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PowerDeliveryReq);
    convert(in, doc.V2G_Message.Body.PowerDeliveryReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const PowerDeliveryResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PowerDeliveryRes);
    convert(in, doc.V2G_Message.Body.PowerDeliveryRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const PowerDeliveryRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const PowerDeliveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
