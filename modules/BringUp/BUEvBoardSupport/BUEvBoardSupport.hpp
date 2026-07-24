// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef BUEV_BOARD_SUPPORT_HPP
#define BUEV_BOARD_SUPPORT_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for required interface implementations
#include <generated/interfaces/ev_board_support/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {};

class BUEvBoardSupport : public Everest::ModuleBase {
public:
    BUEvBoardSupport() = delete;
    BUEvBoardSupport(const ModuleInfo& info, std::unique_ptr<ev_board_supportIntf> r_bsp, Conf& config) :
        ModuleBase(info), r_bsp(std::move(r_bsp)), config(config){};

    const std::unique_ptr<ev_board_supportIntf> r_bsp;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // insert your private definitions here

    std::mutex data_mutex;
    std::string cp_state;
    std::string relais_feedback;
    std::string proximity_pilot;
    std::string duty_cycle;
    std::string rcd_current;
    std::vector<std::vector<std::string>> ev_info;

    std::chrono::time_point<std::chrono::steady_clock> last_allow_power_on_time_point;
    std::string last_error_raised;
    std::string last_error_cleared;

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // BUEV_BOARD_SUPPORT_HPP
