// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <iso15118/config.hpp>
#include <iso15118/detail/io/ssl_config_builder.hpp>

TEST_CASE("build_chain_configs forwards trust_anchor_pem and chain fields") {
    iso15118::config::SSLConfig cfg{};

    iso15118::config::ChainConfig a{};
    a.path_certificate_chain = "/p/a/chain.pem";
    a.path_certificate_key = "/p/a/key.pem";
    a.trust_anchor_pem = "----A ROOT PEM----";

    iso15118::config::ChainConfig b{};
    b.path_certificate_chain = "/p/b/chain.pem";
    b.path_certificate_key = "/p/b/key.pem";
    b.trust_anchor_pem = "----B ROOT PEM----";

    cfg.chains = {a, b};

    const auto out = iso15118::io::build_chain_configs(cfg);
    REQUIRE(out.size() == 2);
    CHECK(std::string(static_cast<const char*>(out[0].certificate_chain_file)) == "/p/a/chain.pem");
    CHECK(std::string(static_cast<const char*>(out[0].private_key_file)) == "/p/a/key.pem");
    CHECK(std::string(static_cast<const char*>(out[0].trust_anchor_pem)) == "----A ROOT PEM----");
    CHECK(std::string(static_cast<const char*>(out[1].trust_anchor_pem)) == "----B ROOT PEM----");
}

TEST_CASE("build_chain_configs leaves trust_anchor unset when root absent") {
    iso15118::config::SSLConfig cfg{};
    iso15118::config::ChainConfig a{};
    a.path_certificate_chain = "/p/a/chain.pem";
    a.path_certificate_key = "/p/a/key.pem";
    // trust_anchor_pem left nullopt
    cfg.chains = {a};

    const auto out = iso15118::io::build_chain_configs(cfg);
    REQUIRE(out.size() == 1);
    CHECK(static_cast<const char*>(out[0].trust_anchor_pem) == nullptr);
}
