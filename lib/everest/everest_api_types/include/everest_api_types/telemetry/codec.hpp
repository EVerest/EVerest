// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::telemetry {

std::string serialize(ChargeProgress val) noexcept;
std::string serialize(V2gDin70121CommunicationState val) noexcept;
std::string serialize(V2gIso15118AcCommunicationState val) noexcept;
std::string serialize(V2gIso15118DcCommunicationState val) noexcept;
std::string serialize(V2gCommunicationState val) noexcept;
std::string serialize(V2gMessageState val) noexcept;
std::string serialize(V2gServerStatus val) noexcept;
std::string serialize(V2gEvErrorCode val) noexcept;
std::string serialize(SlacState val) noexcept;
std::string serialize(SlacD3State val) noexcept;
std::string serialize(CertChainState const& val) noexcept;
std::string serialize(CertTelemetry const& val) noexcept;
std::string serialize(EvseControlStatus const& val) noexcept;
std::string serialize(V2gTransport const& val) noexcept;
std::string serialize(V2gEvElectrical const& val) noexcept;
std::string serialize(V2gPaymentService const& val) noexcept;
std::string serialize(V2gChargerStatus const& val) noexcept;
std::string serialize(V2gEvseElectrical const& val) noexcept;
std::string serialize(SlacStatus const& val) noexcept;
std::string serialize(SlacFsmState const& val) noexcept;

std::ostream& operator<<(std::ostream& os, ChargeProgress const& val);
std::ostream& operator<<(std::ostream& os, V2gCommunicationState const& val);
std::ostream& operator<<(std::ostream& os, V2gDin70121CommunicationState const& val);
std::ostream& operator<<(std::ostream& os, V2gIso15118AcCommunicationState const& val);
std::ostream& operator<<(std::ostream& os, V2gIso15118DcCommunicationState const& val);
std::ostream& operator<<(std::ostream& os, V2gMessageState const& val);
std::ostream& operator<<(std::ostream& os, V2gServerStatus const& val);
std::ostream& operator<<(std::ostream& os, V2gEvErrorCode const& val);
std::ostream& operator<<(std::ostream& os, SlacState const& val);
std::ostream& operator<<(std::ostream& os, SlacD3State const& val);
std::ostream& operator<<(std::ostream& os, CertChainState const& val);
std::ostream& operator<<(std::ostream& os, CertTelemetry const& val);
std::ostream& operator<<(std::ostream& os, EvseControlStatus const& val);
std::ostream& operator<<(std::ostream& os, V2gTransport const& val);
std::ostream& operator<<(std::ostream& os, V2gEvElectrical const& val);
std::ostream& operator<<(std::ostream& os, V2gPaymentService const& val);
std::ostream& operator<<(std::ostream& os, V2gChargerStatus const& val);
std::ostream& operator<<(std::ostream& os, V2gEvseElectrical const& val);
std::ostream& operator<<(std::ostream& os, SlacStatus const& val);
std::ostream& operator<<(std::ostream& os, SlacFsmState const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::telemetry
