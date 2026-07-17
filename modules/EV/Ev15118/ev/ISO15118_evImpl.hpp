// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_ISO15118_EV_IMPL_HPP
#define EV_ISO15118_EV_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/ISO15118_ev/Implementation.hpp>

#include "../Ev15118.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <iso15118/d20/ev/config.hpp>
#include <iso15118/ev_controller.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/session/ev_feedback.hpp>
#include <iso15118/session/protocol.hpp>
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
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual bool handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                       types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                       double& DepartureTime, double& EAmount) override;
    virtual void handle_stop_charging() override;
    virtual void handle_pause_charging() override;
    virtual void handle_set_fault() override;
    virtual void handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) override;
    virtual void handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) override;
    virtual void handle_enable_sae_j2847_v2g_v2h() override;
    virtual void handle_update_soc(double& SoC) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<Ev15118>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // The EVCC library controller. Built in ready() and then run with a blocking loop() on the
    // ready thread. Command handlers run on other threads and use the controller's thread-safe methods.
    std::unique_ptr<iso15118::EvController> controller;

    // Guards the stored parameters below and controller creation, since the command handlers may be
    // called from other threads concurrently with ready().
    std::mutex config_mutex;

    // Target parameters for the DC charging process, provided by the EV simulation. Stored so they
    // can seed the initial setup config in ready() and be re-applied to the controller on updates.
    std::optional<types::iso15118::DcEvParameters> dc_params;
    std::optional<types::iso15118::DcEvBPTParameters> bpt_dc_params;

    // Prioritized list of the energy services parsed from the supported_d20_energy_services config.
    std::vector<iso15118::message_20::datatypes::ServiceCategory> supported_services;

    // Prioritized list of protocol generations offered in the SAP handshake, built from config.
    std::vector<iso15118::ProtocolId> supported_protocols;

    // Set by init() when the configuration is fatally invalid; ready() then refuses to start the EVCC.
    bool startup_error{false};

    // EVCCID / MAC of the (resolved) HLC interface, read from the OS in ready().
    std::array<uint8_t, 6> evcc_mac{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Build the DC / BPT charge parameter sets from the stored EvManager values (config_mutex held).
    iso15118::d20::ev::DcEvChargeParameters build_dc_charge_parameters() const;
    std::optional<iso15118::d20::ev::DcEvBptChargeParameters> build_bpt_dc_charge_parameters() const;

    // Map a requested EnergyTransferMode family onto the first matching configured service, falling
    // back to a generic AC/DC service when no ISO 15118-20 service is configured.
    std::optional<iso15118::message_20::datatypes::ServiceCategory>
    select_service_for(types::iso15118::EnergyTransferMode mode) const;

    iso15118::session::ev::feedback::Callbacks create_callbacks();
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace ev
} // namespace module

#endif // EV_ISO15118_EV_IMPL_HPP
