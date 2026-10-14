// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>
#include <vector>

#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief The device model entries an OCPP connection is allowed to write.
///
/// Built from an OCPPCommCtrlr[<connection>].AllowSetVariables value: a comma separated list of
/// device model hierarchy entries (OCA application note "Secondary OCPP Connection on a Charging
/// Station", section 3.6). An entry naming only a component allows every variable of that
/// component; an entry naming a variable allows just that one.
///
/// Empty by default, and an empty list allows nothing: by default a secondary system may not change
/// any device model variable at all.
class VariablePermissionList {
public:
    /// \brief Parses an AllowSetVariables value.
    ///
    /// Lenient in the same way as the message allow lists: a malformed entry is logged and skipped
    /// rather than voiding the rest of the list, so one typo cannot silently grant or revoke
    /// everything else.
    static VariablePermissionList parse(const std::string& csv);

    /// \brief Whether writing \p variable of \p component is allowed.
    ///
    /// An omitted instance, EVSE or connector in an entry names the component or variable that has
    /// none, rather than matching any, so a grant cannot widen itself by being unspecific.
    [[nodiscard]] bool allows(const Component& component, const Variable& variable) const;

    /// \brief Whether the list grants nothing at all.
    [[nodiscard]] bool empty() const;

private:
    // Each entry is a component, and optionally one variable of it; a std::nullopt variable means
    // the whole component.
    std::vector<ComponentVariable> entries;
};

} // namespace v2
} // namespace ocpp
