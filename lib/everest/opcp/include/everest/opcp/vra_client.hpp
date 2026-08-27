// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <vector>

#include <everest/opcp/environment.hpp>
#include <everest/opcp/http_client.hpp>
#include <everest/opcp/types.hpp>

/// \file vra_client.hpp
/// Hubject end entity registration (`PUT /v1/vra/cpo/endEntities`). Not part of the published OPCP
/// OpenAPI files, but required before a SECC leaf can be enrolled for a common name.
namespace opcp {

struct EndEntity {
    std::string common_name;
    std::string manufacturer;
    std::string device_name;
    std::string device_sw_version;
    std::string evse_serial_number;
    std::vector<std::string> evse_ids; ///< defaults to {common_name} when empty
    IsoVersion iso_version{IsoVersion::ISO15118_2};
    std::string ocpp_version;
    std::string charge_box_serial_number;
};

struct VraResult {
    bool ok{false};
    long http_status{0};
    bool auth_rejected{false};
    std::string error;
    std::string url;
    std::string response_body;
};

class VraClient {
public:
    VraClient(HttpClientInterface& http, Environment env);

    VraResult register_end_entity(const EndEntity& entity, const Auth& auth);

    /// JSON body as sent to the server; exposed for tests
    static std::string to_json(const EndEntity& entity);

private:
    HttpClientInterface& http;
    Environment env;
};

} // namespace opcp
