// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/rcp_client.hpp>

#include <nlohmann/json.hpp>

#include <everest/opcp/pkcs.hpp>

namespace opcp {

RcpClient::RcpClient(HttpClientInterface& http_, Environment env_) : http(http_), env(std::move(env_)) {
}

bool RcpClient::parse_response(const std::string& body, std::vector<RootCertificate>& out, std::string& error) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (const nlohmann::json::exception& e) {
        error = std::string("RCP response is not JSON: ") + e.what();
        return false;
    }

    const nlohmann::json* list = nullptr;
    if (json.contains("RootCertificateCollection") && json["RootCertificateCollection"].contains("rootCertificates")) {
        list = &json["RootCertificateCollection"]["rootCertificates"];
    } else if (json.contains("rootCertificates")) {
        list = &json["rootCertificates"];
    } else if (json.is_array()) {
        list = &json;
    }
    if (list == nullptr || !list->is_array()) {
        error = "RCP response has no RootCertificateCollection.rootCertificates array";
        return false;
    }

    for (const auto& item : *list) {
        if (!item.is_object() || !item.contains("caCertificate") || !item["caCertificate"].is_string()) {
            continue;
        }
        RootCertificate root;
        root.pem = der_base64_to_pem(item["caCertificate"].get<std::string>());
        if (root.pem.empty()) {
            error += "skipped a root certificate that is not base64 DER; ";
            continue;
        }
        const auto type = root_type_from_string(item.value("rootType", "V2G"));
        if (!type.has_value()) {
            error += "skipped a root certificate with unknown rootType '" + item.value("rootType", "") + "'; ";
            continue;
        }
        root.root_type = *type;
        root.root_certificate_id = item.value("rootCertificateId", "");
        root.common_name = item.value("commonName", "");
        root.organization_name = item.value("organizationName", "");
        root.valid_from = item.value("validFrom", "");
        root.valid_to = item.value("validTo", "");
        root.xsd_namespace = item.value("xsdMsgDefNamespace", "");
        root.signature_algorithm = item.value("signatureAlgorithm", "");
        if (root.common_name.empty()) {
            if (const auto summary = describe_certificate(root.pem)) {
                root.common_name = summary->common_name;
            }
        }
        out.push_back(std::move(root));
    }
    return true;
}

RcpResult RcpClient::get_root_certificates(std::optional<RootType> filter, const Auth& auth) {
    RcpResult result;
    result.url = env.root_certs_url();
    if (filter.has_value()) {
        result.url += (result.url.find('?') == std::string::npos ? "?" : "&");
        result.url += std::string("rootType=") + root_type_query_value(*filter);
    }

    HttpRequest request;
    request.method = "GET";
    request.url = result.url;
    request.headers = {{"Accept", "application/json"}};
    request.auth = auth;

    HttpResponse response;
    try {
        response = http.perform(request);
    } catch (const HttpError& e) {
        result.error = e.what();
        return result;
    }
    result.http_status = response.status;
    if (response.auth_rejected()) {
        result.auth_rejected = true;
        result.error = "HTTP " + std::to_string(response.status) + " (credentials rejected)";
        return result;
    }
    if (!response.ok()) {
        result.error = "HTTP " + std::to_string(response.status) + ": " + response.body;
        return result;
    }
    std::string parse_error;
    if (!parse_response(response.body, result.roots, parse_error)) {
        result.error = parse_error;
        return result;
    }
    result.error = parse_error; // non-fatal skips
    result.ok = true;
    return result;
}

} // namespace opcp
