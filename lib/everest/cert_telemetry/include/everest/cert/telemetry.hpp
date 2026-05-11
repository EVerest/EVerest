// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace everest::lib::cert {

struct CertChainState {
    bool configured{false};
    bool synced{false};
    int num_files{0};
    int num_useful_files{0};
};

struct CertTelemetry {
    bool config_complete{false};
    bool sync_complete{false};
    CertChainState secc_chain;
    CertChainState mo_root;
};

void to_json(nlohmann::json& j, CertChainState const&) noexcept;
void from_json(nlohmann::json const& j, CertChainState&);

void to_json(nlohmann::json& j, CertTelemetry const&) noexcept;
void from_json(nlohmann::json const& j, CertTelemetry&);

std::string serialize(CertTelemetry const&);
CertTelemetry deserialize(std::string const&);

} // namespace everest::lib::cert
