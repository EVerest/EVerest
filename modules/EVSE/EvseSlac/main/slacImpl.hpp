// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_SLAC_IMPL_HPP
#define MAIN_SLAC_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/slac/Implementation.hpp>

#include "../EvseSlac.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include <map>
#include <memory>
#include <string>

#include "slac_runtime.hpp"
#include "slac_sink.hpp"
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {
    std::string device;
    int number_of_sounds;
    bool ac_mode_five_percent;
    int set_key_timeout_ms;
    std::string set_key_handling_mode;
    std::string set_key_cnf_success_mode;
    std::string nmk_generation_mode;
    int set_key_max_attempts;
    int sounding_attenuation_adjustment;
    bool publish_mac_on_match_cnf;
    bool publish_mac_on_first_parm_req;
    bool do_chip_reset;
    int chip_reset_delay_ms;
    int chip_reset_timeout_ms;
    bool link_status_detection;
    int link_status_retry_ms;
    int link_status_timeout_ms;
    int link_status_poll_in_matched_state_ms;
    int link_status_debounce_count;
    bool debug_simulate_failed_matching;
    bool reset_instead_of_fail;
    int max_matching_sessions;
    int startup_delay_ms;
    int slac_init_timeout_ms;
    bool print_state_transitions;
    bool hack_disable_regenerate_key_on_reset;
    bool initiate_amp_map;
    std::string amp_map_file;
};

class slacImpl : public slacImplBase {
public:
    slacImpl() = delete;
    slacImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvseSlac>& mod, Conf& config) :
        slacImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // The framework calls shutdown() during orderly teardown; the destructor calls it again as an idempotent fallback.
    ~slacImpl() override;
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual void handle_reset(bool& enable) override;
    virtual void handle_enter_bcd() override;
    virtual void handle_leave_bcd() override;
    virtual void handle_count_bc(int& count) override;
    virtual void handle_dlink_terminate() override;
    virtual void handle_dlink_error() override;
    virtual void handle_dlink_pause() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<EvseSlac>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // This class is the framework adapter only: the manifest values become a typed
    // SlacRuntimeConfig, the runtime's outputs land in the generated publish_* / raise_error /
    // EVLOG through FrameworkSink, and every lifecycle hook and command forwards to the runtime.
    // Everything that can go wrong at run time lives in SlacRuntime, which has no framework
    // dependency and is unit-tested with a fake I/O.
    class FrameworkSink final : public SlacSink {
    public:
        explicit FrameworkSink(slacImpl& owner) : owner(owner) {
        }
        void publish_state(everest::lib::slac::D3State state) override;
        void publish_dlink_ready(bool ready) override;
        void publish_ev_mac_address(std::string const& mac) override;
        void request_error_routine() override;
        void raise_fault(std::string const& type, std::string const& sub_type, std::string const& message) override;
        void clear_fault(std::string const& type) override;
        void publish_telemetry(std::string const& block, std::string const& key, std::string const& value) override;
        void log(LogLevel level, std::string const& text) override;

    private:
        slacImpl& owner;
        std::map<std::string, Everest::TelemetryMap> telemetry_generic;
    };

    SlacRuntimeConfig make_runtime_config() const;

    std::unique_ptr<FrameworkSink> sink;
    std::unique_ptr<SlacRuntime> runtime;
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_SLAC_IMPL_HPP
