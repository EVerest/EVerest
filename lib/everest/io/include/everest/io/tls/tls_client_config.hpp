// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <string>

// Owning mirror of ::tls::Client::config_t. The generic client copies this Config at construction
// and replays setup() on the first poll and on every reconnect, so it must not carry raw pointers
// into caller memory. ::tls::ConfigItem keeps its own copy and preserves libtls's distinction
// between unset (nullptr) and empty (""). setup() materializes a ::tls::Client::config_t from
// these fields for the duration of ::tls::Client::init(), which copies what it needs.
struct everest::lib::io::tls::tls_client_socket::Config {
    struct tls_config {
        ::tls::ConfigItem cipher_list{nullptr};                  //!< unset means use default
        ::tls::ConfigItem ciphersuites{nullptr};                 //!< unset means use default, "" disables TLS 1.3
        ::tls::ConfigItem certificate_chain_file{nullptr};       //!< client certificate and intermediate CAs
        ::tls::ConfigItem private_key_file{nullptr};             //!< private key for client certificate
        ::tls::ConfigItem private_key_password{nullptr};         //!< optional password for private key
        ::tls::ConfigItem verify_locations_file{nullptr};        //!< PEM trust anchors for server certificate
        ::tls::ConfigItem verify_locations_path{nullptr};        //!< for server certificate
        ::tls::Client::trusted_ca_keys_t trusted_ca_keys_data{}; //!< trusted CA keys configuration data
        std::int32_t io_timeout_ms{-1};                          //!< socket timeout in milliseconds
        int min_proto_version{0};                                //!< 0 means use default
        bool verify_server{true};                                //!< verify the server certificate
        bool verify_subject_name{false};                         //!< pin the peer certificate to host_for_sni
        bool status_request{false};                              //!< status request extension in the client hello
        bool status_request_v2{false};                           //!< status request v2 extension in the client hello
        bool trusted_ca_keys{false};                             //!< trusted ca keys extension in the client hello
    };
    tls_config tls{};
    std::string host_for_sni;
};
