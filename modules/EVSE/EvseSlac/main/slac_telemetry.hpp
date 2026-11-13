// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/telemetry.hpp>
#include <everest_api_types/telemetry/API.hpp>

namespace module::main {

// The library's telemetry as the EVerest API types, which the module publishes as JSON.
everest::lib::API::V1_0::types::telemetry::SlacStatus to_api(everest::lib::slac::SlacTelemetry const& status);
everest::lib::API::V1_0::types::telemetry::SlacFsmState to_api(everest::lib::slac::SlacFsmState const& fsm_state);

} // namespace module::main
