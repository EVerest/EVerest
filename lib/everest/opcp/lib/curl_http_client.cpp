// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/http_client.hpp>

#include <cstring>
#include <memory>
#include <mutex>

#include <curl/curl.h>

namespace opcp {

namespace {

std::mutex global_init_mutex;
int global_init_count = 0;

std::size_t write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct SlistDeleter {
    void operator()(curl_slist* list) const {
        curl_slist_free_all(list);
    }
};
struct EasyDeleter {
    void operator()(CURL* handle) const {
        curl_easy_cleanup(handle);
    }
};

void set_or_throw(CURL* handle, CURLoption option, const char* value, const char* what) {
    if (curl_easy_setopt(handle, option, value) != CURLE_OK) {
        throw HttpError(std::string("libcurl refused ") + what);
    }
}

} // namespace

CurlHttpClient::CurlHttpClient(HttpClientOptions options_) : options(std::move(options_)) {
    const std::lock_guard<std::mutex> lock(global_init_mutex);
    if (global_init_count++ == 0) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
}

CurlHttpClient::~CurlHttpClient() {
    const std::lock_guard<std::mutex> lock(global_init_mutex);
    if (--global_init_count == 0) {
        curl_global_cleanup();
    }
}

HttpResponse CurlHttpClient::perform(const HttpRequest& request) {
    const std::unique_ptr<CURL, EasyDeleter> handle(curl_easy_init());
    if (!handle) {
        throw HttpError("curl_easy_init failed");
    }
    CURL* h = handle.get();

    char error_buffer[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, error_buffer);
    set_or_throw(h, CURLOPT_URL, request.url.c_str(), "CURLOPT_URL");
    set_or_throw(h, CURLOPT_PROTOCOLS_STR, "https,http", "CURLOPT_PROTOCOLS_STR");
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(options.timeout.count()));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_VERBOSE, options.verbose ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");

    // TLS: at least 1.2, always verify the server
    if (curl_easy_setopt(h, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        throw HttpError("libcurl without TLS support");
    }
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
    if (options.server_ca_bundle.has_value() && !options.server_ca_bundle->empty()) {
        set_or_throw(h, CURLOPT_CAINFO, options.server_ca_bundle->c_str(), "CURLOPT_CAINFO");
    }

    std::unique_ptr<curl_slist, SlistDeleter> headers;
    auto add_header = [&headers](const std::string& line) {
        curl_slist* next = curl_slist_append(headers.get(), line.c_str());
        if (next == nullptr) {
            throw HttpError("curl_slist_append failed");
        }
        headers.release();
        headers.reset(next);
    };
    for (const auto& [name, value] : request.headers) {
        add_header(name + ": " + value);
    }

    // Authentication
    if (const auto* bearer = std::get_if<BearerToken>(&request.auth)) {
        add_header("Authorization: Bearer " + bearer->token);
    } else if (const auto* client_cert = std::get_if<ClientCertificate>(&request.auth)) {
        set_or_throw(h, CURLOPT_SSLCERT, client_cert->certificate_path.c_str(), "CURLOPT_SSLCERT");
        set_or_throw(h, CURLOPT_SSLCERTTYPE, "PEM", "CURLOPT_SSLCERTTYPE");
        set_or_throw(h, CURLOPT_SSLKEY, client_cert->key_path.c_str(), "CURLOPT_SSLKEY");
        set_or_throw(h, CURLOPT_SSLKEYTYPE, "PEM", "CURLOPT_SSLKEYTYPE");
        if (client_cert->key_password.has_value() && !client_cert->key_password->empty()) {
            set_or_throw(h, CURLOPT_KEYPASSWD, client_cert->key_password->c_str(), "CURLOPT_KEYPASSWD");
        }
    }
    if (headers) {
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers.get());
    }

    // Method and body
    if (request.method == "GET") {
        curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
    } else {
        set_or_throw(h, CURLOPT_CUSTOMREQUEST, request.method.c_str(), "CURLOPT_CUSTOMREQUEST");
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    }

    HttpResponse response;
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &response.body);

    const CURLcode code = curl_easy_perform(h);
    if (code != CURLE_OK) {
        const std::string detail = std::strlen(error_buffer) > 0 ? error_buffer : curl_easy_strerror(code);
        throw HttpError(request.method + " " + request.url + ": " + detail);
    }

    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &response.status);
    char* content_type = nullptr;
    if (curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK && content_type != nullptr) {
        response.content_type = content_type;
    }
    return response;
}

} // namespace opcp
