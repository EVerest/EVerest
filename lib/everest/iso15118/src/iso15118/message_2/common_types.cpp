// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <cmath>
#include <limits>

#include <iso15118/message_2/common_types.hpp>

#include <iso15118/detail/cb_exi.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_NotificationType& in, datatypes::Notification& out) {
    cb_convert_enum(in.FaultCode, out.fault_code);
    if (in.FaultMsg_isUsed) {
        out.fault_msg = CB2CPP_STRING(in.FaultMsg);
    }
}

template <> void convert(const datatypes::Notification& in, struct iso2_NotificationType& out) {
    init_iso2_NotificationType(&out);
    cb_convert_enum(in.fault_code, out.FaultCode);
    if (in.fault_msg) {
        CPP2CB_STRING(in.fault_msg.value(), out.FaultMsg);
        CB_SET_USED(out.FaultMsg);
    }
}

template <typename cb_HeaderType> void convert(const cb_HeaderType& in, Header& out) {
    std::copy(in.SessionID.bytes, in.SessionID.bytes + in.SessionID.bytesLen, out.session_id.begin());
    CB2CPP_CONVERT_IF_USED(in.Notification, out.notification);
    // Signature is skipped (EIM only) but tolerated on decode
}

template void convert(const struct iso2_MessageHeaderType& in, Header& out);

template <typename cb_HeaderType> void convert(const Header& in, cb_HeaderType& out) {
    init_iso2_MessageHeaderType(&out);
    std::copy(in.session_id.begin(), in.session_id.end(), out.SessionID.bytes);
    out.SessionID.bytesLen = datatypes::SESSION_ID_LENGTH;
    CPP2CB_CONVERT_IF_USED(in.notification, out.Notification);
}

template void convert(const Header& in, struct iso2_MessageHeaderType& out);

template <typename cb_PhysicalValueType> void convert(const cb_PhysicalValueType& in, datatypes::PhysicalValue& out) {
    out.value = in.Value;
    out.multiplier = in.Multiplier;
    cb_convert_enum(in.Unit, out.unit);
}

template void convert(const struct iso2_PhysicalValueType& in, datatypes::PhysicalValue& out);

template <typename cb_PhysicalValueType> void convert(const datatypes::PhysicalValue& in, cb_PhysicalValueType& out) {
    init_iso2_PhysicalValueType(&out);
    out.Value = in.value;
    out.Multiplier = in.multiplier;
    cb_convert_enum(in.unit, out.Unit);
}

template void convert(const datatypes::PhysicalValue& in, struct iso2_PhysicalValueType& out);

template <> void convert(const struct iso2_DC_EVStatusType& in, datatypes::DC_EVStatus& out) {
    out.ev_ready = in.EVReady;
    cb_convert_enum(in.EVErrorCode, out.ev_error_code);
    out.ev_ress_soc = in.EVRESSSOC;
}

template <> void convert(const datatypes::DC_EVStatus& in, struct iso2_DC_EVStatusType& out) {
    init_iso2_DC_EVStatusType(&out);
    out.EVReady = in.ev_ready;
    cb_convert_enum(in.ev_error_code, out.EVErrorCode);
    out.EVRESSSOC = in.ev_ress_soc;
}

template <> void convert(const struct iso2_DC_EVSEStatusType& in, datatypes::DC_EVSEStatus& out) {
    out.notification_max_delay = in.NotificationMaxDelay;
    cb_convert_enum(in.EVSENotification, out.notification);
    if (in.EVSEIsolationStatus_isUsed) {
        cb_convert_enum(in.EVSEIsolationStatus, out.isolation_status.emplace());
    }
    cb_convert_enum(in.EVSEStatusCode, out.status_code);
}

template <> void convert(const datatypes::DC_EVSEStatus& in, struct iso2_DC_EVSEStatusType& out) {
    init_iso2_DC_EVSEStatusType(&out);
    out.NotificationMaxDelay = in.notification_max_delay;
    cb_convert_enum(in.notification, out.EVSENotification);
    if (in.isolation_status) {
        cb_convert_enum(in.isolation_status.value(), out.EVSEIsolationStatus);
        CB_SET_USED(out.EVSEIsolationStatus);
    }
    cb_convert_enum(in.status_code, out.EVSEStatusCode);
}

template <> void convert(const struct iso2_AC_EVSEStatusType& in, datatypes::AC_EVSEStatus& out) {
    out.notification_max_delay = in.NotificationMaxDelay;
    cb_convert_enum(in.EVSENotification, out.notification);
    out.rcd = in.RCD;
}

template <> void convert(const datatypes::AC_EVSEStatus& in, struct iso2_AC_EVSEStatusType& out) {
    init_iso2_AC_EVSEStatusType(&out);
    out.NotificationMaxDelay = in.notification_max_delay;
    cb_convert_enum(in.notification, out.EVSENotification);
    out.RCD = in.rcd;
}

template <> void convert(const struct iso2_MeterInfoType& in, datatypes::MeterInfo& out) {
    out.meter_id = CB2CPP_STRING(in.MeterID);
    CB2CPP_ASSIGN_IF_USED(in.MeterReading, out.meter_reading);
    CB2CPP_BYTES_IF_USED(in.SigMeterReading, out.sig_meter_reading);
    CB2CPP_ASSIGN_IF_USED(in.MeterStatus, out.meter_status);
    CB2CPP_ASSIGN_IF_USED(in.TMeter, out.t_meter);
}

template <> void convert(const datatypes::MeterInfo& in, struct iso2_MeterInfoType& out) {
    init_iso2_MeterInfoType(&out);
    CPP2CB_STRING(in.meter_id, out.MeterID);
    CPP2CB_ASSIGN_IF_USED(in.meter_reading, out.MeterReading);
    if (in.sig_meter_reading) {
        CPP2CB_BYTES(in.sig_meter_reading.value(), out.SigMeterReading);
        CB_SET_USED(out.SigMeterReading);
    }
    CPP2CB_ASSIGN_IF_USED(in.meter_status, out.MeterStatus);
    CPP2CB_ASSIGN_IF_USED(in.t_meter, out.TMeter);
}

namespace datatypes {

double from_physical_value(const PhysicalValue& in) {
    return in.value * pow(10, in.multiplier);
}

PhysicalValue to_physical_value(double value, Unit unit) {
    PhysicalValue out;
    out.unit = unit;
    if (value == 0.0) {
        out.multiplier = 0;
        out.value = 0;
        return out;
    }
    int multiplier = static_cast<int>(floor(log10(fabs(value))));
    multiplier -= 3; // add 3 digits of precision
    // ISO 15118-2 PhysicalValueType constrains the multiplier to -3..3.
    multiplier = std::clamp(multiplier, -3, 3);
    // Guard the int16 Value range in case the clamped multiplier cannot represent the magnitude.
    double scaled = value * pow(10, -multiplier);
    scaled = std::clamp(scaled, static_cast<double>(std::numeric_limits<int16_t>::min()),
                        static_cast<double>(std::numeric_limits<int16_t>::max()));
    out.multiplier = static_cast<int8_t>(multiplier);
    out.value = static_cast<int16_t>(scaled);
    return out;
}

} // namespace datatypes

} // namespace iso15118::message_2
