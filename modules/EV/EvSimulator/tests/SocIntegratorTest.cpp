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
//   soc = battery_charge_wh / battery_capacity_wh * 100               [line 141]
//   (soc clamped to [0, 100])                                         [lines 143-147]
//
// SocIntegrator diverges from the original on two points, both required
// for BPT / V2X discharge support:
//   * Always integrates: drops the legacy "skip accumulation when already
//     over capacity" short-circuit (car_simulation.cpp:135-137), which
//     silently dropped discharge energy when starting above capacity.
//   * Hard-clamps `battery_charge_wh` to [0, capacity] after accumulation,
//     keeping the source-of-truth invariant and handling underflow on
//     discharge symmetrically to overshoot on charge.
// Steady-state charge-only values match the original parity-tested cases.

#include "../main/SocIntegrator.hpp"
#include "../main/FsmContext.hpp"
#include "../main/states/Unplugged.hpp"
#include "TestFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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

// Predicate to assert an Event of a given kind was enqueued.
bool event_kind_enqueued(const std::vector<module::Event>& evs, module::EventKind k) {
    return std::any_of(evs.begin(), evs.end(), [k](const module::Event& ev) { return kind_of(ev) == k; });
}

} // namespace

TEST_CASE("SocIntegrator parity with EvManager simulate_soc", "[evsim][soc]") {
    using Catch::Matchers::WithinAbs;
    constexpr float kTol = 1e-3f; // float arithmetic + Wh<->% conversion noise

    SECTION("AC parity across mode / current / phases / tick") {
        struct AcParityCase {
            const char* name;
            api::ChargeMode mode;
            float current_a;
            bool three_phases;
            int tick_ms;
        };
        auto tc = GENERATE(values<AcParityCase>({
            {"AcIec + 16A + 1-phase + 230V + 1000ms tick", api::ChargeMode::AcIec, 16.0f, false, 1000},
            {"AcIec + 32A + 3-phase + 230V + 1000ms tick", api::ChargeMode::AcIec, 32.0f, true, 1000},
            {"AcIso2 + 16A + 3-phase + 230V + 100ms tick", api::ChargeMode::AcIso2, 16.0f, true, 100},
            {"AcIso2 + 16A + 1-phase + 230V + 1000ms tick", api::ChargeMode::AcIso2, 16.0f, false, 1000},
            {"AcIsoD20 + 32A + 3-phase + 230V + 1000ms tick", api::ChargeMode::AcIsoD20, 32.0f, true, 1000},
            {"AcIsoD20 + 16A + 1-phase + 230V + 100ms tick", api::ChargeMode::AcIsoD20, 16.0f, false, 100},
        }));
        CAPTURE(tc.name);

        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = tc.tick_ms;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, tc.mode);
        ctx->vars.charging_current_a = tc.current_a;
        ctx->vars.three_phases = tc.three_phases;
        const float initial_charge = ctx->vars.battery_charge_wh; // 18000 (30% of 60000)

        const double power =
            reference_power(tc.mode, static_cast<double>(tc.current_a), 230.0, tc.three_phases, 0.0, 0.0);
        const double factor = MS_FACTOR * static_cast<double>(tc.tick_ms);
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
    }

    SECTION("DcIso2 live 125A/400V from EvInfo + 100ms tick + iso_update_soc record") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        const float initial_charge = ctx->vars.battery_charge_wh; // 18000

        // Voltage is seeded from cfg.dc_target_voltage; the live present current
        // stays 0 until a real EvInfo arrives, so emulate the passthrough that
        // EvSimRuntime::apply_passthrough_vars performs to deliver 125 A.
        REQUIRE(ctx->vars.dc_present_current_a == 0.0f);
        REQUIRE(ctx->vars.dc_present_voltage_v == 400.0f);
        ctx->vars.dc_present_current_a = 125.0f;

        // Golden: power = 125 * 400 = 50000 W; factor = 100/3600000; delta ≈ 1.3889 Wh
        const double power = reference_power(api::ChargeMode::DcIso2, 0.0, 0.0, false, 125.0, 400.0);
        const double factor = MS_FACTOR * 100.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
        // iso_update_soc routed through PeerActions (wired in TestFixture).
        CHECK(contains_substr(fx.mocks.iso.records, "update_soc("));
    }

    SECTION("DC session with no EvInfo yet delivers ~0 energy, not cfg target") {
        // Root cause (T2): the FsmContext ctor must not seed
        // vars.dc_present_current_a from cfg.dc_target_current. Until a real
        // EvInfo passthrough (EvSimRuntime::apply_passthrough_vars) sets the
        // live present current, the integrator must treat delivered current as
        // 0 so a zero-energy EVSE yields ~0 delivered energy rather than
        // integrating the static cfg.dc_target_current * dc_target_voltage.
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // No EvInfo has arrived: present current must be 0, not the cfg target.
        REQUIRE(ctx->vars.dc_present_current_a == 0.0f);
        const float initial_charge = ctx->vars.battery_charge_wh;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(initial_charge, kTol));
    }

    SECTION("DcIso2 uses live vars.dc_present_current_a / dc_present_voltage_v over cfg") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // Peer EvInfo passthrough (apply_passthrough_vars in EvSimRuntime)
        // overrides the cfg-seeded defaults at runtime — emulate that here.
        ctx->vars.dc_present_current_a = 60.0f;
        ctx->vars.dc_present_voltage_v = 500.0f;
        const float initial_charge = ctx->vars.battery_charge_wh;

        // Golden uses live vars, not cfg: power = 60 * 500 = 30000 W.
        const double power = 60.0 * 500.0;
        const double factor = MS_FACTOR * 1000.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
    }

    SECTION("DcIsoD20 uses live vars.dc_present_current_a / dc_present_voltage_v over cfg") {
        TestFixture fx;
        fx.cfg.dc_target_current = 200;
        fx.cfg.dc_target_voltage = 800;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        ctx->vars.dc_present_current_a = 150.0f;
        ctx->vars.dc_present_voltage_v = 700.0f;
        const float initial_charge = ctx->vars.battery_charge_wh;

        const double power = 150.0 * 700.0;
        const double factor = MS_FACTOR * 100.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
    }

    SECTION("DcIso2 with zero live current yields no SoC delta") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_current_a = 0.0f;
        ctx->vars.dc_present_voltage_v = 400.0f;
        const float initial_charge = ctx->vars.battery_charge_wh;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(initial_charge, kTol));
    }

    // BPT / V2X discharge: negative current in vars represents reverse-power
    // flow from EV to EVSE. The integrator multiplies current * voltage
    // unchanged, so negative current produces negative power and reduces SoC.
    SECTION("DcIso2 negative live current discharges SoC") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_current_a = -60.0f;
        ctx->vars.dc_present_voltage_v = 500.0f;
        const float initial_charge = ctx->vars.battery_charge_wh;

        const double power = -60.0 * 500.0;
        const double factor = MS_FACTOR * 1000.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;
        const double expected_soc = expected_charge / 60000.0 * 100.0;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(static_cast<float>(expected_soc), kTol));
    }

    SECTION("DcIsoD20 negative live current discharges SoC") {
        TestFixture fx;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIsoD20);
        ctx->vars.dc_present_current_a = -150.0f;
        ctx->vars.dc_present_voltage_v = 700.0f;
        const float initial_charge = ctx->vars.battery_charge_wh;

        const double power = -150.0 * 700.0;
        const double factor = MS_FACTOR * 100.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
    }

    SECTION("AcIso2 negative charging_current_a discharges SoC") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = -10.0f;
        ctx->vars.three_phases = true;
        const float initial_charge = ctx->vars.battery_charge_wh;

        const double power = -10.0 * 230.0 * 3.0;
        const double factor = MS_FACTOR * 1000.0;
        const double expected_charge = static_cast<double>(initial_charge) + power * factor;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK(ctx->vars.battery_charge_wh < initial_charge);
    }

    SECTION("Discharge from near-empty floors at 0 (no underflow)") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_current_a = -125.0f;
        ctx->vars.dc_present_voltage_v = 400.0f;
        ctx->vars.battery_charge_wh = 5.0f; // far less than a single-tick discharge

        soc_step(*ctx);

        CHECK(ctx->vars.battery_charge_wh == 0.0f);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(0.0f, kTol));
    }

    SECTION("Discharge from over-capacity drains by the integrated amount") {
        // Inverse of the existing overshoot-then-clamp case: when battery is
        // already above capacity AND discharging, the integrator must still
        // subtract energy from the over-capacity starting point rather than
        // short-circuit to capacity and silently drop the discharged energy.
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_current_a = -10.0f;
        ctx->vars.dc_present_voltage_v = 400.0f;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh + 50.0f;
        const float starting_charge = ctx->vars.battery_charge_wh;

        // power = -10 * 400 = -4000 W; factor = 1/3600; delta = -1.1111 Wh.
        // Expected = 60050 - 1.1111 = 60048.889; still above 60000 capacity,
        // so the post-clamp brings it down to capacity = 60000.
        const double power = -10.0 * 400.0;
        const double factor = MS_FACTOR * 1000.0;
        const double integrated = static_cast<double>(starting_charge) + power * factor;
        const double expected_charge = std::min(integrated, static_cast<double>(ctx->vars.battery_capacity_wh));

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
    }

    SECTION("Deep discharge from over-capacity drops below capacity") {
        // Strict case: over-capacity start + discharge larger than the
        // over-capacity headroom must integrate fully, landing below capacity.
        // The short-circuit-to-capacity branch on the legacy path silently
        // ignored discharge here.
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_current_a = -125.0f;
        ctx->vars.dc_present_voltage_v = 400.0f;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh + 5.0f;
        const float starting_charge = ctx->vars.battery_charge_wh;

        // power = -50000 W; delta = -13.889 Wh; expected = 60005 - 13.889 = 59991.111
        const double power = -125.0 * 400.0;
        const double factor = MS_FACTOR * 1000.0;
        const double expected_charge = static_cast<double>(starting_charge) + power * factor;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(static_cast<float>(expected_charge), kTol));
        CHECK(ctx->vars.battery_charge_wh < ctx->vars.battery_capacity_wh);
    }

    SECTION("Battery clamps to capacity on overshoot") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // EvInfo passthrough delivers the full DC current this tick.
        ctx->vars.dc_present_current_a = 125.0f;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f; // 59990

        // delta = 50000 / 3600 ≈ 13.89 Wh -> would push past capacity.
        soc_step(*ctx);

        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(100.0f, kTol));
    }

    SECTION("Battery already over capacity stays clamped, no accumulation") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // Simulate prior overshoot — car_simulation.cpp:135-137 handles this.
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh + 50.0f;

        soc_step(*ctx);

        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(100.0f, kTol));
    }

    SECTION("Low charge first tick does not underflow") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = false;
        ctx->vars.battery_charge_wh = 5.0f;

        soc_step(*ctx);

        CHECK(ctx->vars.battery_charge_wh > 5.0f);
        CHECK(ctx->vars.battery_charge_wh >= 0.0f);
        CHECK(ctx->vars.soc_pct > 0.0f);
    }

    SECTION("AcIec mode still routes iso_update_soc through PeerActions when peer is wired") {
        // In AcIec mode the production runtime wires iso_update_soc identically
        // to other modes (the action exists on PeerActions); the AcIec semantic
        // difference is that no ISO 15118 session is started. The integrator
        // still calls iso_update_soc — the test verifies this by checking the
        // mock records.
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = false;

        soc_step(*ctx);

        // Even in AcIec the integrator calls iso_update_soc; the runtime's
        // iso_update_soc action will be unset in AcIec scenarios at the
        // EvSimRuntime layer, but at the FsmContext layer we still route it.
        // The silent-no-op contract sits at the PeerActions function-pointer
        // level.
        CHECK(contains_substr(fx.mocks.iso.records, "update_soc("));
    }

    SECTION("iso_update_soc is silently skipped when peer action is unset") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 100;
        // Build the context, then drop the iso peer to model the AcIec runtime
        // configuration (no ISO peer wired).
        auto ctx = fx.make_ctx();
        ctx->peer_actions.iso = {};
        set_mode(*ctx, api::ChargeMode::DcIso2);
        fx.mocks.iso.clear();

        soc_step(*ctx);

        CHECK_FALSE(contains_substr(fx.mocks.iso.records, "update_soc("));
    }

    SECTION("step publishes ev_info on the external topic") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;

        soc_step(*ctx);

        CHECK(topic_recorded(fx.sink, fx.topics.everest_to_extern("ev_info")));
    }

    SECTION("step publishes ev_info on the internal side") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 100;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIso2);
        ctx->vars.charging_current_a = 16.0f;
        ctx->vars.three_phases = true;
        const int before = fx.mocks.internal_ev_info_count;

        soc_step(*ctx);

        CHECK(fx.mocks.internal_ev_info_count == before + 1);
    }

    SECTION("No charge_mode set: SoC remains unchanged") {
        TestFixture fx;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        clear_session(*ctx);
        const float initial_charge = ctx->vars.battery_charge_wh;
        const float initial_soc = ctx->vars.soc_pct;

        soc_step(*ctx);

        // power = 0 -> no charge delta; soc may re-derive from charge but
        // initial soc_pct was already (charge/capacity)*100, so identity.
        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(initial_charge, kTol));
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(initial_soc, kTol));
    }

    SECTION("Zero battery_capacity_wh: SoC stays finite, integrator bails out") {
        // A misconfigured cfg.dc_energy_capacity (= 0) would divide by zero
        // when deriving SoC. The integrator must bail out before publishing
        // a NaN SoC on e2m/ev_info and must not enqueue a StopSession on
        // the resulting non-comparable threshold check.
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.battery_capacity_wh = 0.0f;
        ctx->vars.battery_charge_wh = 0.0f;
        ctx->vars.soc_pct = 0.0f;
        ctx->vars.was_full = false;
        fx.timer.enqueued_events.clear();

        soc_step(*ctx);

        CHECK_FALSE(std::isnan(ctx->vars.soc_pct));
        CHECK_FALSE(std::isnan(ctx->vars.battery_charge_wh));
        CHECK_FALSE(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
    }

    SECTION("Negative battery_capacity_wh: SoC stays finite, integrator bails out") {
        TestFixture fx;
        fx.cfg.tick_interval_ms = 1000;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.battery_capacity_wh = -1.0f;
        ctx->vars.battery_charge_wh = 0.0f;
        ctx->vars.soc_pct = 0.0f;

        soc_step(*ctx);

        CHECK_FALSE(std::isnan(ctx->vars.soc_pct));
        CHECK_FALSE(std::isnan(ctx->vars.battery_charge_wh));
    }
}

