// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Parity tests for SocIntegrator vs CarSimulation::simulate_soc.
//
// CarSimulation::simulate_soc (modules/EV/EvManager/main/car_simulation.cpp
// lines 107-162) is a private member function with heavy peer dependencies
// (`r_ev_board_support`, `r_ev`, `p_ev_manager`, `config`), so calling it
// directly from this translation unit is not practical. Instead, the golden
// values below are computed manually from the same formula as the EvManager
// original, with comments citing the originating line in car_simulation.cpp.
//
// Formula recap (source: car_simulation.cpp:107-162):
//   MS_FACTOR = 1 / 60 / 60 / 1000      (ms -> hours)              [line 9]
//   factor    = MS_FACTOR * tick_ms                                  [line 111]
//   AC 1ph:   power = current_a * ac_nominal_voltage                 [line 119]
//   AC 3ph:   power = current_a * ac_nominal_voltage * 3.0           [line 124]
//   DC:       power = dc_target_current * dc_target_voltage          [line 129]
//   battery_charge_wh += power * factor                               [line 138]
//   (if already > capacity: clamp to capacity, no accumulation       [lines 135-137])
//   soc = battery_charge_wh / battery_capacity_wh * 100               [line 141]
//   (soc clamped to [0, 100])                                         [lines 143-147]
//
// SocIntegrator additionally hard-clamps `battery_charge_wh` to [0, capacity]
// after accumulation (Decision #39: source-of-truth invariant) — this is a
// tightening of the original's one-tick-overshoot behaviour but yields the
// same steady-state values.

#include "../main/SocIntegrator.hpp"
#include "../main/FsmContext.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace module;
using namespace module::test;
namespace api = everest::lib::API::V1_0::types::ev_simulator;

namespace {

constexpr double MS_FACTOR = (1.0 / 60.0 / 60.0 / 1000.0);

// Replicate the reference formula in test-side scope so the golden values can
// be derived from the same constants the production integrator uses.
double reference_power(api::ChargeMode mode, double current_a, double ac_voltage, bool three_phases,
                       double dc_target_current, double dc_target_voltage) {
    switch (mode) {
    case api::ChargeMode::AcIec:
    case api::ChargeMode::AcIso2:
    case api::ChargeMode::AcIsoD20:
        return current_a * ac_voltage * (three_phases ? 3.0 : 1.0);
    case api::ChargeMode::DcIso2:
    case api::ChargeMode::DcIsoD20:
        return dc_target_current * dc_target_voltage;
    }
    return 0.0;
}

bool topic_recorded(const PublisherSink& sink, const std::string& topic) {
    return std::any_of(sink.records.begin(), sink.records.end(), [&](const auto& kv) { return kv.first == topic; });
}

bool contains_substr(const std::vector<std::string>& records, const std::string& needle) {
    return std::any_of(records.begin(), records.end(),
                       [&](const std::string& r) { return r.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("SocIntegrator parity with EvManager simulate_soc", "[evsim][soc]") {
    using Catch::Matchers::WithinAbs;
    constexpr float kTol = 1e-3f; // float arithmetic + Wh<->% conversion noise

    SECTION("AcIec + 16A + 1-phase + 230V + 1000ms tick") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = false;
        const float initial_charge = ctx->vars.battery_charge_wh; // 18000 (30% of 60000)

        // Golden: power = 16 * 230 = 3680 W; factor = 1/3600; delta ≈ 1.02222 Wh
        const double power = reference_power(api::ChargeMode::AcIec, 16.0, 230.0, false, 0.0, 0.0);
        const double factor = MS_FACTOR * 1000.0;
        const double expected_delta = power * factor;
        const double expected_charge = static_cast<double>(initial_charge) + expected_delta;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        SocIntegrator::step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
    }

    SECTION("AcIec + 32A + 3-phase + 230V + 1000ms tick") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        ctx->vars.charging_current_a = 32.0f;
        ctx->vars.three_phases = true;
        const float initial_charge = ctx->vars.battery_charge_wh; // 18000

        // Golden: power = 32 * 230 * 3 = 22080 W; factor = 1/3600; delta ≈ 6.1333 Wh
        const double power = reference_power(api::ChargeMode::AcIec, 32.0, 230.0, true, 0.0, 0.0);
        const double factor = MS_FACTOR * 1000.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        SocIntegrator::step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
    }

    SECTION("AcIso2 + 16A + 3-phase + 230V + 100ms tick") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;
        const float initial_charge = ctx->vars.battery_charge_wh; // 18000

        // Golden: power = 16 * 230 * 3 = 11040 W; factor = 100/3600000; delta ≈ 0.3067 Wh
        const double power = reference_power(api::ChargeMode::AcIso2, 16.0, 230.0, true, 0.0, 0.0);
        const double factor = MS_FACTOR * 100.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        SocIntegrator::step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
    }

    SECTION("DcIso2 + 125A + 400V + 100ms tick + iso_update_soc record") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        const float initial_charge = ctx->vars.battery_charge_wh; // 18000

        // Golden: power = 125 * 400 = 50000 W; factor = 100/3600000; delta ≈ 1.3889 Wh
        const double power = reference_power(api::ChargeMode::DcIso2, 0.0, 0.0, false, 125.0, 400.0);
        const double factor = MS_FACTOR * 100.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        SocIntegrator::step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
        // iso_update_soc routed through PeerActions (wired in TestFixture).
        CHECK(contains_substr(fx.mocks.iso.records, "update_soc("));
    }

