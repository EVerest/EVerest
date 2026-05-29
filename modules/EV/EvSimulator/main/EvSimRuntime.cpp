// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvSimRuntime.hpp"

#include "CommandRouter.hpp"
#include "PeerSubscriptions.hpp"
#include "SocIntegrator.hpp"
#include "states/Disabled.hpp"

#include <everest/logging.hpp>

#include <utility>
#include <variant>

namespace module {

EvSimRuntime::EvSimRuntime(EvSimulator& mod_) : mod(mod_) {
    PeerHandles peers{
        mod.r_ev_board_support.get(),
        mod.r_ev.empty() ? nullptr : mod.r_ev[0].get(),
        mod.r_slac.empty() ? nullptr : mod.r_slac[0].get(),
        mod.r_kvs.empty() ? nullptr : mod.r_kvs[0].get(),
    };
    auto actions = build_peer_actions();

    auto publisher = [this](const std::string& topic, const std::string& payload) { mod.mqtt.publish(topic, payload); };
    auto timer_arm = [this](std::chrono::milliseconds ms) {
        state_timer_fd.set_timeout_ms(static_cast<long long>(ms.count()));
    };
    // timer_fd has no explicit disarm(); `it_value == 0` cancels per timerfd_settime(2).
    auto timer_cancel = [this]() { state_timer_fd.set_timeout_ms(0); };
    auto tick_arm_fn = [this](int ms) { arm_tick(ms); };
    auto tick_disarm_fn = [this]() { disarm_tick(); };
    auto scenario_enqueue = [this](Event ev) { enqueue(std::move(ev)); };
    auto scenario_timer_arm = [this](std::chrono::seconds s) { arm_scenario_timer(s); };

    ctx = std::make_unique<FsmContext>(peers, std::move(actions), std::move(publisher), std::move(timer_arm),
                                       std::move(timer_cancel), std::move(tick_arm_fn), std::move(tick_disarm_fn),
                                       std::move(scenario_enqueue), std::move(scenario_timer_arm), mod.config,
                                       mod.get_topics());
}

EvSimRuntime::~EvSimRuntime() = default;

PeerActions EvSimRuntime::build_peer_actions() {
    PeerActions a;

    // BSP — always present (ev_board_support requirement is required, min_connections=1).
    auto* bsp = mod.r_ev_board_support.get();
    a.bsp_set_cp = [bsp](::types::ev_board_support::EvCpState s) { bsp->call_set_cp_state(s); };
    a.bsp_allow_power_on = [bsp](bool on) { bsp->call_allow_power_on(on); };
    a.bsp_set_ac_max_current = [bsp](float c) { bsp->call_set_ac_max_current(static_cast<double>(c)); };
    a.bsp_set_three_phases = [bsp](bool p) { bsp->call_set_three_phases(p); };
    a.bsp_diode_fail = [bsp](bool b) { bsp->call_diode_fail(b); };
    a.bsp_set_rcd_error = [bsp](float mA) { bsp->call_set_rcd_error(static_cast<double>(mA)); };

    // ISO — optional.
    if (!mod.r_ev.empty()) {
        auto* iso = mod.r_ev[0].get();
        a.iso_start_charging = [iso](::types::iso15118::EnergyTransferMode etm, ::types::iso15118::PaymentOption pay,
                                     int32_t departure_time_s, int32_t e_amount_wh, bool force_payment_option) {
            ::types::iso15118::SelectedPaymentOption selected{};
            selected.payment_option = pay;
            selected.enforce_payment_option = force_payment_option;
            return iso->call_start_charging(etm, selected, static_cast<double>(departure_time_s),
                                            static_cast<double>(e_amount_wh));
        };
        a.iso_stop_charging = [iso]() { iso->call_stop_charging(); };
        a.iso_pause_charging = [iso]() { iso->call_pause_charging(); };
        a.iso_update_soc = [iso](float pct) { iso->call_update_soc(static_cast<double>(pct)); };
    }

    // SLAC — optional.
    if (!mod.r_slac.empty()) {
        auto* slac = mod.r_slac[0].get();
        a.slac_trigger_matching = [slac]() { return slac->call_trigger_matching(); };
    }

    // KVS — optional. The generated `call_store` takes the variant directly;
    // we always store the serialized JSON payload as a string alternative.
    if (!mod.r_kvs.empty()) {
        auto* kvs = mod.r_kvs[0].get();
        a.kvs_store = [kvs](const std::string& key, const std::string& json) {
            std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string> value = json;
            kvs->call_store(key, value);
        };
        a.kvs_load_raw = [kvs](const std::string& key) -> std::string {
            auto value = kvs->call_load(key);
            if (std::holds_alternative<std::string>(value)) {
                return std::get<std::string>(value);
            }
            return {};
        };
    }

    // Internal publishes — always (p_ev_manager always present).
    auto* p = mod.p_ev_manager.get();
    a.publish_internal_bsp_event = [p](const ::types::board_support_common::BspEvent& e) { p->publish_bsp_event(e); };
    a.publish_internal_ev_info = [p](const ::types::evse_manager::EVInfo& info) { p->publish_ev_info(info); };

    return a;
}

void EvSimRuntime::run(std::atomic_bool& online) {
    auto wake_handler = [this]() { on_wake(); };
    auto state_timer_handler = [this]() { on_state_timer(); };
    auto tick_handler = [this]() { on_tick(); };
    auto scenario_handler = [this]() { on_scenario_timer(); };

    loop.register_event_handler(&wake_fd, wake_handler);
    loop.register_event_handler(&state_timer_fd, state_timer_handler);
    loop.register_event_handler(&tick_fd, tick_handler);
    loop.register_event_handler(&scenario_timer_fd, scenario_handler);

    fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Disabled>(*ctx));

