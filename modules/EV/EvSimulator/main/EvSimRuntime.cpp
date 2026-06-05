// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvSimRuntime.hpp"

#include "CommandRouter.hpp"
#include "EventDispatch.hpp"
#include "PeerSubscriptions.hpp"
#include "RampInterpolator.hpp"
#include "SocIntegrator.hpp"
#include "states/Disabled.hpp"

#include <everest/logging.hpp>

#include <cerrno>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace module {

namespace {
// Cancel the next fire and consume any fire already pending on the fd.
// timerfd is non-blocking (TFD_NONBLOCK): read() returns the fire count
// or -1/EAGAIN when nothing is queued. Without this flush, a fire that
// became readable before set_timeout_ms(0) will still wake epoll and
// dispatch the handler against whatever state is now current.
void disarm_and_flush(everest::lib::io::event::timer_fd& fd) {
    fd.set_timeout_ms(0);
    // timer_fd::read() forwards ::read()'s return directly, so errno is valid
    // immediately after (no intervening syscall). A negative return with
    // EAGAIN/EWOULDBLOCK is the expected "nothing pending" case; any other
    // errno means a genuinely broken fd that would otherwise look identical
    // to an empty flush.
    errno = 0;
    if (fd.read() < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        EVLOG_error << "EvSimulator: timer_fd flush read failed: errno=" << errno;
    }
}
} // namespace

EvSimRuntime::EvSimRuntime(EvSimulator& mod_) : mod(mod_) {
    auto actions = build_peer_actions();

    // MqttProvider::publish() is void / fire-and-forget — there is no
    // delivery result to inspect. Contain any exception so a publish throw
    // from inside on_wake's flush loop cannot escape and kill the loop
    // thread; the failed topic is logged rather than silently dropped.
    auto publisher = [this](const std::string& topic, const std::string& payload) {
        try {
            mod.mqtt.publish(topic, payload);
        } catch (const std::exception& e) {
            EVLOG_error << "EvSimulator: publish to '" << topic << "' failed: " << e.what();
        } catch (...) {
            EVLOG_error << "EvSimulator: publish to '" << topic << "' failed: unknown exception";
        }
    };
    auto timer_arm = [this](std::chrono::milliseconds ms) {
        state_timer_fd.set_timeout_ms(static_cast<long long>(ms.count()));
    };
    // timer_fd has no explicit disarm(); `it_value == 0` cancels per timerfd_settime(2).
    // Flush after cancel so a fire already in epoll's ready set cannot dispatch on_state_timer
    // against an unrelated state.
    auto timer_cancel = [this]() { disarm_and_flush(state_timer_fd); };
    auto tick_arm_fn = [this](int ms) { arm_tick(ms); };
    auto tick_disarm_fn = [this]() { disarm_tick(); };
    auto scenario_enqueue = [this](Event ev) { enqueue(std::move(ev)); };
    auto scenario_timer_arm = [this](std::chrono::milliseconds ms) { arm_scenario_timer(ms); };

    ctx = std::make_unique<FsmContext>(std::move(actions), std::move(publisher), std::move(timer_arm),
                                       std::move(timer_cancel), std::move(tick_arm_fn), std::move(tick_disarm_fn),
                                       std::move(scenario_enqueue), std::move(scenario_timer_arm), mod.config,
                                       mod.get_topics());
}

EvSimRuntime::~EvSimRuntime() {
    // Detach MQTT m2e command-router subscriptions first: the captured
    // lambdas hold `[&rt]` references into this runtime, and a message
    // delivered after the queue / ctx / fsm members are destroyed would be
    // use-after-free. Invoke every token before anything else tears down.
    for (auto& token : command_router_tokens) {
        if (token) {
            try {
                token();
            } catch (const std::exception& ex) {
                EVLOG_warning << "EvSimulator: exception while detaching m2e subscription: " << ex.what();
            } catch (...) {
                EVLOG_warning << "EvSimulator: unknown exception while detaching m2e subscription";
            }
        }
    }
    command_router_tokens.clear();
}

