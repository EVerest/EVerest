// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::iso15118_charger {

std::string serialize(CertificateActionEnum val) noexcept;
std::string serialize(EnergyTransferMode val) noexcept;
std::string serialize(Status val) noexcept;
std::string serialize(HashAlgorithm val) noexcept;
std::string serialize(RequestExiStreamSchema const& val) noexcept;
std::string serialize(ResponseExiStreamStatus const& val) noexcept;
std::string serialize(CertificateHashDataInfo const& val) noexcept;
std::string serialize(EnergyTransferModeList const& val) noexcept;

std::ostream& operator<<(std::ostream& os, CertificateActionEnum const& val);
std::ostream& operator<<(std::ostream& os, EnergyTransferMode const& val);
std::ostream& operator<<(std::ostream& os, Status const& val);
std::ostream& operator<<(std::ostream& os, HashAlgorithm const& val);
std::ostream& operator<<(std::ostream& os, RequestExiStreamSchema const& val);
std::ostream& operator<<(std::ostream& os, ResponseExiStreamStatus const& val);
std::ostream& operator<<(std::ostream& os, CertificateHashDataInfo const& val);
std::ostream& operator<<(std::ostream& os, EnergyTransferModeList const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::iso15118_charger
