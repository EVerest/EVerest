// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <iso15118/message/din/msg_data_types.hpp>

#include <iso15118/detail/cb_exi.hpp>

namespace iso15118::din::msg {

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

} // namespace iso15118::din::msg
