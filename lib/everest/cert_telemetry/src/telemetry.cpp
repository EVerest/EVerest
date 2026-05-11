// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/cert/telemetry.hpp>
#include <nlohmann/json.hpp>

namespace everest::lib::cert {

using json = nlohmann::json;

namespace {

template <typename T>
void load(json const& j, char const* key, T& dst) {
    if (j.contains(key)) {
        dst = j.at(key).get<T>();
    }
}

} // namespace

void to_json(json& j, CertChainState const& k) noexcept {
    j["configured"] = k.configured;
    j["synced"] = k.synced;
    j["num_files"] = k.num_files;
    j["num_useful_files"] = k.num_useful_files;
}

void from_json(json const& j, CertChainState& k) {
    load(j, "configured", k.configured);
    load(j, "synced", k.synced);
    load(j, "num_files", k.num_files);
    load(j, "num_useful_files", k.num_useful_files);
}

void to_json(json& j, CertTelemetry const& k) noexcept {
    j["config_complete"] = k.config_complete;
    j["sync_complete"] = k.sync_complete;
    j["secc_chain"] = k.secc_chain;
    j["mo_root"] = k.mo_root;
}

void from_json(json const& j, CertTelemetry& k) {
    load(j, "config_complete", k.config_complete);
    load(j, "sync_complete", k.sync_complete);
    if (j.contains("secc_chain")) {
        k.secc_chain = j.at("secc_chain").get<CertChainState>();
    }
    if (j.contains("mo_root")) {
        k.mo_root = j.at("mo_root").get<CertChainState>();
    }
}

std::string serialize(CertTelemetry const& v) {
    return json(v).dump(0);
}

CertTelemetry deserialize(std::string const& s) {
    return json::parse(s).get<CertTelemetry>();
}

} // namespace everest::lib::cert