PeerActions EvSimRuntime::build_peer_actions() {
    PeerActions a;

    // BSP — always present (ev_board_support requirement is required, min_connections=1).
    // `present` is set in the same block that wires the functions so presence
    // implies every BSP function is set.
    auto* bsp = mod.r_ev_board_support.get();
    a.bsp.set_cp = [bsp](::types::ev_board_support::EvCpState s) { bsp->call_set_cp_state(s); };
    a.bsp.allow_power_on = [bsp](bool on) { bsp->call_allow_power_on(on); };
    a.bsp.set_ac_max_current = [bsp](float c) { bsp->call_set_ac_max_current(static_cast<double>(c)); };
    a.bsp.set_three_phases = [bsp](bool p) { bsp->call_set_three_phases(p); };
    a.bsp.diode_fail = [bsp](bool b) { bsp->call_diode_fail(b); };
    a.bsp.set_rcd_error = [bsp](float mA) { bsp->call_set_rcd_error(static_cast<double>(mA)); };
    a.bsp.enable = [bsp](bool on) { bsp->call_enable(on); };
    a.bsp.present = true;

    // ISO — optional. All seven functions and `present` are set in this block,
    // never separately, so `iso.present` IFF the whole peer is wired.
    if (!mod.r_ev.empty()) {
        auto* iso = mod.r_ev[0].get();
        a.iso.start_charging = [iso](::types::iso15118::EnergyTransferMode etm, ::types::iso15118::PaymentOption pay,
                                     int32_t departure_time_s, int32_t e_amount_wh, bool force_payment_option) {
            ::types::iso15118::SelectedPaymentOption selected{};
            selected.payment_option = pay;
            selected.enforce_payment_option = force_payment_option;
            return iso->call_start_charging(etm, selected, static_cast<double>(departure_time_s),
                                            static_cast<double>(e_amount_wh));
        };
        a.iso.stop_charging = [iso]() { iso->call_stop_charging(); };
        a.iso.pause_charging = [iso]() { iso->call_pause_charging(); };
        a.iso.update_soc = [iso](float pct) { iso->call_update_soc(static_cast<double>(pct)); };
        a.iso.enable_sae_j2847_v2g_v2h = [iso]() { iso->call_enable_sae_j2847_v2g_v2h(); };
        a.iso.set_bpt_dc_params = [iso](const ::types::iso15118::DcEvBPTParameters& params) {
            iso->call_set_bpt_dc_params(params);
        };
        a.iso.set_dc_params = [iso](const ::types::iso15118::DcEvParameters& params) {
            iso->call_set_dc_params(params);
        };
        a.iso.present = true;
    }

    // SLAC — optional.
    if (!mod.r_slac.empty()) {
        auto* slac = mod.r_slac[0].get();
        a.slac.trigger_matching = [slac]() { return slac->call_trigger_matching(); };
        a.slac.present = true;
    }

    // KVS — optional. The generated `call_store` takes the variant directly;
    // we always store the serialized JSON payload as a string alternative.
    if (!mod.r_kvs.empty()) {
        auto* kvs = mod.r_kvs[0].get();
        a.kvs.store = [kvs](const std::string& key, const std::string& json) {
            std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string> value = json;
            kvs->call_store(key, value);
        };
        a.kvs.load_raw = [kvs](const std::string& key) -> std::optional<std::string> {
            auto value = kvs->call_load(key);
            if (std::holds_alternative<std::string>(value)) {
                return std::get<std::string>(value);
            }
            return std::nullopt;
        };
        a.kvs.present = true;
    }

    // Internal publishes — always (p_ev_manager always present).
    auto* p = mod.p_ev_manager.get();
    a.publisher.bsp_event = [p](const ::types::board_support_common::BspEvent& e) { p->publish_bsp_event(e); };
    a.publisher.ev_info = [p](const ::types::evse_manager::EVInfo& info) { p->publish_ev_info(info); };
    a.publisher.present = true;

    // Out-of-band error raise/clear — always (p_ev_manager always present).
    // Driven only from the loop thread when a RaiseErrorCmd / ClearErrorCmd is
    // flushed, so it cannot race Faulted::enter's fault publishing.
    a.error.raise = [p](const std::string& type, const std::string& sub_type, const std::string& message,
                        Everest::error::Severity severity) {
        auto err = p->error_factory->create_error(type, sub_type, message, severity);
        p->raise_error(err);
    };
    a.error.clear = [p](const std::string& type, const std::optional<std::string>& sub_type) {
        if (sub_type) {
            p->clear_error(type, *sub_type);
        } else {
            p->clear_error(type);
        }
    };
    a.error.is_active = [p](const std::string& type, const std::string& sub_type) -> bool {
        const auto monitor = p->error_state_monitor;
        return monitor && monitor->is_error_active(type, sub_type);
    };
    a.error.present = true;

    return a;
}

