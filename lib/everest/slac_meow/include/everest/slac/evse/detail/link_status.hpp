// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/states.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>

// Vendor specific data link supervision. WaitForLink leaves on a positive answer, Matched on a
// negative one; the polling itself is the same.
namespace everest::slac::evse {

enum class LinkCheckMode {
    /// No supervision: either disabled by configuration, or an unknown modem vendor.
    None,
    Lumissil,
    Qualcomm,
};

LinkCheckMode link_check_mode_for(defs::ModemVendor vendor);

/// A no-op in mode None.
void send_link_status_req(Context& ctx, LinkCheckMode mode);

/// `linked` is only meaningful when the return value is true.
bool parse_link_status_cnf(messages::HomeplugMessage const& frame, LinkCheckMode mode, bool& linked);

bool is_link_up(messages::HomeplugMessage const& frame, LinkCheckMode mode);

/// A truncated answer is neither up nor down.
bool is_link_down(messages::HomeplugMessage const& frame, LinkCheckMode mode);

} // namespace everest::slac::evse
