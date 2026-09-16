// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/transport/data_client.hpp>

#include <cerrno>
#include <utility>

#include <arpa/inet.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <everest/io/tls/tls_client_config.hpp>

#include <extensions/trusted_ca_keys.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::ev::transport {

namespace {
// libio tcp_socket::setup expects a connect timeout in milliseconds.
constexpr int CONNECT_TIMEOUT_MS = 5000;

// [V2G2-077]/[V2G2-124]: the EVCC picks its source port from the dynamic range.
constexpr everest::lib::io::tcp::source_port_range EVCC_SOURCE_PORTS{49152, 65535};

// The single cipher suite ISO 15118-2 allows.
constexpr auto TLS1_2_CIPHER_LIST = "ECDHE-ECDSA-AES128-SHA256";

// The cipher suites ISO 15118-20 allows.
constexpr auto TLS1_3_CIPHERSUITES = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";

// libio reports a plain-TCP EOF and a TLS close_notify as ECONNRESET, a poll hangup as ENOTCONN,
// and a write to a closed peer as EPIPE.
bool is_peer_close(int error) {
    return error == ECONNRESET or error == ENOTCONN or error == EPIPE;
}

// [V2G2-651]: advertise each configured V2G root by its SHA-1 hash so the SECC can select a chain
// the EV trusts. An absent or certificate-free file yields an empty hint, which emits no extension.
::tls::trusted_ca_keys::trusted_ca_keys_t build_trusted_ca_keys(const std::string& v2g_root_path) {
    ::tls::trusted_ca_keys::trusted_ca_keys_t tck{};
    if (v2g_root_path.empty()) {
        return tck;
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(v2g_root_path.c_str(), "r"), &BIO_free);
    if (not bio) {
        logf_warning("trusted_ca_keys: could not open the V2G root file %s", v2g_root_path.c_str());
        return tck;
    }
    while (true) {
        std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr),
                                                         &X509_free);
        if (not cert) {
            break;
        }
        ::tls::trusted_ca_keys::digest_t digest{};
        if (::tls::trusted_ca_keys::certificate_digest(digest, cert.get())) {
            tck.cert_sha1_hash.push_back(digest);
        }
    }
    // PEM_read_bio_X509 leaves a benign "no start line" error on the queue at EOF; clear it so it
    // does not surface in later handshake diagnostics.
    ERR_clear_error();
    return tck;
}

// [V2G2-875]: the SECC leaf identifies a charge point operator via a DomainComponent=="CPO" RDN.
bool has_cpo_domain_component(const X509* cert) {
    if (cert == nullptr) {
        return false;
    }
    const X509_NAME* subject = X509_get_subject_name(cert);
    if (subject == nullptr) {
        return false;
    }
    int idx = -1;
    while ((idx = X509_NAME_get_index_by_NID(subject, NID_domainComponent, idx)) >= 0) {
        const X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, idx);
        const ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        if (data == nullptr) {
            continue;
        }
        const std::string dc(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)),
                             static_cast<size_t>(ASN1_STRING_length(data)));
        if (dc == "CPO") {
            return true;
        }
    }
    return false;
}

