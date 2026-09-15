// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_ISO15118_EV_IMPL_HPP
#define EV_ISO15118_EV_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/ISO15118_ev/Implementation.hpp>

#include "../Ev15118.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include <everest/util/async/monitor.hpp>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/ev/session_params.hpp>
#include <iso15118/message/shared_datatypes.hpp>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace ev {

struct Conf {};

class ISO15118_evImpl : public ISO15118_evImplBase {
public:
    ISO15118_evImpl() = delete;
    ISO15118_evImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<Ev15118>& mod, Conf& config) :
        ISO15118_evImplBase(ev, "ev"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    ~ISO15118_evImpl();
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual bool handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                       types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                       double& DepartureTime, double& EAmount) override;
    virtual void handle_stop_charging() override;
    virtual void handle_pause_charging() override;
    virtual void handle_abort_charging() override;
    virtual void handle_cp_state_changed(types::iso15118::CpState& cp_state) override;
    virtual void handle_set_fault() override;
    virtual void handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) override;
    virtual void handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) override;
    virtual void handle_enable_sae_j2847_v2g_v2h() override;
    virtual void handle_update_soc(double& SoC) override;
    virtual void handle_update_present_values(types::iso15118::EvPresentValues& PresentValues) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<Ev15118>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    enum class SessionPhase {
        idle,
        requested,
        running
    };

    struct SessionState {
        SessionPhase phase{SessionPhase::idle};
        bool shutting_down{false};
        // A cancelled request owes a v2g_session_finished; the worker publishes it.
        bool finish_pending{false};
        iso15118::ev::Controller* current{nullptr};
        iso15118::ev::DcChargeParams dc_params;
        // From set_bpt_dc_params; overlay the config values at start_charging.
        std::optional<float> cmd_max_discharge_power;
        std::optional<float> cmd_max_discharge_current;
        iso15118::ev::AcChargeParams ac_params;
        iso15118::message_20::datatypes::ServiceCategory energy_service{
            iso15118::message_20::datatypes::ServiceCategory::DC};
        // ISO 15118-2 / DIN granularity of the requested EnergyTransferMode.
        iso15118::shared_datatypes::EnergyTransferMode iso2_transfer_mode{
            iso15118::shared_datatypes::EnergyTransferMode::DC_extended};
        // Payment option of the last start_charging: ExternalPayment explicitly asked for
        // (overrides the enable_pnc Contract preference), and Contract asked for even when
        // the SECC does not offer it.
        bool eim_requested{false};
        bool enforce_contract{false};
        // Latched cp_state_changed report; replayed to every new Controller. Its presence
        // is what tells the stack the control pilot is reported at all.
        std::optional<bool> cp_c_or_d;
        // Left by the previous session when it ended with SessionStop(Pause); handed to the
        // next Controller as EvConfig::resume.
        std::optional<iso15118::ev::PausedSession> paused;
    };

    // Graceful end requested by the EV, applied to the running Controller.
    using ControllerAction = void (iso15118::ev::Controller::*)();

    everest::lib::util::monitor<SessionState> session;
    std::thread worker;

    // HLC interface resolved once in ready(); "auto" is replaced by the picked device.
    std::string hlc_device;
    // MAC of hlc_device, the ISO 15118-2 / DIN EVCCID. Empty keeps the library default.
    std::optional<std::array<uint8_t, 6>> evcc_mac;
    // make_ev_config is const and runs per session; warn about PnC-without-TLS only once.
    mutable bool pnc_without_tls_warned{false};
    // PnC material read once in ready(); copied per session. Static config, so no file IO
    // ever runs under the session monitor.
    iso15118::ev::d2::PnCConfig pnc_material;
    // False when ready() bailed out before starting the worker; start_charging then fails.
    bool worker_started{false};

    void session_worker();
    void run_one_session();
    // Runs `action` on the live Controller, or cancels a session that was requested but not
    // yet constructed. drop_paused discards a stored paused session (stop and abort end it).
    void end_session(ControllerAction action, bool drop_paused);
    // Caller holds the session lock; reads energy_service, paused and cp_c_or_d from `state`.
    iso15118::ev::EvConfig make_ev_config(const SessionState& state) const;
    iso15118::ev::feedback::Callbacks make_callbacks();
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace ev
} // namespace module

#endif // EV_ISO15118_EV_IMPL_HPP
