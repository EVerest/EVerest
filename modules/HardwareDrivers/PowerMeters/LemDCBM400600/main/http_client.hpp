// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef EVEREST_CORE_MODULE_HTTPCLIENT_H
#define EVEREST_CORE_MODULE_HTTPCLIENT_H

#include "fmt/format.h"
#include "http_client_interface.hpp"
#include <curl/curl.h>
#include <everest/logging.hpp>
#include <everest/util/async/monitor.hpp>
#include <regex>
#include <stdexcept>
#include <string>

namespace module::main {

// The DCBM does not print its certificate correctly in its /certificate API.
// In particular, the newlines after -----BEGIN CERTIFICATE----- and before -----END CERTIFICATE----- are missing there.
// This function will add these newlines if they are missing.
static void fixup_tls_certificate(std::string& tls_certificate) {
    tls_certificate = std::regex_replace(tls_certificate, std::regex("-----BEGIN CERTIFICATE-----\\s*([^\n])"),
                                         "-----BEGIN CERTIFICATE-----\n$1");
    tls_certificate = std::regex_replace(tls_certificate, std::regex("([^\n])\\s*-----END CERTIFICATE-----"),
                                         "$1\n-----END CERTIFICATE-----");
}

class HttpClient : public HttpClientInterface {

public:
    HttpClient() = delete;

    HttpClient(const std::string& host_arg, int port_arg, const std::string& tls_certificate,
               const std::string& network_interface = "", bool connection_reuse = true) {
        // initialize libcurl - this is safe to do multiple times, if there are multiple HttpClients
        // Note: This is only thread-safe after libcurl 7.84.0, but we use 8.4.0, so it should be fine
        curl_global_init(CURL_GLOBAL_DEFAULT);
        // These are saved in the client to avoid making the controller pass them at every call
        host = host_arg;
        port = port_arg;
        dcbm_tls_certificate = tls_certificate;
        tls_enabled = !dcbm_tls_certificate.empty();
        fixup_tls_certificate(dcbm_tls_certificate);
        this->network_interface = network_interface;
        if (connection_reuse) {
            setup_share();
        }
    }

    ~HttpClient() override {
        if (share != nullptr) {
            curl_share_cleanup(share);
        }
        // release the libcurl resources - this must be done once for every call to curl_global_init().
        // Note: This is only thread-safe after libcurl 7.84.0, but we use 8.4.0, so it should be fine
        curl_global_cleanup();
    }

    void set_command_timeout(const int command_timeout_ms) override {
        this->command_timeout_ms = command_timeout_ms;
    }
    [[nodiscard]] HttpResponse get(const std::string& path) const override;
    [[nodiscard]] HttpResponse put(const std::string& path, const std::string& body) const override;
    [[nodiscard]] HttpResponse post(const std::string& path, const std::string& body) const override;

private:
    struct RequestExecutionGuard {};

    std::string host;
    int port;
    bool tls_enabled;
    std::string dcbm_tls_certificate;
    int command_timeout_ms = 5000; // default timeout in milliseconds
    std::string network_interface; // network interface

    // Shared connection/SSL-session/DNS caches, so consecutive requests reuse the TCP (and TLS)
    // connection instead of reconnecting per request. nullptr if connection reuse is disabled.
    // Requests are serialized because libcurl does not support sharing the connection cache between
    // concurrent transfers.
    CURLSH* share = nullptr;
    // Held for the duration of a request. Note that this is not recursive: a request must never be
    // issued from within another request on the same client.
    mutable everest::lib::util::monitor<RequestExecutionGuard> request_execution_guard;

    void setup_share() {
        share = curl_share_init();
        if (share == nullptr) {
            EVLOG_warning << "curl_share_init() failed - falling back to one connection per request";
            return;
        }
        // No CURLSHOPT_LOCKFUNC/UNLOCKFUNC is registered: all transfers using this share are serialized
        // by request_execution_guard, so the shared caches are never accessed concurrently. Locking
        // callbacks would not be sufficient on their own anyway, since libcurl does not support using a
        // shared connection cache from concurrent transfers at all.
        for (const auto data : {CURL_LOCK_DATA_CONNECT, CURL_LOCK_DATA_SSL_SESSION, CURL_LOCK_DATA_DNS}) {
            if (curl_share_setopt(share, CURLSHOPT_SHARE, data) != CURLSHE_OK) {
                // Without the shared caches there is nothing to reuse, and the connection of a handle
                // would just be discarded when the handle is cleaned up after the request. Drop the share
                // so that CURLOPT_FORBID_REUSE is set again and the previous behavior is restored.
                EVLOG_warning << "curl_share_setopt() failed - falling back to one connection per request";
                curl_share_cleanup(share);
                share = nullptr;
                return;
            }
        }
    }

    [[nodiscard]] CURL* create_curl_handle_and_setup_url(const std::string& path) const;
    HttpResponse perform_request(CURL* connection, const std::string& request_body, const char* method_name,
                                 const std::string& path) const;
};

} // namespace module::main

#endif // EVEREST_CORE_MODULE_HTTPCLIENT_H
