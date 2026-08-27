// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <everest/opcp/environment.hpp>
#include <everest/opcp/http_client.hpp>
#include <everest/opcp/types.hpp>

/// \file rcp_client.hpp
/// Root Certificate Pool: `GET /v1/root/rootCerts[?rootType=v2g|mo|oem]`
namespace opcp {

struct RootCertificate {
    std::string pem;
    RootType root_type{RootType::V2G};
    std::string root_certificate_id;
    std::string common_name;
    std::string organization_name;
    std::string valid_from;
    std::string valid_to;
    std::string xsd_namespace;       ///< "urn:iso:15118:2:2013:MsgDef" / "urn:iso:15118:20:2022:MsgDef" (optional)
    std::string signature_algorithm; ///< "secp256r1" / "secp521r1" / "ed448" (optional)
};

struct RcpResult {
    bool ok{false};
    long http_status{0};
    bool auth_rejected{false};
    std::string error;
    std::string url;
    std::vector<RootCertificate> roots;
};

class RcpClient {
public:
    RcpClient(HttpClientInterface& http, Environment env);

    /// All roots of the pool, optionally filtered server-side by type
    RcpResult get_root_certificates(std::optional<RootType> filter, const Auth& auth);

    /// Parses a `GetAllRootCertsResponseV1` body; exposed for tests
    static bool parse_response(const std::string& body, std::vector<RootCertificate>& out, std::string& error);

private:
    HttpClientInterface& http;
    Environment env;
};

} // namespace opcp
