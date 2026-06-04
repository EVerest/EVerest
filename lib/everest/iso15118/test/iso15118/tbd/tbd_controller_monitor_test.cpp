// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <thread>

#include <iso15118/config.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/tbd_controller.hpp>

using iso15118::TbdConfig;
using iso15118::TbdController;
using iso15118::config::CertificateBackend;
using iso15118::config::SSLConfig;

namespace {

SSLConfig make_cfg_a() {
    SSLConfig cfg{};
    cfg.backend = CertificateBackend::EVEREST_LAYOUT;
    cfg.chains.push_back(iso15118::config::ChainConfig{
        "/tmp/iso15118-test/a/chain.pem",
        "/tmp/iso15118-test/a/key.pem",
        std::nullopt,
        {},
        "/tmp/iso15118-test/a/v2g_root.pem", // trust_anchor_pem
    });
    cfg.path_certificate_v2g_root = "/tmp/iso15118-test/a/v2g_root.pem";
    cfg.path_certificate_mo_root = "/tmp/iso15118-test/a/mo_root.pem";
    cfg.enforce_tls_1_3 = false;
    return cfg;
}

SSLConfig make_cfg_b() {
    SSLConfig cfg{};
    cfg.backend = CertificateBackend::EVEREST_LAYOUT;
    cfg.chains.push_back(iso15118::config::ChainConfig{
        "/tmp/iso15118-test/b/chain.pem",
        "/tmp/iso15118-test/b/key.pem",
        std::nullopt,
        {},
        "/tmp/iso15118-test/b/v2g_root.pem", // trust_anchor_pem
    });
    cfg.path_certificate_v2g_root = "/tmp/iso15118-test/b/v2g_root.pem";
    cfg.path_certificate_mo_root = "/tmp/iso15118-test/b/mo_root.pem";
    cfg.enforce_tls_1_3 = true;
    return cfg;
}

bool chain_config_equal(const iso15118::config::ChainConfig& lhs, const iso15118::config::ChainConfig& rhs) {
    return lhs.path_certificate_chain == rhs.path_certificate_chain &&
           lhs.path_certificate_key == rhs.path_certificate_key &&
           lhs.private_key_password == rhs.private_key_password && lhs.ocsp_response_files == rhs.ocsp_response_files &&
           lhs.trust_anchor_pem == rhs.trust_anchor_pem;
}

bool ssl_config_equal(const SSLConfig& lhs, const SSLConfig& rhs) {
    if (lhs.chains.size() != rhs.chains.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.chains.size(); ++i) {
        if (!chain_config_equal(lhs.chains[i], rhs.chains[i])) {
            return false;
        }
    }
    return lhs.backend == rhs.backend && lhs.config_string == rhs.config_string &&
           lhs.path_certificate_v2g_root == rhs.path_certificate_v2g_root &&
           lhs.path_certificate_mo_root == rhs.path_certificate_mo_root &&
           lhs.enable_ssl_logging == rhs.enable_ssl_logging &&
           lhs.enable_tls_key_logging == rhs.enable_tls_key_logging && lhs.enforce_tls_1_3 == rhs.enforce_tls_1_3 &&
           lhs.tls_key_logging_path == rhs.tls_key_logging_path;
}

TbdController make_controller(SSLConfig initial) {
    TbdConfig tbd_cfg{};
    tbd_cfg.ssl = std::move(initial);
    tbd_cfg.interface_name = "lo";
    tbd_cfg.enable_sdp_server = false;
    return TbdController{std::move(tbd_cfg), iso15118::session::feedback::Callbacks{},
                         iso15118::d20::EvseSetupConfig{}};
}

} // namespace

SCENARIO("TbdController SSL config monitor: set then snapshot returns the new value") {
    GIVEN("a controller initialized with a baseline SSL config") {
        auto controller = make_controller(SSLConfig{});

        WHEN("set_ssl_config(cfg_a) is called and a snapshot is taken") {
            const auto cfg_a = make_cfg_a();
            controller.set_ssl_config(cfg_a);

            const SSLConfig snap = controller.ssl_config_snapshot();

            THEN("the snapshot exposes cfg_a") {
                REQUIRE(ssl_config_equal(snap, cfg_a));
            }
        }
    }
}

SCENARIO("TbdController SSL config monitor: second write does not mutate earlier snapshot") {
    GIVEN("a controller and two distinct SSL configs") {
        auto controller = make_controller(SSLConfig{});
        const auto cfg_a = make_cfg_a();
        const auto cfg_b = make_cfg_b();

        WHEN("cfg_a is written, snapshot taken, then cfg_b is written and snapshot taken") {
            controller.set_ssl_config(cfg_a);
            const SSLConfig snap_a = controller.ssl_config_snapshot();

            controller.set_ssl_config(cfg_b);
            const SSLConfig snap_b = controller.ssl_config_snapshot();

            THEN("snap_a still observes cfg_a and snap_b observes cfg_b") {
                REQUIRE(ssl_config_equal(snap_a, cfg_a));
                REQUIRE(ssl_config_equal(snap_b, cfg_b));
            }
        }
    }
}

SCENARIO("TbdController SSL config monitor: concurrent reader and writer never tear") {
    GIVEN("a controller, a writer alternating cfg_a/cfg_b, and a reader taking snapshots") {
        auto controller = make_controller(SSLConfig{});
        const auto cfg_a = make_cfg_a();
        const auto cfg_b = make_cfg_b();
        controller.set_ssl_config(cfg_a);

        constexpr int iterations = 100;

        WHEN("100 writes and 100 reads run concurrently") {
            std::atomic<bool> torn{false};

            std::thread writer([&]() {
                for (int i = 0; i < iterations; ++i) {
                    controller.set_ssl_config((i % 2 == 0) ? cfg_a : cfg_b);
                }
            });

            std::thread reader([&]() {
                for (int i = 0; i < iterations; ++i) {
                    const SSLConfig snap = controller.ssl_config_snapshot();
                    if (!ssl_config_equal(snap, cfg_a) && !ssl_config_equal(snap, cfg_b)) {
                        torn.store(true);
                        return;
                    }
                }
            });

            writer.join();
            reader.join();

            THEN("every observed snapshot equals either cfg_a or cfg_b") {
                REQUIRE_FALSE(torn.load());
            }
        }
    }
}
