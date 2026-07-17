// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022-2023 chargebyte GmbH
// Copyright (C) 2022-2023 Contributors to EVerest
#include "EvseV2G.hpp"
#include "connection/connection.hpp"
#include "connection/tls_connection.hpp"
#include "log.hpp"
#include "sdp.hpp"
#include <everest/logging.hpp>

#include <csignal>
#include <cstdlib>
#include <everest/tls/openssl_util.hpp>
#include <stdexcept>
#include <unistd.h>
namespace {
void log_handler(openssl::log_level_t level, const std::string& str) {
    switch (level) {
    case openssl::log_level_t::debug:
        // ignore debug logs
        break;
    case openssl::log_level_t::info:
        EVLOG_info << str;
        break;
    case openssl::log_level_t::warning:
        EVLOG_warning << str;
        break;
    case openssl::log_level_t::error:
    default:
        EVLOG_error << str;
        break;
    }
}
} // namespace

struct v2g_context* v2g_ctx = nullptr;

namespace module {

void EvseV2G::init() {

    std::vector<ISO15118_vasIntf*> r_vas;
    r_vas.reserve(r_iso15118_vas.size());
    for (const auto& vas : r_iso15118_vas) {
        r_vas.emplace_back(vas.get());
    }
    /* create v2g context */
    v2g_ctx = v2g_ctx_create(p_charger.get(), p_extensions.get(), r_security.get(), r_vas);

    if (v2g_ctx == nullptr) {
        throw std::runtime_error("Failed to create v2g context");
    }

    (void)openssl::set_log_handler(log_handler);
    tls::Server::configure_signal_handler(SIGUSR1);
    v2g_ctx->tls_server = &tls_server;

    this->r_security->subscribe_certificate_store_update(
        [this](const types::evse_security::CertificateStoreUpdate& update) {
            if (!update.leaf_certificate_type.has_value()) {
                return;
            }

            if (update.leaf_certificate_type.value() != types::evse_security::LeafCertificateType::V2G) {
                return;
            }

            dlog(DLOG_LEVEL_INFO, "Certificate store update received, reconfiguring TLS server");
            auto config = std::make_unique<tls::Server::config_t>();
            if (build_config(*config, v2g_ctx)) {
                dlog(DLOG_LEVEL_INFO, "Configuration of TLS server successful, updating it");
                v2g_ctx->tls_server->update(*config);
            } else {
                dlog(DLOG_LEVEL_INFO, "Configuration of TLS server failed, suspending it");
                v2g_ctx->tls_server->suspend();
            }
        });

    invoke_init(*p_charger);
    invoke_init(*p_extensions);
}

void EvseV2G::ready() {
    run_network_lifecycle();
}

void EvseV2G::run_network_lifecycle() {
    dlog(DLOG_LEVEL_DEBUG, "Starting V2G network lifecycle");

    while (!v2g_ctx->shutdown) {
        v2g_ctx->if_name = config.device.c_str();

        if (try_start_network()) {
            invoke_ready(*p_charger);
            invoke_ready(*p_extensions);

            if (config.enable_sdp_server) {
                if (sdp_listen(v2g_ctx) == -1) {
                    dlog(DLOG_LEVEL_ERROR, "sdp_listen() failed");
                }
            } else {
                wait_for_shutdown();
            }
            return;
        }

        if (!v2g_ctx->shutdown) {
            sleep(1);
        }
    }
}

bool EvseV2G::try_start_network() {
    dlog(DLOG_LEVEL_DEBUG, "Starting SDP responder");

    std::string failure_detail;
    if (connection_init(v2g_ctx, &failure_detail) == -1) {
        report_network_startup_failure("Failed to initialize connection: " + failure_detail);
        return false;
    }

    if (config.enable_sdp_server && sdp_init(v2g_ctx, &failure_detail) == -1) {
        report_network_startup_failure("Failed to start SDP responder: " + failure_detail);
        cleanup_failed_network_start();
        return false;
    }

    dlog(DLOG_LEVEL_DEBUG, "starting socket server(s)");
    if (connection_start_servers(v2g_ctx)) {
        report_network_startup_failure("start_connection_servers() failed");
        cleanup_failed_network_start();
        return false;
    }

    clear_network_startup_failure();
    return true;
}

void EvseV2G::cleanup_failed_network_start() {
    connection_cleanup(v2g_ctx);

    if (v2g_ctx->sdp_socket != -1) {
        close(v2g_ctx->sdp_socket);
        v2g_ctx->sdp_socket = -1;
    }
}

void EvseV2G::wait_for_shutdown() {
    while (!v2g_ctx->shutdown) {
        sleep(1);
    }
}

std::string EvseV2G::network_device_label() const {
    // When "device: auto" is configured, and the interface name is resolved, this returns
    // the resolved interface name. Fall back to config.device if still unresolved.
    if (v2g_ctx != nullptr && v2g_ctx->if_name != nullptr) {
        const std::string resolved = v2g_ctx->if_name;
        if (!resolved.empty() && resolved != "auto") {
            return resolved;
        }
    }
    return config.device;
}

void EvseV2G::report_network_startup_failure(const std::string& reason) {
    const auto message = "Device " + network_device_label() + ": " + reason;

    if (startup_fault_message == message) {
        if (!startup_fault_raised && p_charger->error_factory && p_charger->error_manager) {
            p_charger->raise_error(p_charger->error_factory->create_error("generic/CommunicationFault", "", message));
            startup_fault_raised = true;
        }
        return;
    }

    if (startup_fault_raised && p_charger->error_manager) {
        p_charger->clear_error("generic/CommunicationFault");
        startup_fault_raised = false;
    }

    startup_fault_message = message;
    dlog(DLOG_LEVEL_WARNING, "V2G network startup failed, retrying: %s", message.c_str());

    if (p_charger->error_factory && p_charger->error_manager) {
        p_charger->raise_error(p_charger->error_factory->create_error("generic/CommunicationFault", "", message));
        startup_fault_raised = true;
    }
}

void EvseV2G::clear_network_startup_failure() {
    if (startup_fault_message.empty()) {
        return;
    }

    dlog(DLOG_LEVEL_INFO, "V2G network startup recovered: Device %s is available", network_device_label().c_str());
    startup_fault_message.clear();

    if (startup_fault_raised && p_charger->error_manager) {
        p_charger->clear_error("generic/CommunicationFault");
        startup_fault_raised = false;
    }
}

EvseV2G::~EvseV2G() {
    v2g_ctx_free(v2g_ctx);
}

} // namespace module
