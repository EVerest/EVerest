// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../main/FsmContext.hpp"
#include "../main/SocIntegrator.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_controller;

namespace {

constexpr float k_tol = 1e-3F;

std::size_t count_prefix(const std::vector<std::string>& records, const std::string& prefix) {
    return static_cast<std::size_t>(std::count_if(
        records.begin(), records.end(), [&prefix](const std::string& rec) { return rec.rfind(prefix, 0) == 0; }));
}

bool event_kind_enqueued(const std::vector<Event>& evs, EventKind kind) {
    return std::any_of(evs.begin(), evs.end(), [kind](const Event& ev) { return kind_of(ev) == kind; });
}

void make_external(TestFixture& fx) {
    fx.cfg.soc_source = "external";
    fx.cfg.dc_target_current = 125;
    fx.cfg.dc_target_voltage = 400;
    fx.cfg.tick_interval_ms = 1000;
}

} // namespace

TEST_CASE("soc_source parses internal and external", "[evctl][soc][external]") {
    CHECK(parse_soc_source("internal") == SocSource::Internal);
    CHECK(parse_soc_source("external") == SocSource::External);
    CHECK_THROWS_AS(parse_soc_source("bms"), std::invalid_argument);
    CHECK_THROWS_AS(parse_soc_source(""), std::invalid_argument);
}

TEST_CASE("External battery state is taken as measured", "[evctl][soc][external]") {
    using Catch::Matchers::WithinAbs;

    SECTION("SoC and capacity set the battery state and reach ISO at once") {
        TestFixture fx;
        make_external(fx);
        auto ctx = fx.make_ctx();
        REQUIRE(ctx->soc_source == SocSource::External);

        ctx->apply_external_battery_state(67.0F, 4000.0F);

        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(67.0F, k_tol));
        CHECK_THAT(ctx->vars.battery_capacity_wh, WithinAbs(4000.0F, k_tol));
        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(2680.0F, k_tol));
        CHECK(fx.mocks.iso.records.back().rfind("update_soc(SoC=67", 0) == 0);
    }

    SECTION("A missing or non-positive capacity keeps the previous one") {
        TestFixture fx;
        make_external(fx);
        auto ctx = fx.make_ctx();
        const float capacity = ctx->vars.battery_capacity_wh;

        ctx->apply_external_battery_state(50.0F, std::nullopt);
        CHECK_THAT(ctx->vars.battery_capacity_wh, WithinAbs(capacity, k_tol));
        ctx->apply_external_battery_state(50.0F, 0.0F);
        CHECK_THAT(ctx->vars.battery_capacity_wh, WithinAbs(capacity, k_tol));
        ctx->apply_external_battery_state(50.0F, -10.0F);
        CHECK_THAT(ctx->vars.battery_capacity_wh, WithinAbs(capacity, k_tol));
        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(capacity / 2.0F, k_tol));
    }

    SECTION("A capacity alone rescales the charge and sends no SoC") {
        TestFixture fx;
        make_external(fx);
        auto ctx = fx.make_ctx();
        ctx->apply_external_battery_state(40.0F, 10000.0F);
        fx.mocks.iso.clear();

        ctx->apply_external_battery_state(std::nullopt, 5000.0F);

        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(40.0F, k_tol));
        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(2000.0F, k_tol));
        CHECK(count_prefix(fx.mocks.iso.records, "update_soc(") == 0);
    }

    SECTION("An out-of-range SoC is clamped") {
        TestFixture fx;
        make_external(fx);
        auto ctx = fx.make_ctx();
        ctx->apply_external_battery_state(120.0F, std::nullopt);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(100.0F, k_tol));
        ctx->apply_external_battery_state(-5.0F, std::nullopt);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(0.0F, k_tol));
    }
}

TEST_CASE("External SoC is never integrated while charging", "[evctl][soc][external]") {
    using Catch::Matchers::WithinAbs;

    TestFixture fx;
    make_external(fx);
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.evse_dc_present_current_a = 125.0F;
    ctx->apply_external_battery_state(55.0F, 4000.0F);
    fx.mocks.iso.clear();

    for (int tick = 0; tick < 10; ++tick) {
        soc_step(*ctx);
    }

    CHECK_THAT(ctx->vars.soc_pct, WithinAbs(55.0F, k_tol));
    CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(2200.0F, k_tol));
    CHECK(count_prefix(fx.mocks.iso.records, "update_soc(SoC=55") == 10);
    CHECK(count_prefix(fx.mocks.iso.records, "update_present_values(") == 10);
}

TEST_CASE("Internal SoC still integrates", "[evctl][soc][external]") {
    TestFixture fx;
    fx.cfg.dc_target_current = 125;
    fx.cfg.dc_target_voltage = 400;
    fx.cfg.tick_interval_ms = 1000;
    auto ctx = fx.make_ctx();
    REQUIRE(ctx->soc_source == SocSource::Internal);
    set_mode(*ctx, api::ChargeMode::DcIso2);
    ctx->vars.evse_dc_present_current_a = 125.0F;
    const float initial = ctx->vars.soc_pct;

    soc_step(*ctx);

    CHECK(ctx->vars.soc_pct > initial);
}

TEST_CASE("on_battery_full fires on an external SoC crossing the threshold", "[evctl][soc][external]") {
    TestFixture fx;
    make_external(fx);
    fx.cfg.on_battery_full = "stop_session";
    fx.cfg.battery_full_threshold_pct = 80.0;
    auto ctx = fx.make_ctx();
    set_mode(*ctx, api::ChargeMode::DcIso2);

    ctx->apply_external_battery_state(79.0F, std::nullopt);
    soc_step(*ctx);
    CHECK_FALSE(event_kind_enqueued(fx.timer.enqueued_events, EventKind::StopSession));

    ctx->apply_external_battery_state(80.0F, std::nullopt);
    soc_step(*ctx);
    CHECK(event_kind_enqueued(fx.timer.enqueued_events, EventKind::StopSession));
}

TEST_CASE("An ISO start seeds the SoC before the session runs", "[evctl][soc][iso]") {
    SECTION("DC, external: the measured SoC and capacity") {
        TestFixture fx;
        make_external(fx);
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->apply_external_battery_state(42.0F, 4000.0F);
        fx.mocks.iso.clear();

        REQUIRE(ctx->iso_start_charging(api::ChargeMode::DcIso2, std::nullopt, 0, 0));

        const auto& recs = fx.mocks.iso.records;
        const auto soc_at = std::find_if(recs.begin(), recs.end(),
                                         [](const std::string& rec) { return rec.rfind("update_soc(SoC=42", 0) == 0; });
        const auto start_at = std::find_if(recs.begin(), recs.end(),
                                           [](const std::string& rec) { return rec.rfind("start_charging(", 0) == 0; });
        REQUIRE(soc_at != recs.end());
        REQUIRE(start_at != recs.end());
        CHECK(soc_at < start_at);
        CHECK(contains_substr(recs, "energy_capacity=4000"));
    }

    SECTION("AC, internal: the initial SoC") {
        TestFixture fx;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);

        REQUIRE(ctx->iso_start_charging(api::ChargeMode::AcIso2, std::nullopt, 0, 0));

        CHECK(contains_substr(fx.mocks.iso.records, "update_soc(SoC=30"));
    }
}
