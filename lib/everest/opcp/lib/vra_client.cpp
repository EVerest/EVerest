// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/vra_client.hpp>

#include <nlohmann/json.hpp>

namespace opcp {

VraClient::VraClient(HttpClientInterface& http_, Environment env_) : http(http_), env(std::move(env_)) {
}

std::string VraClient::to_json(const EndEntity& entity) {
    nlohmann::json evse_ids = nlohmann::json::array();
    if (entity.evse_ids.empty()) {
        evse_ids.push_back(entity.common_name);
    } else {
        for (const auto& id : entity.evse_ids) {
            evse_ids.push_back(id);
        }
    }
    // Field names as expected by the Hubject VRA (note the spelling "manufacture")
    const nlohmann::json body = {
        {"commonName", entity.common_name},
        {"manufacture", entity.manufacturer},
        {"deviceName", entity.device_name},
        {"deviceSWVersion", entity.device_sw_version},
        {"evseSerialNumber", entity.evse_serial_number},
        {"evseID", evse_ids},
        {"evseISOversion", info(entity.iso_version).vra_iso_version},
        {"ocppVersion", entity.ocpp_version},
        {"chargeBoxSerialNumber", entity.charge_box_serial_number},
    };
    return body.dump();
}

VraResult VraClient::register_end_entity(const EndEntity& entity, const Auth& auth) {
    VraResult result;
    result.url = env.end_entities_url();

    HttpRequest request;
    request.method = "PUT";
    request.url = result.url;
    request.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
    request.body = to_json(entity);
    request.auth = auth;

    HttpResponse response;
    try {
        response = http.perform(request);
    } catch (const HttpError& e) {
        result.error = e.what();
        return result;
    }
    result.http_status = response.status;
    result.response_body = response.body;
    if (response.auth_rejected()) {
        result.auth_rejected = true;
        result.error = "HTTP " + std::to_string(response.status) + " (credentials rejected): " + response.body;
        return result;
    }
    if (!response.ok()) {
        result.error = "HTTP " + std::to_string(response.status) + ": " + response.body;
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace opcp
