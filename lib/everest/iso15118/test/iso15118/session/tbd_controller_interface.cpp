// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <memory>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/tbd_controller.hpp>

#include "mock_connection.hpp"

using iso15118::test::MockConnection;

namespace {

struct MockConnectionFactory {
    MockConnection* last{nullptr};
    int calls{0};

    iso15118::TbdController::ConnectionFactory fn() {
        return [this](iso15118::io::PollManager&, const std::string&) -> std::unique_ptr<iso15118::io::IConnection> {
            auto connection = std::make_unique<MockConnection>();
            last = connection.get();
            ++calls;
            return connection;
        };
    }
};

// Stands in for if_nametoindex: the test moves the device by changing the index.
struct FakeInterface {
    unsigned int index{0};
    int lookups{0};

    iso15118::TbdController::InterfaceIndexLookup fn() {
        return [this](std::string&) {
            ++lookups;
            return index;
        };
    }
};

struct Fixture {
    bool terminate_signaled{false};
    bool error_signaled{false};
    iso15118::session::feedback::Callbacks callbacks;
    MockConnectionFactory factory;
    FakeInterface interface;
    std::unique_ptr<iso15118::TbdController> controller;

    Fixture() {
        callbacks.signal = [this](iso15118::session::feedback::Signal s) {
            if (s == iso15118::session::feedback::Signal::DLINK_TERMINATE) {
                terminate_signaled = true;
            }
            if (s == iso15118::session::feedback::Signal::DLINK_ERROR) {
                error_signaled = true;
            }
        };
    }

    void make_controller() {
        iso15118::TbdConfig config{{},
                                   "plc0",
                                   iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER,
                                   /*enable_sdp_server=*/false};
        config.interface_check_interval_ms = 0; // check on every tick
        controller = std::make_unique<iso15118::TbdController>(
            std::move(config), callbacks, iso15118::session::EvseSetupConfig{}, factory.fn(), interface.fn());
    }
};

} // namespace

SCENARIO("A missing interface at startup is waited for") {
    Fixture f;

    GIVEN("the interface does not exist when the controller is constructed") {
        f.interface.index = 0;
        REQUIRE_NOTHROW(f.make_controller());

        WHEN("the loop ticks") {
            f.controller->tick();
            f.controller->tick();

            THEN("no session is created") {
                REQUIRE(f.factory.calls == 0);
                REQUIRE_FALSE(f.controller->has_active_session());
            }
        }

        WHEN("the interface appears") {
            f.interface.index = 3;
            f.controller->tick();

            THEN("a session is created on it") {
                REQUIRE(f.factory.calls == 1);
                REQUIRE(f.controller->has_active_session());
            }
        }
    }
}

SCENARIO("The interface disappearing and reappearing at runtime") {
    Fixture f;
    f.interface.index = 3;
    f.make_controller();
    f.controller->tick();
    REQUIRE(f.factory.calls == 1);
    f.factory.last->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    f.terminate_signaled = false;

    WHEN("the interface keeps its index") {
        f.controller->tick();

        THEN("the session is untouched") {
            REQUIRE_FALSE(f.terminate_signaled);
            REQUIRE(f.factory.calls == 1);
            REQUIRE(f.controller->has_active_session());
        }
    }

    WHEN("the interface is gone") {
        f.interface.index = 0;
        f.controller->tick();

        THEN("the session is torn down and none is created") {
            REQUIRE(f.terminate_signaled);
            REQUIRE_FALSE(f.error_signaled);
            REQUIRE(f.factory.calls == 1);
            REQUIRE_FALSE(f.controller->has_active_session());

            AND_WHEN("the interface comes back under a new index") {
                f.interface.index = 4;
                f.terminate_signaled = false;
                f.controller->tick();

                THEN("a fresh session is created") {
                    REQUIRE_FALSE(f.terminate_signaled);
                    REQUIRE(f.factory.calls == 2);
                    REQUIRE(f.controller->has_active_session());
                }
            }
        }
    }

    WHEN("the interface is replaced between two checks") {
        f.interface.index = 4;
        f.controller->tick();

        THEN("the old session is torn down and a fresh one created in the same tick") {
            REQUIRE(f.terminate_signaled);
            REQUIRE(f.factory.calls == 2);
            REQUIRE(f.controller->has_active_session());
        }
    }
}

SCENARIO("A listening endpoint that cannot be opened is retried") {
    Fixture f;
    int failures = 0;
    bool fail = true;
    iso15118::TbdController::ConnectionFactory factory = [&](iso15118::io::PollManager&, const std::string&) {
        if (fail) {
            ++failures;
            throw std::runtime_error("address not available");
        }
        return std::unique_ptr<iso15118::io::IConnection>(std::make_unique<MockConnection>());
    };
    f.interface.index = 3;
    iso15118::TbdConfig config{{},
                               "plc0",
                               iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER,
                               /*enable_sdp_server=*/false};
    config.interface_check_interval_ms = 0;
    iso15118::TbdController controller{std::move(config), f.callbacks, iso15118::session::EvseSetupConfig{}, factory,
                                       f.interface.fn()};

    WHEN("the loop ticks while the endpoint cannot be opened") {
        controller.tick();
        controller.tick();

        THEN("the loop survives and keeps retrying") {
            REQUIRE(failures == 2);
            REQUIRE_FALSE(controller.has_active_session());

            AND_WHEN("the endpoint can be opened") {
                fail = false;
                controller.tick();

                THEN("a session is created") {
                    REQUIRE(controller.has_active_session());
                }
            }
        }
    }

    WHEN("the data link is ready but the interface goes away before an endpoint could be opened") {
        controller.set_dlink_ready(true);
        controller.tick();
        REQUIRE_FALSE(f.error_signaled);
        f.interface.index = 0;
        controller.tick();

        THEN("the link loss is reported as D-LINK_ERROR right away, with no session to carry a terminate") {
            REQUIRE(f.error_signaled);
            REQUIRE_FALSE(f.terminate_signaled);
            REQUIRE_FALSE(controller.has_active_session());
        }
    }
}

SCENARIO("A nonexistent device with the SDP server enabled does not fail construction") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};
    MockConnectionFactory factory;

    iso15118::TbdConfig config{{},
                               "nonexistent-plc0",
                               iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER,
                               /*enable_sdp_server=*/true};
    config.interface_check_interval_ms = 0;

    std::unique_ptr<iso15118::TbdController> controller;
    REQUIRE_NOTHROW(controller = std::make_unique<iso15118::TbdController>(
                        std::move(config), callbacks, iso15118::session::EvseSetupConfig{}, factory.fn()));

    WHEN("the loop ticks and the data link is reported ready") {
        controller->set_dlink_ready(true);
        REQUIRE_NOTHROW(controller->tick());

        THEN("nothing is listening") {
            REQUIRE_FALSE(controller->has_active_session());
            REQUIRE(factory.calls == 0);
        }
    }
}
