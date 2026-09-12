// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <cmath>

#include <iso15118/message_din/common_types.hpp>

#include <iso15118/detail/cb_exi.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>

namespace iso15118::message_din {

template <typename cb_HeaderType> void convert(const cb_HeaderType& in, Header& out) {
    std::copy(in.SessionID.bytes, in.SessionID.bytes + in.SessionID.bytesLen, out.session_id.begin());
    // Notification and Signature are intentionally decoded-and-dropped: EvseV2G never populates the SECC
    // header Notification/FaultCode, and the EVCC state tree does not consume it.
}

template void convert(const struct din_MessageHeaderType& in, Header& out);

template <> void convert(const Header& in, din_MessageHeaderType& out) {
    init_din_MessageHeaderType(&out);
    std::copy(in.session_id.begin(), in.session_id.end(), out.SessionID.bytes);
    out.SessionID.bytesLen = datatypes::SESSION_ID_LENGTH;
}

din_PhysicalValueType to_physical_value(double value, datatypes::Unit unit) {
    din_PhysicalValueType out;
    init_din_PhysicalValueType(&out);

    // DIN PhysicalValue Multiplier is a signed exponent constrained to [-3, 3].
    constexpr int8_t MULTIPLIER_MIN = -3;
    constexpr int8_t MULTIPLIER_MAX = 3;
    constexpr double INT16_MAX_D = 32767.0;
    constexpr double INT16_MIN_D = -32768.0;

    int8_t multiplier = 0;
    double scaled = value;

    // Scale down large magnitudes (positive multiplier) so the mantissa fits into int16.
    while (std::fabs(scaled) > INT16_MAX_D && multiplier < MULTIPLIER_MAX) {
        scaled /= 10.0;
        ++multiplier;
    }

    // Scale up fractional values (negative multiplier) to retain sub-unit precision, keeping the mantissa
    // within int16 range (|scaled| stays <= 32767 because we stop below 3276.7 before multiplying).
    while (multiplier > MULTIPLIER_MIN && std::fabs(scaled) < 3276.7 && scaled != std::floor(scaled)) {
        scaled *= 10.0;
        --multiplier;
    }

    // Guard against int16 overflow (e.g. a value too large even at multiplier +3).
    double clamped = std::round(scaled);
    if (clamped > INT16_MAX_D) {
        clamped = INT16_MAX_D;
    } else if (clamped < INT16_MIN_D) {
        clamped = INT16_MIN_D;
    }

    out.Value = static_cast<int16_t>(clamped);
    out.Multiplier = multiplier;
    out.Unit = static_cast<din_unitSymbolType>(unit);
    out.Unit_isUsed = 1;
    return out;
}

double from_physical_value(const din_PhysicalValueType& in) {
    return in.Value * std::pow(10.0, in.Multiplier);
}

template <> void convert(const struct din_DC_EVStatusType& in, datatypes::DcEvStatus& out) {
    out.ev_ready = in.EVReady;
    cb_convert_enum(in.EVErrorCode, out.ev_error_code);
    out.ev_ress_soc = in.EVRESSSOC;
    CB2CPP_ASSIGN_IF_USED(in.EVCabinConditioning, out.ev_cabin_conditioning);
    CB2CPP_ASSIGN_IF_USED(in.EVRESSConditioning, out.ev_ress_conditioning);
}

template <> void convert(const datatypes::DcEvStatus& in, struct din_DC_EVStatusType& out) {
    init_din_DC_EVStatusType(&out);
    out.EVReady = in.ev_ready;
    cb_convert_enum(in.ev_error_code, out.EVErrorCode);
    out.EVRESSSOC = in.ev_ress_soc;
    CPP2CB_ASSIGN_IF_USED(in.ev_cabin_conditioning, out.EVCabinConditioning);
    CPP2CB_ASSIGN_IF_USED(in.ev_ress_conditioning, out.EVRESSConditioning);
}

template <> void convert(const struct din_DC_EVSEStatusType& in, datatypes::DcEvseStatus& out) {
    if (in.EVSEIsolationStatus_isUsed) {
        cb_convert_enum(in.EVSEIsolationStatus, out.evse_isolation_status.emplace());
    }
    cb_convert_enum(in.EVSEStatusCode, out.evse_status_code);
    out.notification_max_delay = in.NotificationMaxDelay;
    cb_convert_enum(in.EVSENotification, out.evse_notification);
}

template <> void convert(const datatypes::DcEvseStatus& in, struct din_DC_EVSEStatusType& out) {
    init_din_DC_EVSEStatusType(&out);
    if (in.evse_isolation_status) {
        cb_convert_enum(in.evse_isolation_status.value(), out.EVSEIsolationStatus);
        CB_SET_USED(out.EVSEIsolationStatus);
    }
    cb_convert_enum(in.evse_status_code, out.EVSEStatusCode);
    out.NotificationMaxDelay = in.notification_max_delay;
    cb_convert_enum(in.evse_notification, out.EVSENotification);
}

template <> void convert(const struct din_AC_EVSEStatusType& in, datatypes::AcEvseStatus& out) {
    out.power_switch_closed = in.PowerSwitchClosed;
    out.rcd = in.RCD;
    out.notification_max_delay = in.NotificationMaxDelay;
    cb_convert_enum(in.EVSENotification, out.evse_notification);
}

template <> void convert(const datatypes::AcEvseStatus& in, struct din_AC_EVSEStatusType& out) {
    init_din_AC_EVSEStatusType(&out);
    out.PowerSwitchClosed = in.power_switch_closed;
    out.RCD = in.rcd;
    out.NotificationMaxDelay = in.notification_max_delay;
    cb_convert_enum(in.evse_notification, out.EVSENotification);
}

} // namespace iso15118::message_din
