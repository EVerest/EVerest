// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "OtlpHttpServer.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <libwebsockets.h>

#include <everest/logging.hpp>

namespace module {

struct OtlpHttpServer::Impl {
    MetricsHandler on_metrics;
    std::array<lws_protocols, 2> protocols{};
    lws_context* context{nullptr};
    std::atomic<bool> running{true};
    std::thread thread;
};

namespace {

constexpr auto METRICS_PATH = "/v1/metrics";
constexpr auto PROTOBUF_CONTENT_TYPE = "application/x-protobuf";

// Per-connection state. libwebsockets allocates it as zeroed raw memory, so it
// has to stay a POD; the body buffer is owned through a plain pointer.
struct Session {
    std::string* body;
    unsigned int status; // 0 = still undecided, otherwise the HTTP status to answer with
};

// Sends the response for the current transaction. A successful export is
// answered with an empty ExportMetricsServiceResponse, which encodes to zero
// bytes.
int finish(lws* wsi, unsigned int status) {
    if (status == HTTP_STATUS_OK) {
        std::array<unsigned char, LWS_PRE + 256> buffer{};
        unsigned char* start = buffer.data() + LWS_PRE;
        unsigned char* p = start;
        unsigned char* end = buffer.data() + buffer.size();
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK, PROTOBUF_CONTENT_TYPE, 0, &p, end) != 0 ||
            lws_finalize_write_http_header(wsi, start, &p, end) != 0) {
            return -1;
        }
    } else if (lws_return_http_status(wsi, status, nullptr) != 0) {
        return -1;
    }
    return lws_http_transaction_completed(wsi) != 0 ? -1 : 0;
}

int http_callback(lws* wsi, lws_callback_reasons reason, void* user, void* in, std::size_t len) {
    auto* session = static_cast<Session*>(user);

    switch (reason) {
    case LWS_CALLBACK_HTTP: {
        // `in` holds the request path; a POST is recognized by its POST_URI header
        delete session->body;
        session->body = nullptr;
        session->status = 0;

        const bool is_post = lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0;
        const std::string path(static_cast<const char*>(in), len);
        if (not is_post or path != METRICS_PATH) {
            return finish(wsi, HTTP_STATUS_NOT_FOUND);
        }

        std::array<char, 64> content_type{};
        lws_hdr_copy(wsi, content_type.data(), content_type.size(), WSI_TOKEN_HTTP_CONTENT_TYPE);
        if (std::strncmp(content_type.data(), PROTOBUF_CONTENT_TYPE, std::strlen(PROTOBUF_CONTENT_TYPE)) != 0) {
            // the body still has to be consumed before the error can be sent
            session->status = HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
        }
        session->body = new std::string;
        return 0; // libwebsockets now delivers the body
    }

    case LWS_CALLBACK_HTTP_BODY:
        if (session->body != nullptr and session->status == 0) {
            session->body->append(static_cast<const char*>(in), len);
        }
        return 0;

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
        auto* self = static_cast<OtlpHttpServer::Impl*>(lws_get_protocol(wsi)->user);
        unsigned int status = session->status;
        if (status == 0 and session->body != nullptr) {
            status = self->on_metrics(*session->body) ? HTTP_STATUS_OK : HTTP_STATUS_BAD_REQUEST;
        }
        delete session->body;
        session->body = nullptr;
        return finish(wsi, status);
    }

    case LWS_CALLBACK_CLOSED_HTTP:
        if (session != nullptr) {
            delete session->body;
            session->body = nullptr;
        }
        return 0;

    default:
        return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
}

} // namespace

OtlpHttpServer::OtlpHttpServer(std::uint16_t port, MetricsHandler on_metrics) : impl(std::make_unique<Impl>()) {
    impl->on_metrics = std::move(on_metrics);
    impl->protocols = {{{"http", http_callback, sizeof(Session), 0, 0, impl.get(), 0}, LWS_PROTOCOL_LIST_TERM}};

    lws_set_log_level(LLL_ERR | LLL_WARN, [](int, const char* line) { EVLOG_warning << "libwebsockets: " << line; });

    lws_context_creation_info info{};
    info.port = port;
    info.protocols = impl->protocols.data();
    info.gid = -1;
    info.uid = -1;

    impl->context = lws_create_context(&info);
    if (impl->context == nullptr) {
        throw std::runtime_error("OTLP receiver could not listen on port " + std::to_string(port));
    }

    impl->thread = std::thread([this]() {
        while (impl->running) {
            lws_service(impl->context, 0);
        }
    });
}

OtlpHttpServer::~OtlpHttpServer() {
    impl->running = false;
    lws_cancel_service(impl->context);
    impl->thread.join();
    lws_context_destroy(impl->context);
}

} // namespace module