void EvSimRuntime::run(std::atomic_bool& online) {
    // register_event_handler returns false when the underlying epoll_ctl(ADD) fails
    // (EBADF, EEXIST, ENOMEM, EPERM). A discarded failure leaves the loop running
    // without that fd delivering events, so the module sits idle silently. Abort
    // startup so the framework surfaces the failure.
    auto register_or_throw = [this](auto* fd, const auto& handler, const char* name) {
        if (!loop.register_event_handler(fd, handler)) {
            EVLOG_error << "EvSimulator: failed to register " << name << " with event loop";
            throw std::runtime_error(std::string{"EvSimulator: failed to register "} + name + " with event loop");
        }
    };

    register_or_throw(
        &wake_fd, [this]() { on_wake(); }, "wake_fd");
    register_or_throw(
        &state_timer_fd, [this]() { on_state_timer(); }, "state_timer_fd");
    register_or_throw(
        &tick_fd, [this]() { on_tick(); }, "tick_fd");
    register_or_throw(
        &scenario_timer_fd, [this]() { on_scenario_timer(); }, "scenario_timer_fd");

    fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Disabled>(*ctx));

    loop.run(online);
}

void EvSimRuntime::wake() {
    // notify() bumps the wake_fd eventfd counter; on failure the loop is
    // never told to flush and the just-enqueued event is stranded until the
    // next unrelated wake. Surface it instead of losing the command silently.
    if (!wake_fd.notify()) {
        EVLOG_error << "EvSimulator: wake_fd notify failed; a queued event may not be flushed promptly";
    }
}

void EvSimRuntime::enqueue(Event ev) {
    queue.push(std::move(ev));
    wake();
}

void EvSimRuntime::on_wake() {
    // Flush all queued events. fd_event_handler already read() the wake_fd
    // for us before invoking this handler. Each step is exception-isolated:
    // a throw in one handler must not abandon the rest of the queue or kill
    // the loop thread silently.
    while (auto e = queue.try_pop()) {
        // raise_error / clear_error are out-of-band meta-commands: they touch
        // p_ev_manager error state, not the FSM. Handle them here on the loop
        // thread (which owns all peer/error interaction) so they cannot
        // interleave with Faulted::enter's fault publishing, then skip the
        // FSM feed entirely.
        if (auto* rc = std::get_if<RaiseErrorCmd>(&e->payload)) {
            try {
                ctx->raise_error(*rc);
            } catch (const std::exception& ex) {
                EVLOG_error << "EvSimulator: exception in raise_error: " << ex.what();
            } catch (...) {
                EVLOG_error << "EvSimulator: unknown exception in raise_error";
            }
            continue;
        }
        if (auto* cc = std::get_if<ClearErrorCmd>(&e->payload)) {
            try {
                ctx->clear_error(*cc);
            } catch (const std::exception& ex) {
                EVLOG_error << "EvSimulator: exception in clear_error: " << ex.what();
            } catch (...) {
                EVLOG_error << "EvSimulator: unknown exception in clear_error";
            }
            continue;
        }
        // configure_session is latched into ctx (consumed at the next plug),
        // not fed to the FSM. Same pre-FSM seam as raise/clear_error: handled
        // on the loop thread which owns all peer/ack publishing.
        if (auto* sp = std::get_if<API_types::ev_simulator::SessionConfigParams>(&e->payload)) {
            try {
                ctx->configure_session(*sp);
            } catch (const std::exception& ex) {
                EVLOG_error << "EvSimulator: exception in configure_session: " << ex.what();
            } catch (...) {
                EVLOG_error << "EvSimulator: unknown exception in configure_session";
            }
            continue;
        }
        try {
            apply_passthrough_vars(*e);
        } catch (const std::exception& ex) {
            EVLOG_error << "EvSimulator: exception in apply_passthrough_vars kind=" << static_cast<int>(kind_of(*e))
                        << ": " << ex.what();
        } catch (...) {
            EVLOG_error << "EvSimulator: unknown exception in apply_passthrough_vars kind="
                        << static_cast<int>(kind_of(*e));
        }
        if (fsm) {
            feed_with_fault_isolation(fsm, *ctx, *e);
        }
        try {
            publish_passthrough_external(*e);
        } catch (const std::exception& ex) {
            EVLOG_error << "EvSimulator: exception in publish_passthrough_external kind="
                        << static_cast<int>(kind_of(*e)) << ": " << ex.what();
        } catch (...) {
            EVLOG_error << "EvSimulator: unknown exception in publish_passthrough_external kind="
                        << static_cast<int>(kind_of(*e));
        }
    }
}

