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
    iso15118::config::ChainConfig chain{};
    chain.path_certificate_chain = "/tmp/iso15118-test/a/chain.pem";
    chain.path_certificate_key = "/tmp/iso15118-test/a/key.pem";
    chain.trust_anchor_pem = "/tmp/iso15118-test/a/v2g_root.pem";
    cfg.chains.push_back(std::move(chain));
    cfg.path_certificate_v2g_root = "/tmp/iso15118-test/a/v2g_root.pem";
    cfg.path_certificate_mo_root = "/tmp/iso15118-test/a/mo_root.pem";
    cfg.enforce_tls_1_3 = false;
    return cfg;
}

SSLConfig make_cfg_b() {
    SSLConfig cfg{};
    cfg.backend = CertificateBackend::EVEREST_LAYOUT;
    iso15118::config::ChainConfig chain{};
    chain.path_certificate_chain = "/tmp/iso15118-test/b/chain.pem";
    chain.path_certificate_key = "/tmp/iso15118-test/b/key.pem";
    chain.trust_anchor_pem = "/tmp/iso15118-test/b/v2g_root.pem";
    cfg.chains.push_back(std::move(chain));
    cfg.path_certificate_v2g_root = "/tmp/iso15118-test/b/v2g_root.pem";
    cfg.path_certificate_mo_root = "/tmp/iso15118-test/b/mo_root.pem";
    cfg.enforce_tls_1_3 = true;
    return cfg;
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
                REQUIRE(snap == cfg_a);
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
                REQUIRE(snap_a == cfg_a);
                REQUIRE(snap_b == cfg_b);
            }
        }
    }
}

// Value-level pin of the rotation seam: connection_ssl_config() must expose the
// rotated config, not the construction-time TbdConfig::ssl. The factory lambda's
// choice of accessor is not observable here (connection construction needs a live
// SDP/TLS stack); the named seam and its doc comment guard that wiring.
SCENARIO("TbdController SSL config monitor: the rotation seam exposes the rotated config") {
    GIVEN("a controller constructed with cfg_a") {
        const auto cfg_a = make_cfg_a();
        auto controller = make_controller(cfg_a);

        WHEN("set_ssl_config(cfg_b) rotates the config") {
            const auto cfg_b = make_cfg_b();
            controller.set_ssl_config(cfg_b);

            THEN("connection_ssl_config() returns cfg_b, not the construction-time cfg_a") {
                REQUIRE(controller.connection_ssl_config() == cfg_b);
                REQUIRE_FALSE(controller.connection_ssl_config() == cfg_a);
            }
        }
    }
}

SCENARIO("TbdController SSL config monitor: concurrent snapshots always observe a complete config") {
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
                    if (!(snap == cfg_a) && !(snap == cfg_b)) {
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
