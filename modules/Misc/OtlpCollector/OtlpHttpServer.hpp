// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OTLP_HTTP_SERVER_HPP
#define OTLP_HTTP_SERVER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace module {

/// Minimal OTLP/HTTP receiver: accepts POST /v1/metrics with an
/// application/x-protobuf body and hands the raw payload to a callback. Any
/// other request is answered with the matching HTTP error. No TLS, no gRPC,
/// no compression, no JSON.
class OtlpHttpServer {
public:
    /// Called on the receiver's own thread. Return false if the payload could
    /// not be decoded, the client then gets a 400.
    using MetricsHandler = std::function<bool(const std::string& payload)>;

    /// Starts listening immediately; throws if the port cannot be bound.
    OtlpHttpServer(std::uint16_t port, MetricsHandler on_metrics);
    ~OtlpHttpServer();

    OtlpHttpServer(const OtlpHttpServer&) = delete;
    OtlpHttpServer& operator=(const OtlpHttpServer&) = delete;

    struct Impl; // defined in the .cpp, where the libwebsockets callback needs it

private:
    std::unique_ptr<Impl> impl;
};

} // namespace module

#endif // OTLP_HTTP_SERVER_HPP
