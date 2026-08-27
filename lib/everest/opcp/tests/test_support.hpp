// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <everest/opcp/http_client.hpp>
#include <everest/opcp/security_store.hpp>

namespace opcp::test {

inline std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        ADD_FAILURE() << "cannot read " << path;
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/// Generates the test PKI once per process into ./pki
inline void ensure_test_pki() {
    static bool generated = false;
    if (!generated) {
        const int rc = std::system("sh ./generate_test_pki.sh pki > pki.log 2>&1");
        ASSERT_EQ(rc, 0) << "generate_test_pki.sh failed, see pki.log";
        generated = true;
    }
}

inline std::string pki(const std::string& name) {
    return read_file("pki/" + name);
}

/// Scripted HTTP client: responses are matched by (method, url prefix); every request is recorded
class FakeHttpClient : public HttpClientInterface {
public:
    struct Scripted {
        std::string method;
        std::string url_prefix;
        HttpResponse response;
        bool throw_transport_error{false};
    };

    std::vector<Scripted> scripts;
    std::vector<HttpRequest> requests;

    void on(const std::string& method, const std::string& url_prefix, long status, std::string body,
            std::string content_type = "") {
        scripts.push_back({method, url_prefix, HttpResponse{status, std::move(body), std::move(content_type)}, false});
    }
    void fail(const std::string& method, const std::string& url_prefix) {
        scripts.push_back({method, url_prefix, HttpResponse{}, true});
    }

    HttpResponse perform(const HttpRequest& request) override {
        requests.push_back(request);
        for (const auto& script : scripts) {
            if (script.method == request.method && request.url.rfind(script.url_prefix, 0) == 0) {
                if (script.throw_transport_error) {
                    throw HttpError("simulated transport failure for " + request.url);
                }
                return script.response;
            }
        }
        return HttpResponse{404, "no scripted response for " + request.method + " " + request.url, "text/plain"};
    }

    const HttpRequest* last(const std::string& method, const std::string& url_part) const {
        for (auto it = requests.rbegin(); it != requests.rend(); ++it) {
            if (it->method == method && it->url.find(url_part) != std::string::npos) {
                return &*it;
            }
        }
        return nullptr;
    }
};

/// In-memory security store that hands out a prepared CSR instead of generating keys
class FakeSecurityStore : public SecurityStore {
public:
    std::string csr_to_return;
    evse_security::GetCertificateSignRequestStatus csr_status{evse_security::GetCertificateSignRequestStatus::Accepted};
    evse_security::InstallCertificateResult leaf_install_result{evse_security::InstallCertificateResult::Accepted};
    evse_security::InstallCertificateResult ca_install_result{evse_security::InstallCertificateResult::Accepted};
    bool v2g_root_installed{true};
    std::map<evse_security::LeafCertificateType, int> expiry_days;
    std::map<evse_security::LeafCertificateType, LeafInfo> leaf_infos;

    std::vector<std::string> installed_leaf_chains;
    std::vector<std::pair<evse_security::CaCertificateType, std::string>> installed_cas;
    std::vector<std::string> failed_csrs;
    std::vector<std::string> generated_common_names;

    evse_security::GetCertificateSignRequestResult
    generate_certificate_signing_request(evse_security::LeafCertificateType, const std::string&, const std::string&,
                                         const std::string& common_name, bool) override {
        generated_common_names.push_back(common_name);
        evse_security::GetCertificateSignRequestResult result;
        result.status = csr_status;
        if (csr_status == evse_security::GetCertificateSignRequestStatus::Accepted) {
            result.csr = csr_to_return;
        }
        return result;
    }
    void certificate_signing_request_failed(const std::string& csr, evse_security::LeafCertificateType) override {
        failed_csrs.push_back(csr);
    }
    evse_security::InstallCertificateResult update_leaf_certificate(const std::string& chain,
                                                                    evse_security::LeafCertificateType) override {
        if (leaf_install_result == evse_security::InstallCertificateResult::Accepted) {
            installed_leaf_chains.push_back(chain);
        }
        return leaf_install_result;
    }
    evse_security::InstallCertificateResult install_ca_certificate(const std::string& pem,
                                                                   evse_security::CaCertificateType type) override {
        if (ca_install_result == evse_security::InstallCertificateResult::Accepted) {
            installed_cas.emplace_back(type, pem);
        }
        return ca_install_result;
    }
    bool is_ca_certificate_installed(evse_security::CaCertificateType type) override {
        return type == evse_security::CaCertificateType::V2G ? v2g_root_installed : false;
    }
    std::optional<LeafInfo> get_leaf_certificate_info(evse_security::LeafCertificateType type) override {
        const auto it = leaf_infos.find(type);
        if (it == leaf_infos.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    int get_leaf_expiry_days_count(evse_security::LeafCertificateType type) override {
        const auto it = expiry_days.find(type);
        return it == expiry_days.end() ? 0 : it->second;
    }
    std::string get_ca_bundle_pem(evse_security::CaCertificateType) override {
        return {};
    }
};

} // namespace opcp::test