// tls_1_3 selects between the two ISO 15118 profiles; everything else is shared.
everest::lib::io::tls::tls_client_socket::Config make_tls_config(const TlsParams& params, const std::string& remote) {
    everest::lib::io::tls::tls_client_socket::Config cfg;

    // The SECC certificate carries no hostname, so the numeric address goes into the SNI slot and
    // the subject-name check stays off; the chain is verified against the V2G root instead.
    cfg.host_for_sni = remote;
    cfg.tls.verify_subject_name = false;
    cfg.tls.verify_server = params.verify_server;
    if (not params.v2g_root_cert_path.empty()) {
        cfg.tls.verify_locations_file = params.v2g_root_cert_path;
    }
    cfg.tls.io_timeout_ms = CONNECT_TIMEOUT_MS;

    // [V2G2-070]: request an OCSP staple. libtls decodes it and fails the handshake on a malformed
    // response; revocation is not verified.
    cfg.tls.status_request = true;

    // [V2G2-651]: hint the SECC at the roots this EV trusts.
    cfg.tls.trusted_ca_keys = true;
    cfg.tls.trusted_ca_keys_data = build_trusted_ca_keys(params.v2g_root_cert_path);

    cfg.tls.tls_key_logging = params.key_logging;
    cfg.tls.tls_key_logging_path = params.key_logging_path;

    if (params.tls_1_3) {
        cfg.tls.min_proto_version = TLS1_3_VERSION;
        cfg.tls.ciphersuites = TLS1_3_CIPHERSUITES;
        // mTLS: the EV presents its vehicle chain.
        if (not params.client_cert_chain_path.empty()) {
            cfg.tls.certificate_chain_file = params.client_cert_chain_path;
        }
        if (not params.client_key_path.empty()) {
            cfg.tls.private_key_file = params.client_key_path;
        }
        if (not params.client_key_password.empty()) {
            cfg.tls.private_key_password = params.client_key_password;
        }
    } else {
        cfg.tls.min_proto_version = TLS1_2_VERSION;
        // An empty ciphersuites list disables TLS 1.3, capping the negotiation at 1.2.
        cfg.tls.ciphersuites = "";
        cfg.tls.cipher_list = TLS1_2_CIPHER_LIST;
    }

    return cfg;
}
} // namespace

DataClient::DataClient(everest::lib::io::event::fd_event_handler& handler_) : handler(handler_) {
}

DataClient::~DataClient() {
    teardown();
}

void DataClient::fire_failed() {
    if (failed_fired) {
        return;
    }
    failed_fired = true;
    if (on_failed) {
        on_failed();
    }
}

void DataClient::fire_closed() {
    if (closed_fired) {
        return;
    }
    closed_fired = true;
    if (m_on_closed) {
        m_on_closed();
    }
}

void DataClient::on_closed(std::function<void()> handler_) {
    m_on_closed = std::move(handler_);
}

everest::lib::io::event::fd_event_sync_interface* DataClient::active_client() {
    if (client) {
        return client.get();
    }
    if (tls_client) {
        return tls_client.get();
    }
    return nullptr;
}

void DataClient::teardown() {
    if (registered) {
        if (auto* obj = active_client()) {
            handler.unregister_event_handler(obj);
        }
    }
    registered = false;
    client.reset();
    tls_client.reset();
}

void DataClient::defer_teardown() {
    handler.add_action([this, token = std::weak_ptr<int>(life)]() {
        if (token.expired()) {
            return;
        }
        teardown();
    });
}

bool DataClient::peer_is_cpo() {
    if (not tls_client) {
        return false;
    }
    const auto& policy = tls_client->get_raw_handler();
    if (not policy) {
        return false;
    }
    const auto* connection = policy->connection();
    if (connection == nullptr) {
        return false;
    }
    return has_cpo_domain_component(connection->peer_certificate());
}

void DataClient::on_ready(bool check_cpo) {
    // set_on_ready_action is persistent and fires on every ready transition, including internal
    // resets; the one-shot guards keep the on_connected contract (fired exactly once per connect).
    if (connected_fired or failed_fired) {
        return;
    }
    if (check_cpo and not peer_is_cpo()) {
        logf_error("[V2G2-875] the SECC leaf certificate carries no DomainComponent=CPO RDN");
        fire_failed();
        defer_teardown();
        return;
    }
    connected_fired = true;
    if (on_connected) {
        on_connected();
    }
}