TEST_CASE("SocIntegrator on_battery_full policy", "[evsim][soc]") {
    using Catch::Matchers::WithinAbs;
    constexpr float kTol = 1e-3f;

    SECTION("clamp policy preserves legacy integrate-then-clamp on full") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "clamp";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh; // exactly 100%
        ctx->vars.soc_pct = 100.0f;
        ctx->vars.was_full = true;

        soc_step(*ctx);

        CHECK(ctx->vars.battery_charge_wh == ctx->vars.battery_capacity_wh);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(100.0f, kTol));
        CHECK(fx.timer.enqueued_events.empty());
    }

    SECTION("idle_at_full: at cap with positive power, zero accumulation, no event") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "idle_at_full";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh;
        ctx->vars.soc_pct = 100.0f;
        ctx->vars.was_full = true;
        const float starting_charge = ctx->vars.battery_charge_wh;

        soc_step(*ctx);

        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(starting_charge, kTol));
        CHECK(fx.timer.enqueued_events.empty());
    }

    SECTION("idle_at_full: discharge from full still subtracts energy") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "idle_at_full";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.dc_present_current_a = -100.0f;
        ctx->vars.dc_present_voltage_v = 400.0f;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh;
        ctx->vars.soc_pct = 100.0f;
        ctx->vars.was_full = true;
        const float starting_charge = ctx->vars.battery_charge_wh;

        soc_step(*ctx);

        CHECK(ctx->vars.battery_charge_wh < starting_charge);
        CHECK(fx.timer.enqueued_events.empty());
    }

    SECTION("stop_session: rising edge enqueues StopSession") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // EvInfo passthrough delivers the full DC current this tick.
        ctx->vars.dc_present_current_a = 125.0f;
        // Start just below capacity so the integrator crosses the threshold
        // this tick: delta = 50000 * 1/3600 = 13.89 Wh > 10 Wh headroom.
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f;
        ctx->vars.was_full = false;

        soc_step(*ctx);

        CHECK(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
        CHECK(ctx->vars.was_full);
    }

    SECTION("stop_session: no refire on subsequent ticks while still full") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh;
        ctx->vars.soc_pct = 100.0f;
        ctx->vars.was_full = true;

        soc_step(*ctx);

        CHECK_FALSE(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
    }

    SECTION("pause_if_iso: ISO mode rising edge enqueues PauseSession") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "pause_if_iso";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // EvInfo passthrough delivers the full DC current this tick.
        ctx->vars.dc_present_current_a = 125.0f;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f;
        ctx->vars.was_full = false;

        soc_step(*ctx);

        CHECK(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::PauseSession));
    }

    SECTION("pause_if_iso: AcIec rising edge falls back to idle (no event, zero power)") {
        TestFixture fx;
        fx.cfg.ac_nominal_voltage = 230.0;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "pause_if_iso";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::AcIec);
        ctx->vars.charging_current_a = 32.0f;
        ctx->vars.three_phases = true;
        // Cross-threshold tick: starting just below 100%.
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 1.0f;
        ctx->vars.soc_pct = 99.998f;
        ctx->vars.was_full = false;

        soc_step(*ctx);

        CHECK_FALSE(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::PauseSession));
        CHECK_FALSE(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
        CHECK(ctx->vars.was_full);
    }

    SECTION("battery_full_threshold_pct=80 fires policy at 80%, not 100%") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        fx.cfg.battery_full_threshold_pct = 80.0;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // EvInfo passthrough delivers the full DC current this tick.
        ctx->vars.dc_present_current_a = 125.0f;
        // 79% start: 47400 Wh of 60000. Cross to 80% (48000) requires +600.
        // Per-tick delta at 50000 W * 1s = 13.89 Wh; need many ticks. Skip
        // by setting charge just below threshold so a single tick crosses.
        ctx->vars.battery_charge_wh = 47990.0f;
        ctx->vars.soc_pct = 79.983f;
        ctx->vars.was_full = false;

        soc_step(*ctx);

        CHECK(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
    }

    SECTION("crossing tick lands exactly at threshold, no one-tick overshoot") {
        // threshold 80% of 60000 = 48000 Wh. Start at 47990 (just under) so a
        // single 50000 W * 1 s = 13.89 Wh tick crosses. Pre-fix the integrator
        // accumulated the whole delta (48003.89) before the policy engaged the
        // next tick; post-fix it trims to exactly 48000 on the crossing tick.
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        fx.cfg.battery_full_threshold_pct = 80.0;
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // EvInfo passthrough delivers the full DC current this tick.
        ctx->vars.dc_present_current_a = 125.0f;
        ctx->vars.battery_charge_wh = 47990.0f;
        ctx->vars.soc_pct = 79.983f;
        ctx->vars.was_full = false;

        soc_step(*ctx);

        const float threshold_wh = 0.80f * ctx->vars.battery_capacity_wh; // 48000
        CHECK_THAT(ctx->vars.battery_charge_wh, WithinAbs(threshold_wh, kTol));
        CHECK(ctx->vars.battery_charge_wh <= threshold_wh + kTol);
        CHECK_THAT(ctx->vars.soc_pct, WithinAbs(80.0f, 1e-2f));
        CHECK(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
    }

    SECTION("Edge re-arms after SoC drops below threshold") {
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // Already full, was_full latched.
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 5000.0f;
        ctx->vars.soc_pct = 91.667f; // forced below 100 for the test
        ctx->vars.was_full = true;

        // Switch to discharge to drop SoC below threshold this tick.
        ctx->vars.dc_present_current_a = -125.0f;
        soc_step(*ctx);

        // Edge cleared.
        CHECK_FALSE(ctx->vars.was_full);
        CHECK(fx.timer.enqueued_events.empty());

        // Now charge again — cross threshold from below.
        ctx->vars.dc_present_current_a = 125.0f;
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f;
        ctx->vars.soc_pct = 99.983f;
        fx.timer.enqueued_events.clear();

        soc_step(*ctx);

        CHECK(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
        CHECK(ctx->vars.was_full);
    }

    SECTION("stop_session: rising edge fires a second time after session-end reset") {
        // Latch must clear on session-end so a fresh session with SoC already
        // at threshold re-fires the StopSession edge. Without the reset, the
        // second session inherits was_full=true and the policy is inert until
        // SoC physically dips below threshold during the new session.
        TestFixture fx;
        fx.cfg.dc_target_current = 125;
        fx.cfg.dc_target_voltage = 400;
        fx.cfg.tick_interval_ms = 1000;
        fx.cfg.on_battery_full = "stop_session";
        auto ctx = fx.make_ctx();
        set_mode(*ctx, api::ChargeMode::DcIso2);
        // EvInfo passthrough delivers the full DC current this tick.
        ctx->vars.dc_present_current_a = 125.0f;
        // Session 1: start just below capacity and cross the threshold.
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f;
        ctx->vars.was_full = false;

        soc_step(*ctx);

        REQUIRE(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
        REQUIRE(ctx->vars.was_full);

        // Session end: Unplugged::enter is the canonical reset path. After it
        // runs, was_full must be cleared so the rising edge can fire again.
        Unplugged unplugged{*ctx};
        unplugged.enter();
        REQUIRE_FALSE(ctx->vars.was_full);

        // Session 2: re-enter charging context, prime SoC just below capacity
        // again, clear the recorded events, and tick. The rising edge must
        // fire a second StopSession.
        set_mode(*ctx, api::ChargeMode::DcIso2);
        ctx->vars.battery_charge_wh = ctx->vars.battery_capacity_wh - 10.0f;
        ctx->vars.soc_pct = 99.983f;
        fx.timer.enqueued_events.clear();

        soc_step(*ctx);

        CHECK(event_kind_enqueued(fx.timer.enqueued_events, module::EventKind::StopSession));
        CHECK(ctx->vars.was_full);
    }
}
