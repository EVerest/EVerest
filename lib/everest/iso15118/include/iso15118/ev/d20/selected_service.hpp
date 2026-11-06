// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

// What ServiceDetail and ServiceSelection settled. The SECC keeps it across a pause and
// continues with it after OK_OldSessionJoined, so a resumed session skips straight to
// ChargeParameterDiscovery.
struct SelectedService {
    message_20::datatypes::ServiceCategory service;
    message_20::datatypes::ControlMode control_mode;
    // AC family only.
    std::optional<message_20::datatypes::AcConnector> ac_connector{std::nullopt};
};

} // namespace iso15118::ev::d20
