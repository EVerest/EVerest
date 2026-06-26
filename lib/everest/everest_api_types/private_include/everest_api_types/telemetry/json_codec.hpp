// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/telemetry/API.hpp>

namespace everest::lib::API::V1_0::types::telemetry {

using json = nlohmann::json;

void to_json(json& j, ChargeProgress const& k) noexcept;
void from_json(json const& j, ChargeProgress& k);

void to_json(json& j, V2gDin70121CommunicationState const& k) noexcept;
void from_json(json const& j, V2gDin70121CommunicationState& k);

void to_json(json& j, V2gIso15118AcCommunicationState const& k) noexcept;
void from_json(json const& j, V2gIso15118AcCommunicationState& k);

void to_json(json& j, V2gIso15118DcCommunicationState const& k) noexcept;
void from_json(json const& j, V2gIso15118DcCommunicationState& k);

void to_json(json& j, V2gCommunicationState const& k) noexcept;
void from_json(json const& j, V2gCommunicationState& k);

void to_json(json& j, V2gMessageState const& k) noexcept;
void from_json(json const& j, V2gMessageState& k);

void to_json(json& j, V2gServerStatus const& k) noexcept;
void from_json(json const& j, V2gServerStatus& k);

void to_json(json& j, V2gEvErrorCode const& k) noexcept;
void from_json(json const& j, V2gEvErrorCode& k);

void to_json(json& j, CertChainState const& k) noexcept;
void from_json(json const& j, CertChainState& k);

void to_json(json& j, CertTelemetry const& k) noexcept;
void from_json(json const& j, CertTelemetry& k);

void to_json(json& j, EvseControlStatus const& k) noexcept;
void from_json(json const& j, EvseControlStatus& k);

void to_json(json& j, V2gTransport const& k) noexcept;
void from_json(json const& j, V2gTransport& k);

void to_json(json& j, V2gEvElectrical const& k) noexcept;
void from_json(json const& j, V2gEvElectrical& k);

void to_json(json& j, V2gPaymentService const& k) noexcept;
void from_json(json const& j, V2gPaymentService& k);

void to_json(json& j, V2gChargerStatus const& k) noexcept;
void from_json(json const& j, V2gChargerStatus& k);

void to_json(json& j, V2gEvseElectrical const& k) noexcept;
void from_json(json const& j, V2gEvseElectrical& k);

} // namespace everest::lib::API::V1_0::types::telemetry
