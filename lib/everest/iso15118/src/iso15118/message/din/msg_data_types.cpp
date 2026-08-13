// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cmath>

#include <iso15118/message/din/msg_data_types.hpp>

#include <iso15118/detail/cb_exi.hpp>

namespace iso15118::din::msg {

namespace data_types {

float from_PhysicalValue(const PhysicalValue& in) {
    return in.value * pow(10, in.multiplier);
}

PhysicalValue from_float(const float in, const data_types::UnitSymbol unit) {
    PhysicalValue out;
    out.unit = unit;
    if (in == 0.0) {
        out.multiplier = 0;
        out.value = 0;
        return out;
    }
    out.multiplier = static_cast<int8_t>(floor(log10(fabs(in))));
    out.multiplier -= 3; // add 3 digits of precision
    out.value = static_cast<int16_t>(in * pow(10, -out.multiplier));
    return out;
}

}; // namespace data_types

void convert(const din_NotificationType& in, data_types::Notification& out) {
    cb_convert_enum(in.FaultCode, out.fault_code);
    CB2CPP_STRING_IF_USED(in.FaultMsg, out.fault_msg);
}

void convert(const data_types::Notification& in, struct din_NotificationType& out) {
    cb_convert_enum(in.fault_code, out.FaultCode);
    CPP2CB_STRING_IF_USED(in.fault_msg, out.FaultMsg);
}

void convert(const Header& in, struct din_MessageHeaderType& out) {
    init_din_MessageHeaderType(&out);
    CPP2CB_BYTES(in.session_id, out.SessionID);
    CPP2CB_CONVERT_IF_USED(in.notification, out.Notification);
}

void convert(const struct din_MessageHeaderType& in, Header& out) {
    CB2CPP_BYTES(in.SessionID, out.session_id);
    CB2CPP_CONVERT_IF_USED(in.Notification, out.notification);
}
template <> void convert(const din_DC_EVStatusType& in, data_types::DcEvStatus& out) {
    out.ev_ready = in.EVReady;
    cb_convert_enum(in.EVErrorCode, out.ev_error_code);
    out.ev_ress_soc = in.EVRESSSOC;
}

template <> void convert(const data_types::AcEvseStatus& in, din_AC_EVSEStatusType& out) {
    init_din_AC_EVSEStatusType(&out);
    cb_convert_enum(in.evse_notification, out.EVSENotification);
    out.NotificationMaxDelay = in.notification_max_delay;
    out.RCD = in.rcd;
    out.PowerSwitchClosed = in.power_switch_closed;
}

template <> void convert(const data_types::DcEvseStatus& in, din_DC_EVSEStatusType& out) {
    init_din_DC_EVSEStatusType(&out);
    cb_convert_enum(in.evse_notification, out.EVSENotification);
    out.NotificationMaxDelay = in.notification_max_delay;
    if (in.evse_isolation_status.has_value()) {
        cb_convert_enum(in.evse_isolation_status.value(), out.EVSEIsolationStatus);
        CB_SET_USED(out.EVSEIsolationStatus);
    }
    cb_convert_enum(in.evse_status_code, out.EVSEStatusCode);
}

template <> void convert(const din_PhysicalValueType& in, data_types::PhysicalValue& out) {
    out.multiplier = in.Multiplier;
    out.value = in.Value;
    if (in.Unit_isUsed) {
        out.unit.emplace();
        cb_convert_enum(in.Unit, out.unit.value());
    }
}

template <> void convert(const data_types::PhysicalValue& in, din_PhysicalValueType& out) {
    init_din_PhysicalValueType(&out);
    out.Multiplier = in.multiplier;
    out.Value = in.value;
    if (in.unit.has_value()) {
        cb_convert_enum(in.unit.value(), out.Unit);
        CB_SET_USED(out.Unit);
    }
}

} // namespace iso15118::din::msg
