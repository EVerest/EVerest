// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_ISO15118_EV_IMPL_HPP
#define EV_ISO15118_EV_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/ISO15118_ev/Implementation.hpp>

#include "../EvIso15118D20.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <thread>

#include <everest/util/async/monitor.hpp>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session/feedback.hpp>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace ev {

struct Conf {};

class ISO15118_evImpl : public ISO15118_evImplBase {
public:
    ISO15118_evImpl() = delete;
    ISO15118_evImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvIso15118D20>& mod, Conf& config) :
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
    virtual void handle_set_fault() override;
    virtual void handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) override;
    virtual void handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) override;
    virtual void handle_enable_sae_j2847_v2g_v2h() override;
    virtual void handle_update_soc(double& SoC) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<EvIso15118D20>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    enum class SessionPhase {
        idle,
        requested,
        running
    };

    struct SessionState {
        SessionPhase phase{SessionPhase::idle};
        bool shutting_down{false};
        iso15118::ev::Controller* current{nullptr};
        iso15118::ev::DcChargeParams dc_params;
        bool bpt_dc_params_set{false};
        iso15118::ev::AcChargeParams ac_params;
        iso15118::message_20::datatypes::ServiceCategory energy_service{
            iso15118::message_20::datatypes::ServiceCategory::DC};
    };

    everest::lib::util::monitor<SessionState> session;
    std::thread worker;

    void session_worker();
    void run_one_session();
    iso15118::ev::EvConfig make_ev_config(iso15118::message_20::datatypes::ServiceCategory energy_service) const;
    iso15118::ev::feedback::Callbacks make_callbacks();
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace ev
} // namespace module

#endif // EV_ISO15118_EV_IMPL_HPP
