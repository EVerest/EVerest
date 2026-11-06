// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "Events.hpp"
#include "IsoStart.hpp"

#include <everest/iso15118_ev_session/session_setup.hpp>

#include <everest_api_types/ev_controller/API.hpp>
#include <generated/types/iso15118.hpp>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/ev/d20/der/sae_profile.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/session/protocol.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace module {

// The EV side of ISO 15118, run in-process on the libiso15118 ev::Controller and set up by the
// library code Ev15118 also uses (everest::lib::iso15118_ev).
//
// Every member function runs on the loop thread, so the session state needs no lock. A start
// builds the Controller there and runs its loop() on a worker thread; the Controller's callbacks
// enqueue Events tagged with the session that produced them, and the worker's last act is that
// session's IsoV2GFinished. The loop admits a tagged event only while its session is live (admit),
// so an event of an ended or replaced session never reaches the FSM.
class IsoSession {
public:
    struct Settings {
        // enable_pnc and the -2 AC EAmount are set per session from the start; ac_phase_count stays
        // 3, and a single-phase start asks for one phase.
        everest::lib::iso15118_ev::VehicleConfig vehicle;
        // The HLC interface; "auto" is resolved at each start.
        std::string device;
        std::string supported_protocols;
        // Empty selects the built-in profile.
        std::string sae_inverter_profile_path;
    };
    using Enqueue = std::function<void(Event)>;

    // Throws Everest::EverestConfigError on a supported_protocols list that names no protocol, an
    // unknown one, or one twice, and on a SAE inverter profile that does not load.
    IsoSession(Settings settings, Enqueue enqueue);
    // Throws where the constructor does, and has no other effect.
    static void check(const Settings& settings);
    ~IsoSession();

    IsoSession(const IsoSession&) = delete;
    IsoSession& operator=(const IsoSession&) = delete;

    // Why a session of `mode` cannot run with the configured protocols, or std::nullopt when it can.
    std::optional<std::string> cannot_run(everest::lib::API::V1_0::types::ev_controller::ChargeMode mode) const;

    // False when the request cannot run or is invalid, a session still runs, or the Controller
    // cannot be built.
    bool start_charging(const IsoStart& start);
    void stop_charging();
    void pause_charging();
    // Ends the session without SessionStop.
    void abort_charging();
    void cp_state_changed(::types::iso15118::CpState cp_state);
    // Partial: omitted fields keep their value. A merged set that does not validate changes nothing.
    void set_dc_params(const ::types::iso15118::DcEvParameters& params);
    void update_soc(float soc_pct);
    void update_present_values(const ::types::iso15118::EvPresentValues& values);

    // True when `session` is the live session. `ended` takes its finish: the worker is joined, a
    // pause is kept for the next start, and the session is no longer live.
    bool admit(std::uint32_t session, bool ended);

private:
    struct Live {
        std::uint32_t id{0};
        std::unique_ptr<iso15118::ev::Controller> controller;
        std::thread worker;
        std::atomic_bool loop_returned{false};
        // Written by the worker before loop_returned; read after the join.
        bool failed{false};
        // A contract the session installed. Written by the worker; read after the join.
        std::optional<iso15118::ev::d2::ContractMaterial> installed_contract;
        // A stop or abort reached it; its pause is not kept.
        bool discard_pause{false};
    };

    // The protocols a session of `mode` offers, in SAP priority order.
    std::vector<iso15118::ProtocolId>
    protocols_for(everest::lib::API::V1_0::types::ev_controller::ChargeMode mode) const;
    iso15118::ev::feedback::Callbacks make_callbacks(Live& session);
    void release();
    void end(void (iso15118::ev::Controller::*action)());

    const Settings settings;
    const std::vector<iso15118::ProtocolId> protocols;
    const iso15118::ev::d20::der::SaeInverterProfile sae_profile;
    const Enqueue enqueue;

    iso15118::ev::params::DcChargeParams dc_params;
    iso15118::ev::params::AcChargeParams ac_params;
    // Latched; replayed to each new Controller, whose CP-gated checks it enables.
    std::optional<bool> cp_c_or_d;
    // Left by a session that ended with SessionStop(Pause); the next start resumes it.
    std::optional<iso15118::ev::config::PausedSession> paused;
    // The contract a paused session installed; its resume presents it.
    std::optional<iso15118::ev::d2::ContractMaterial> installed_contract;
    bool pnc_without_tls_warned{false};
    std::uint32_t last_id{0};
    std::unique_ptr<Live> live;
};

} // namespace module
