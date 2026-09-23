// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_sae_names.hpp>

namespace iso15118::message_20::datatypes::sae {

// No default: under ISO15118_COMPILE_OPTIONS_WARNING (-Wall -Werror) a missed enumerator fails the build.
std::string_view to_string(IEEE1547NormalCategory value) {
    switch (value) {
    case IEEE1547NormalCategory::CategoryA:
        return "CategoryA";
    case IEEE1547NormalCategory::CategoryB:
        return "CategoryB";
    }
    return {};
}

std::string_view to_string(IEEE1547AbnormalCategory value) {
    switch (value) {
    case IEEE1547AbnormalCategory::CategoryI:
        return "CategoryI";
    case IEEE1547AbnormalCategory::CategoryII:
        return "CategoryII";
    case IEEE1547AbnormalCategory::CategoryIII:
        return "CategoryIII";
    }
    return {};
}

std::string_view to_string(DEROperationalState value) {
    switch (value) {
    case DEROperationalState::On:
        return "On";
    case DEROperationalState::Off:
        return "Off";
    }
    return {};
}

std::string_view to_string(DERConnectionStatus value) {
    switch (value) {
    case DERConnectionStatus::Disconnected:
        return "Disconnected";
    case DERConnectionStatus::Connected:
        return "Connected";
    }
    return {};
}

} // namespace iso15118::message_20::datatypes::sae
