// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

// The ISO 15118-20 namespace string literals live in session/protocol.hpp
// (ISO20_DC_PROTOCOL_NAMESPACE / ISO20_AC_PROTOCOL_NAMESPACE), which is included above.

// Per-state nested namespace to avoid ambiguity of the (identically named) create_request overloads.
namespace supported_app_protocol {

// Builds one AppProtocol entry per supported protocol generation, in priority order (the first entry
// in supported_protocols gets priority 1, and so on). The -20 branch derives its AC/DC namespace from
// the requested energy services; DIN SPEC 70121 is DC-only and is skipped for an AC service.
message_20::SupportedAppProtocolRequest
create_request(const std::vector<ProtocolId>& supported_protocols,
               const std::vector<dt::ServiceCategory>& supported_energy_services,
               const std::optional<std::string>& custom_protocol);

struct Result {
    bool negotiated{false};
    std::optional<uint8_t> schema_id{std::nullopt};
};

Result handle_response(const message_20::SupportedAppProtocolResponse& res);

} // namespace supported_app_protocol

} // namespace iso15118::d20::ev::state
