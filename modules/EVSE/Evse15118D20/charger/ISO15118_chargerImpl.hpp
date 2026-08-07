// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef CHARGER_ISO15118_CHARGER_IMPL_HPP
#define CHARGER_ISO15118_CHARGER_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/ISO15118_charger/Implementation.hpp>

#include "../Evse15118D20.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <atomic>
#include <bitset>
#include <mutex>
#include <optional>

#include <iso15118/message/v2g_message_type.hpp>

#include "der_relay.hpp"
#include "grid_event.hpp"
#include "utils.hpp"

#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/tbd_controller.hpp>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace charger {

struct Conf {};

class ISO15118_chargerImpl : public ISO15118_chargerImplBase {
public:
    ISO15118_chargerImpl() = delete;
    ISO15118_chargerImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<Evse15118D20>& mod, Conf& config) :
        ISO15118_chargerImplBase(ev, "charger"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual void handle_setup(types::iso15118::EVSEID& evse_id, types::iso15118::SaeJ2847BidiMode& sae_j2847_mode,
                              bool& debug_mode) override;
    virtual void handle_set_charging_parameters(types::iso15118::SetupPhysicalValues& physical_values) override;
    virtual void handle_session_setup(std::vector<types::iso15118::PaymentOption>& payment_options,
                                      bool& supported_certificate_service,
                                      bool& central_contract_validation_allowed) override;
    virtual void handle_bpt_setup(types::iso15118::BptSetup& bpt_config) override;
    virtual void handle_set_powersupply_capabilities(types::power_supply_DC::Capabilities& capabilities) override;
    virtual void handle_authorization_response(types::authorization::AuthorizationStatus& authorization_status,
                                               types::authorization::CertificateStatus& certificate_status) override;
    virtual void handle_ac_contactor_closed(bool& status) override;
    virtual void handle_dlink_ready(bool& value) override;
    virtual void handle_cable_check_finished(bool& status) override;
    virtual void handle_receipt_is_required(bool& receipt_required) override;
    virtual void handle_stop_charging(bool& stop) override;
    virtual void handle_pause_charging(bool& pause) override;
    virtual void handle_no_energy_pause_charging(types::iso15118::NoEnergyPauseMode& mode) override;
    virtual bool
    handle_update_supported_app_protocols(types::iso15118::SupportedAppProtocols& supported_app_protocols) override;
    virtual void handle_update_energy_transfer_modes(
        std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) override;
    virtual void handle_update_ac_max_current(double& max_current) override;
    virtual void handle_update_ac_parameters(types::iso15118::AcParameters& ac_parameters) override;
    virtual void handle_update_ac_maximum_limits(types::iso15118::AcEvseMaximumPower& maximum_limits) override;
    virtual void handle_update_ac_minimum_limits(types::iso15118::AcEvseMinimumPower& minimum_limits) override;
    virtual void handle_update_ac_target_values(types::iso15118::AcTargetValues& target_values) override;
    virtual void handle_update_ac_present_power(types::units::Power& present_power) override;
    virtual void handle_update_dc_maximum_limits(types::iso15118::DcEvseMaximumLimits& maximum_limits) override;
    virtual void handle_update_dc_minimum_limits(types::iso15118::DcEvseMinimumLimits& minimum_limits) override;
    virtual void handle_update_isolation_status(types::iso15118::IsolationStatus& isolation_status) override;
    virtual void
    handle_update_dc_present_values(types::iso15118::DcEvsePresentVoltageCurrent& present_voltage_current) override;
    virtual void handle_update_meter_info(types::powermeter::Powermeter& powermeter) override;
    virtual void handle_send_error(types::iso15118::EvseError& error) override;
    virtual void handle_reset_error() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<Evse15118D20>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    iso15118::session::feedback::Callbacks create_callbacks();

    // The SECC leaf certificate chain backing the TLS server.
    struct TlsChain {
        std::string path_chain; //!< resolved chain file (multi-cert chain, or the single certificate)
        types::evse_security::CertificateInfo info;
    };
    // Fetch the V2G leaf from the security module. nullopt when none is installed: TLS is then simply
    // not offered rather than being a startup failure -- ISO 15118-2 still runs unsecured (EIM only) and
    // DIN SPEC 70121 never uses TLS. Only ISO 15118-20 ([V2G20-2677]) and ENFORCE_TLS need it.
    std::optional<TlsChain> acquire_tls_chain();
    // ISO 15118-20 may actually be offered: configured AND a TLS chain exists, since -20 is TLS-only
    // ([V2G20-2677]). Decided once in ready() and honoured by handle_update_supported_app_protocols too,
    // so a runtime offer update cannot switch -20 back on when there is no certificate. Atomic: written
    // on the ready thread, read from the command threads.
    std::atomic_bool iso15118_20_offerable{false};

    std::unique_ptr<iso15118::TbdController> controller;

    iso15118::session::EvseSetupConfig setup_config;
    SetupStepsDone setup_steps_done;

    // The protocol offer as configured (module config, narrowed by update_supported_app_protocols), before
    // the AC filter, plus whether an AC energy transfer mode is configured. apply_supported_protocols()
    // combines both into setup_config.supported_protocols. All three need GEL held.
    std::vector<iso15118::ProtocolId> configured_protocol_offer;
    bool ac_energy_transfer_mode{false};
    void apply_supported_protocols();

    std::optional<float> evse_max_reactive_power;

    std::vector<iso15118::d20::SupportedVASs> supported_vas_services_per_provider;
    std::mutex vas_mutex;

    void update_supported_vas_services();
    std::optional<size_t> get_vas_provider_index(uint16_t service_id);

    // EV grid-event fault detector; outlives sessions (reset at SETUP_FINISHED), touched only by the charger thread.
    GridEventEdgeDetector grid_event_detector;
    void publish_grid_event(uint8_t condition);

    // EV-negotiated DER control functions from ServiceSelection; read at ChargeParameterDiscovery to
    // surface ev_supported_dercontrol. Reset at SETUP_FINISHED; touched only by the charger thread.
    std::bitset<12> ev_selected_der_control_functions;

    // Serializes apply_active_der_directives so the per-name update loop cannot interleave between two
    // concurrent applies and leave a mixed DER-function map. Outermost lock; acquired before GEL.
    std::mutex der_apply_mutex;
    void apply_active_der_directives();

    // hlc_session_failed derivation. The last V2G message handled this session (loop thread only, from
    // the v2g_message feedback) is mapped to a reason at teardown, mirroring EvseV2G. graceful_stop and
    // emergency_shutdown are set from the module command threads (handle_stop_charging / handle_send_error)
    // so they are atomic. Either one suppresses the report: both are EVSE-initiated ends, which is what
    // EvseV2G checks (`stop_hlc || intl_emergency_shutdown`, connection.cpp:518).
    std::optional<iso15118::V2gMessageType> last_v2g_message;
    // Last published EV completion flags (DIN SPEC 70121 / ISO 15118-2 charge progress); published on
    // change only. Reset when the session's data link ends.
    std::optional<bool> last_charging_complete;
    std::optional<bool> last_bulk_charging_complete;
    std::atomic_bool graceful_stop_requested{false};
    std::atomic_bool emergency_shutdown_requested{false};
    // debug_mode from the setup command gates the v2g_messages and ev_app_protocol publishes (mirrors
    // EvseV2G, which publishes both only with debugMode). Atomic: set from the command thread, read on
    // the loop thread.
    std::atomic_bool debug_mode{false};
    void report_hlc_session_failed();
    // Clear the per-session state above; called for every end of the data link (terminate, error, pause).
    void reset_session_state();
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace charger
} // namespace module

#endif // CHARGER_ISO15118_CHARGER_IMPL_HPP