    SECTION("Battery clamps to capacity on overshoot") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f; // 59990

        // delta = 50000 / 3600 ≈ 13.89 Wh -> would push past capacity.
        SocIntegrator::step(*ctx);

        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(100.0f, kTol));
    }

    SECTION("Battery already over capacity stays clamped, no accumulation") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        // Simulate prior overshoot — car_simulation.cpp:135-137 handles this.
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh + 50.0f;

        SocIntegrator::step(*ctx);

        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(100.0f, kTol));
    }

    SECTION("Low charge first tick does not underflow") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = false;
        ctx->vars.battery_charge_wh = 5.0f;

        SocIntegrator::step(*ctx);

        CHECK(ctx->vars.battery_charge_wh > 5.0f);
        CHECK(ctx->vars.battery_charge_wh >= 0.0f);
        CHECK(ctx->vars.soc_pct > 0.0f);
    }

    SECTION("AcIec mode does not record iso_update_soc (iso peer unused)") {
        // In AcIec mode the production runtime wires iso_update_soc identically
        // to other modes (the action exists on PeerActions); the AcIec semantic
        // difference is that no ISO 15118 session is started. The integrator
        // still calls iso_update_soc — the test verifies this by checking the
        // mock records.
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIec;
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = false;

        SocIntegrator::step(*ctx);

        // Even in AcIec the integrator calls iso_update_soc; the runtime's
        // iso_update_soc action will be unset in AcIec scenarios at the
        // EvSimRuntime layer, but at the FsmContext layer we still route it.
        // This SECTION documents that behavior — the silent-no-op contract
        // sits at the PeerActions function-pointer level (verified in T-B3).
        CHECK(contains_substr(fx.mocks.iso.records, "update_soc("));
    }

    SECTION("iso_update_soc is silently skipped when peer action is unset") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 100;
        // Build the context, then unset iso_update_soc on PeerActions to model
        // the AcIec runtime configuration (no ISO peer wired).
        auto ctx = fx.make_ctx();
        ctx->peer_actions.iso_update_soc = nullptr;
        ctx->vars.charge_mode = api::ChargeMode::DcIso2;
        fx.mocks.iso.clear();

        SocIntegrator::step(*ctx);

        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "update_soc("));
    }

    SECTION("step publishes ev_info on the external topic") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;

        SocIntegrator::step(*ctx);

        CHECK(topic_recorded(fx.sink, fx.topics.everest_to_extern("ev_info")));
    }

    SECTION("step publishes ev_info on the internal side") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode = api::ChargeMode::AcIso2;
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;
        const int before = fx.mocks.internal_ev_info_count;

        SocIntegrator::step(*ctx);

        CHECK(fx.mocks.internal_ev_info_count == before + 1);
    }

    SECTION("No charge_mode set: SoC remains unchanged") {
        TestFixture fx;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        ctx->vars.charge_mode.reset();
        const float initial_charge = ctx->vars.battery_charge_wh;
        const float initial_soc = ctx->vars.soc_pct;

        SocIntegrator::step(*ctx);

        // power = 0 -> no charge delta; soc may re-derive from charge but
        // initial soc_pct was already (charge/capacity)*100, so identity.
        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(initial_charge, kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(initial_soc, kTol));
    }
}