void EvSimRuntime::on_state_timer() {
    if (fsm) {
        Event ev{StateDeadlineEvt{}};
        feed_with_fault_isolation(fsm, *ctx, ev);
    }
}

void EvSimRuntime::on_tick() {
    if (!ctx) {
        return;
    }
    try {
        ramp_step(*ctx, std::chrono::steady_clock::now());
        soc_step(*ctx);
    } catch (const std::exception& ex) {
        EVLOG_error << "EvSimulator: exception in on_tick: " << ex.what();
    } catch (...) {
        EVLOG_error << "EvSimulator: unknown exception in on_tick";
    }
}

void EvSimRuntime::on_scenario_timer() {
    if (!ctx) {
        return;
    }
    try {
        ctx->scenario.on_timer_fire(*ctx);
    } catch (const std::exception& ex) {
        EVLOG_error << "EvSimulator: exception in on_scenario_timer: " << ex.what();
    } catch (...) {
        EVLOG_error << "EvSimulator: unknown exception in on_scenario_timer";
    }
}

void EvSimRuntime::arm_tick(int ms) {
    tick_fd.set_timeout_ms(static_cast<long long>(ms));
}

void EvSimRuntime::disarm_tick() {
    disarm_and_flush(tick_fd);
}

void EvSimRuntime::arm_scenario_timer(std::chrono::milliseconds ms) {
    scenario_timer_fd.set_timeout_ms(ms.count());
}

void EvSimRuntime::apply_passthrough_vars(const Event& ev) {
    using K = EventKind;
    switch (kind_of(ev)) {
    case K::EvInfo:
        if (auto* p = std::get_if<EvInfoPayload>(&ev.payload)) {
            if (p->ev_info.soc) {
                ctx->vars.soc_pct = *p->ev_info.soc;
            }
            if (p->ev_info.present_current) {
                ctx->vars.evse_dc_present_current_a = static_cast<float>(*p->ev_info.present_current);
            }
            if (p->ev_info.present_voltage) {
                ctx->vars.dc_present_voltage_v = static_cast<float>(*p->ev_info.present_voltage);
            }
        }
        break;
    case K::SlacState:
        if (auto* p = std::get_if<SlacStatePayload>(&ev.payload)) {
            ctx->vars.slac_state = p->state;
            // A SLAC UNMATCHED is the SECC's D-LINK_TERMINATE: the link was torn
            // down (AC pause does this; DC keeps the link). Latch it so the
            // EV-initiated resume path re-matches SLAC. Cleared by SlacMatching
            // on the next MATCHED.
            if (p->state == "UNMATCHED") {
                ctx->vars.slac_unmatched = true;
            }
        }
        break;
    case K::BspMeasurement:
        if (auto* p = std::get_if<BspMeasurementPayload>(&ev.payload)) {
            ctx->vars.pwm_duty_cycle = p->cp_pwm_duty_cycle;
        }
        break;
    case K::DcEvsePresentCurrent:
        if (auto* p = std::get_if<DcEvsePresentCurrentPayload>(&ev.payload)) {
            ctx->vars.evse_dc_present_current_a = static_cast<float>(p->current_a);
        }
        break;
    case K::DcEvsePresentVoltage:
        if (auto* p = std::get_if<DcEvsePresentVoltagePayload>(&ev.payload)) {
            ctx->vars.dc_present_voltage_v = static_cast<float>(p->voltage_v);
        }
        break;
    case K::IsoV2GFinished:
        // The Josev V2G comm session has fully torn down. Clear the live-session
        // flag before the FSM feed so Paused::feed can release a deferred resume
        // (the SECC is now safely paused and ready for the BCB wake-up).
        ctx->vars.iso_session_active = false;
        break;
    default:
        break;
    }
}

void EvSimRuntime::publish_passthrough_external(const Event& ev) {
    using K = EventKind;
    switch (kind_of(ev)) {
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
    auto tokens = setup_command_router(*this, mod);
    command_router_tokens.insert(command_router_tokens.end(), std::make_move_iterator(tokens.begin()),
                                 std::make_move_iterator(tokens.end()));
}

void EvSimRuntime::register_peer_subscriptions() {
    setup_peer_subscriptions(*this, mod);
}

} // namespace module
