// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../util/raise_error_router.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

namespace {

// Minimal Everest::error::Error builder usable without a real ErrorFactory.
// Uses the constructor that accepts plain strings for origin module/impl.
Everest::error::Error make_test_error(const std::string& type, const std::string& sub_type, const std::string& message,
                                      Everest::error::Severity severity) {
    return Everest::error::Error(type, sub_type, message, "test", "test-module", "test-impl", severity);
}

struct RaiseCall {
    std::string target;
    Everest::error::Error error;
};

struct ClearCall {
    std::string target;
    std::string type;
    std::string sub_type;
};

// Builds a PeerAdapters where every call is recorded into the supplied vectors.
// make_*_error delegates to make_test_error so no real ErrorFactory is needed.
yeti_sim_router::PeerAdapters recording_adapters(std::vector<RaiseCall>& raises, std::vector<ClearCall>& clears) {
    yeti_sim_router::PeerAdapters p;

    auto record_raise = [&raises](const std::string& target) {
        return [&raises, target](const Everest::error::Error& e) { raises.push_back({target, e}); };
    };
    auto record_clear = [&clears](const std::string& target) {
        return [&clears, target](const std::string& type, const std::string& sub_type) {
            clears.push_back({target, type, sub_type});
        };
    };

    p.raise_board_support = record_raise("board_support");
    p.clear_board_support = record_clear("board_support");
    p.raise_connector_lock = record_raise("connector_lock");
    p.clear_connector_lock = record_clear("connector_lock");
    p.raise_rcd = record_raise("rcd");
    p.clear_rcd = record_clear("rcd");
    p.raise_powermeter = record_raise("powermeter");
    p.clear_powermeter = record_clear("powermeter");

    p.make_board_support_error = make_test_error;
    p.make_connector_lock_error = make_test_error;
    p.make_rcd_error = make_test_error;
    p.make_powermeter_error = make_test_error;

    return p;
}

namespace api = everest::lib::API::V1_0::types::yeti_simulator;

} // namespace

TEST_CASE("route_raise dispatches BoardSupport with lookup defaults", "[yetisim][router]") {
    std::vector<RaiseCall> raises;
    std::vector<ClearCall> clears;
    auto peers = recording_adapters(raises, clears);

    api::RaiseError cmd;
    cmd.type = "evse_board_support/MREC6UnderVoltage";

    const bool ok = yeti_sim_router::route_raise(cmd, peers);

    REQUIRE(ok);
    REQUIRE(raises.size() == 1);
    CHECK(clears.empty());

    CHECK(raises[0].target == "board_support");
    CHECK(raises[0].error.type == "evse_board_support/MREC6UnderVoltage");
    CHECK(raises[0].error.severity == Everest::error::Severity::High);
    CHECK(raises[0].error.message == "Simulated fault event");
    CHECK(raises[0].error.sub_type == "");
}

TEST_CASE("route_raise applies severity override", "[yetisim][router]") {
    std::vector<RaiseCall> raises;
    std::vector<ClearCall> clears;
    auto peers = recording_adapters(raises, clears);

    api::RaiseError cmd;
    cmd.type = "evse_board_support/MREC6UnderVoltage";
    cmd.severity = api::Severity::Low;

    const bool ok = yeti_sim_router::route_raise(cmd, peers);

    REQUIRE(ok);
    REQUIRE(raises.size() == 1);
    // Severity override must win; lookup default is High.
    CHECK(raises[0].error.severity == Everest::error::Severity::Low);
    // Other fields still from the lookup table.
    CHECK(raises[0].error.message == "Simulated fault event");
    CHECK(raises[0].error.sub_type == "");
    CHECK(raises[0].target == "board_support");
}

TEST_CASE("route_raise dispatches ConnectorLock for connector_lock/* type", "[yetisim][router]") {
    std::vector<RaiseCall> raises;
    std::vector<ClearCall> clears;
    auto peers = recording_adapters(raises, clears);

    api::RaiseError cmd;
    cmd.type = "connector_lock/ConnectorLockUnexpectedClose";

    const bool ok = yeti_sim_router::route_raise(cmd, peers);

    REQUIRE(ok);
    REQUIRE(raises.size() == 1);
    CHECK(raises[0].target == "connector_lock");
    CHECK(raises[0].error.type == "connector_lock/ConnectorLockUnexpectedClose");
}

TEST_CASE("route_clear dispatches to BoardSupport; sub_type propagated when provided", "[yetisim][router]") {
    SECTION("clear without sub_type uses lookup default (empty string)") {
        std::vector<RaiseCall> raises;
        std::vector<ClearCall> clears;
        auto peers = recording_adapters(raises, clears);

        api::ClearError cmd;
        cmd.type = "evse_board_support/MREC6UnderVoltage";

        const bool ok = yeti_sim_router::route_clear(cmd, peers);

        REQUIRE(ok);
        REQUIRE(clears.size() == 1);
        CHECK(raises.empty());
        CHECK(clears[0].target == "board_support");
        CHECK(clears[0].type == "evse_board_support/MREC6UnderVoltage");
        CHECK(clears[0].sub_type == "");
    }

    SECTION("clear with sub_type override propagates it") {
        std::vector<RaiseCall> raises;
        std::vector<ClearCall> clears;
        auto peers = recording_adapters(raises, clears);

        api::ClearError cmd;
        cmd.type = "evse_board_support/MREC6UnderVoltage";
        cmd.sub_type = "custom_sub";

        const bool ok = yeti_sim_router::route_clear(cmd, peers);

        REQUIRE(ok);
        REQUIRE(clears.size() == 1);
        CHECK(clears[0].sub_type == "custom_sub");
    }
}

TEST_CASE("route_raise dispatches Rcd for ac_rcd/* type", "[yetisim][router]") {
    std::vector<RaiseCall> raises;
    std::vector<ClearCall> clears;
    auto peers = recording_adapters(raises, clears);

    api::RaiseError cmd;
    cmd.type = "ac_rcd/DC";

    const bool ok = yeti_sim_router::route_raise(cmd, peers);

    REQUIRE(ok);
    REQUIRE(raises.size() == 1);
    CHECK(raises[0].target == "rcd");
    CHECK(raises[0].error.type == "ac_rcd/DC");
    CHECK(raises.size() == 1); // no board_support / connector_lock / powermeter call
}

TEST_CASE("route_raise with unknown type returns false and makes no peer call", "[yetisim][router]") {
    std::vector<RaiseCall> raises;
    std::vector<ClearCall> clears;
    auto peers = recording_adapters(raises, clears);

    api::RaiseError cmd;
    cmd.type = "evse_board_support/NoSuchError";

    const bool ok = yeti_sim_router::route_raise(cmd, peers);

    CHECK_FALSE(ok);
    CHECK(raises.empty());
    CHECK(clears.empty());
}
