// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/message/common_types.hpp>

// The universal SECC-side session configuration lives in the protocol-neutral iso15118::session
// namespace. The structs below stay in d20 because they are expressed with the -20 RationalNumber
// datatype and DER control types, and are referenced qualified from the session-side structs.
namespace iso15118::d20 {

struct ControlMobilityNeedsModes {
    message_20::datatypes::ControlMode control_mode;
    message_20::datatypes::MobilityNeedsMode mobility_mode;
};

struct AcSetupConfig {
    uint32_t voltage;
    std::vector<message_20::datatypes::AcConnector> connectors;
};

struct BptSetupConfig {
    message_20::datatypes::BptChannel bpt_channel;
    message_20::datatypes::GeneratorMode generator_mode;
    std::optional<message_20::datatypes::GridCodeIslandingDetectionMethod> grid_code_detection_method;
};

struct DerIecSetupConfig {
    std::map<iec::DERControlName, iec::DERControlFunction> supported_der_control_functions;
    iec::OperatingMode operating_mode;
    iec::GridConnectionMode grid_connection_mode;
};

} // namespace iso15118::d20