    loop.run(online);
}

void EvSimRuntime::wake() {
    wake_fd.notify();
}

void EvSimRuntime::enqueue(Event ev) {
    queue.push(std::move(ev));
    wake();
}

void EvSimRuntime::on_wake() {
    // Drain all queued events. fd_event_handler already read() the wake_fd
    // for us before invoking this handler.
    while (auto e = queue.try_pop()) {
        apply_passthrough_vars(*e);
        if (fsm) {
            fsm->feed(*e);
        }
        publish_passthrough_external(*e);
    }
}

void EvSimRuntime::on_state_timer() {
    if (fsm) {
        Event ev{};
        ev.kind = EventKind::StateDeadline;
        fsm->feed(ev);
    }
}

void EvSimRuntime::on_tick() {
    if (ctx) {
        SocIntegrator::step(*ctx);
    }
}

void EvSimRuntime::on_scenario_timer() {
    Event ev{};
    ev.kind = EventKind::StopSession;
    enqueue(std::move(ev));
}

void EvSimRuntime::arm_tick(int ms) {
    tick_fd.set_timeout_ms(static_cast<long long>(ms));
}

void EvSimRuntime::disarm_tick() {
    tick_fd.set_timeout_ms(0);
}

void EvSimRuntime::arm_scenario_timer(std::chrono::seconds s) {
    scenario_timer_fd.set_timeout_ms(static_cast<long long>(s.count()) * 1000);
}

void EvSimRuntime::disarm_scenario_timer() {
    scenario_timer_fd.set_timeout_ms(0);
}

void EvSimRuntime::apply_passthrough_vars(const Event& ev) {
    using K = EventKind;
    switch (ev.kind) {
    case K::EvInfo:
        if (auto* p = std::get_if<EvInfoPayload>(&ev.payload)) {
            if (p->ev_info.soc) {
                ctx->vars.soc_pct = *p->ev_info.soc;
            }
        }
        break;
    case K::SlacState:
        if (auto* p = std::get_if<SlacStatePayload>(&ev.payload)) {
            ctx->vars.slac_state = p->state;
        }
        break;
    case K::BspMeasurement:
        if (auto* p = std::get_if<BspMeasurementPayload>(&ev.payload)) {
            ctx->vars.pwm_duty_cycle = p->cp_pwm_duty_cycle;
        }
        break;
    default:
        break;
    }
}

void EvSimRuntime::publish_passthrough_external(const Event& ev) {
    using K = EventKind;
    switch (ev.kind) {
    case K::BspEvent:
        if (auto* p = std::get_if<BspEventPayload>(&ev.payload)) {
            ctx->publish_internal_bsp_event(p->bsp_event);
            ctx->publish_e2m_bsp_event(p->bsp_event);
        }
        break;
    case K::EvInfo:
        ctx->publish_internal_ev_info();
        ctx->publish_e2m_ev_info();
        break;
    case K::SlacState:
        ctx->publish_e2m_slac_state();
        break;
    case K::BspMeasurement:
        if (mod.config.publish_bsp_measurements) {
            if (auto* p = std::get_if<BspMeasurementPayload>(&ev.payload)) {
                ctx->publish_e2m_bsp_measurement(*p);
            }
        }
        break;
    default:
        break;
    }
}

void EvSimRuntime::register_m2e_subscriptions() {
    setup_command_router(*this, mod);
}

void EvSimRuntime::register_peer_subscriptions() {
    setup_peer_subscriptions(*this, mod);
}

} // namespace module
