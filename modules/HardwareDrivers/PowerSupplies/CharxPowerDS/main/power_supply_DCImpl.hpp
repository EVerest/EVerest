// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_POWER_SUPPLY_DC_IMPL_HPP
#define MAIN_POWER_SUPPLY_DC_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/power_supply_DC/Implementation.hpp>

#include "../CharxPowerDS.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "charx_canopen.hpp"
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {};

class power_supply_DCImpl : public power_supply_DCImplBase {
public:
    power_supply_DCImpl() = delete;
    power_supply_DCImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<CharxPowerDS>& mod, Conf& config) :
        power_supply_DCImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual void handle_setMode(types::power_supply_DC::Mode& mode,
                                types::power_supply_DC::ChargingPhase& phase) override;
    virtual void handle_setExportVoltageCurrent(double& voltage, double& current) override;
    virtual void handle_setImportVoltageCurrent(double& voltage, double& current) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<CharxPowerDS>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    using Clock = std::chrono::steady_clock;

    bool read_capabilities();
    bool poll_status();
    void actuate();
    void handle_errors();
    void update_reported_mode();
    void set_error(const std::string& type, bool active, const std::string& message);
    charx::SdoResult write(charx::Obj o, int32_t value, const char* what);

    std::unique_ptr<charx::Canopen> can;
    int32_t contactor_value{1};

    // targets from EvseManager (guarded by mtx)
    std::mutex mtx;
    std::condition_variable cv;
    bool target_export{false};
    double target_voltage{0};
    double target_current{0};
    uint64_t target_generation{0}; // bumps on every setMode / setpoint change

    // what the module confirmed in the last poll (guarded by mtx)
    bool status_valid{false};
    uint32_t flags1{0};
    int32_t ds_status{0};
    int32_t contactor_error{0};
    double v_meas{0};
    double i_meas{0};
    bool power_on() const;
    bool contactor_closed() const;

    // actuation state (loop thread only)
    uint64_t applied_generation{~0ull};
    bool sent_voltage{false};
    bool sent_contactor{false};
    bool power_on_accepted{false};
    bool sent_current{false};
    bool sent_off{false};
    std::optional<Clock::time_point> on_requested_at;
    std::optional<Clock::time_point> off_requested_at;
    Clock::time_point last_resend{};

    // limits
    double cap_v_min{50};
    double cap_v_max{1000};
    double cap_i_max{125};

    int comm_failures{0};
    std::optional<types::power_supply_DC::Mode> reported_mode;
    uint32_t last_logged_flags{0xFFFFFFFF};
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_POWER_SUPPLY_DC_IMPL_HPP