void DataClient::connect(const iso15118::io::Ipv6EndPoint& endpoint, const std::string& device,
                         const std::optional<TlsParams>& tls, std::function<void()> on_connected_,
                         std::function<void()> on_failed_) {
    on_connected = std::move(on_connected_);
    on_failed = std::move(on_failed_);
    connected_fired = false;
    failed_fired = false;
    closed_fired = false;

    // register_events short-circuits on the registered guard, so a reconnect must tear down first.
    teardown();

    char remote[INET6_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET6, endpoint.address, remote, sizeof(remote)) == nullptr) {
        logf_error("DataClient::connect failed to format the SECC address");
        fire_failed();
        return;
    }

    // Shared wiring for both client types; only the ready action differs.
    const auto wire = [this](auto& transport) {
        transport.set_rx_handler([this](std::vector<uint8_t> const& bytes, auto& /*device*/) {
            if (m_on_rx) {
                m_on_rx(bytes);
            }
        });

        // libio connects on a detached thread, so without this a failed connect is invisible.
        transport.set_error_handler([this](int error, std::string const& msg) {
            if (error == 0) {
                // error-cleared transition, not a failure
                return;
            }
            if (connected_fired and is_peer_close(error)) {
                // A close is the regular end of a session, not a transport failure.
                logf_info("DataClient: the SECC closed the connection");
                fire_closed();
                return;
            }
            logf_error("DataClient transport error %d: %s", error, msg.c_str());
            fire_failed();
        });
    };

    // SO_BINDTODEVICE supplies the scope for a link-local SECC address; without it connect() to an
    // fe80:: address fails with EINVAL. Empty leaves it unbound, for tests.
    try {
        // The client ctors register and make_shared internally and are not noexcept; a throw must
        // not escape with no client and on_failed un-fired, or a caller awaiting the outcome would
        // hang forever.
        if (tls.has_value()) {
            tls_client = std::make_unique<everest::lib::io::tls::tls_client>(
                make_tls_config(*tls, std::string(remote)), std::string(remote), endpoint.port, CONNECT_TIMEOUT_MS,
                device, EVCC_SOURCE_PORTS);
            wire(*tls_client);
            // [V2G2-875] applies to the ISO 15118-2 profile; ISO 15118-20 leaf naming differs.
            const auto check_cpo = not tls->tls_1_3;
            tls_client->set_on_ready_action([this, check_cpo]() { on_ready(check_cpo); });
        } else {
            client = std::make_unique<everest::lib::io::tcp::tcp_client>(std::string(remote), endpoint.port,
                                                                         CONNECT_TIMEOUT_MS, device, EVCC_SOURCE_PORTS);
            wire(*client);
            client->set_on_ready_action([this]() { on_ready(false); });
        }
    } catch (const std::exception& e) {
        logf_error("DataClient::connect failed to construct the data client: %s", e.what());
        client.reset();
        tls_client.reset();
        fire_failed();
        return;
    }

    if (not register_events(handler)) {
        logf_error("DataClient::connect failed to register the data client");
        fire_failed();
    }
}

bool DataClient::send(const std::vector<uint8_t>& frame) {
    if (client) {
        return client->tx(frame);
    }
    if (tls_client) {
        return tls_client->tx(frame);
    }
    logf_error("DataClient::send called before connect");
    return false;
}

void DataClient::on_rx(std::function<void(const std::vector<uint8_t>&)> handler_) {
    m_on_rx = std::move(handler_);
}

bool DataClient::register_events(everest::lib::io::event::fd_event_handler& handler_) {
    // Idempotent: a second call is a no-op.
    if (registered) {
        return true;
    }

    auto* obj = active_client();
    if (obj == nullptr) {
        logf_error("DataClient::register_events called before connect");
        return false;
    }

    const auto ok = handler_.register_event_handler(obj);
    if (not ok) {
        // Keep the invariant "client non-null <=> registered": a half-registered client would
        // accept sends the reactor never drives.
        logf_error("Failed to register the data client with the event handler");
        client.reset();
        tls_client.reset();
        return false;
    }
    registered = true;
    return true;
}

} // namespace iso15118::ev::transport
