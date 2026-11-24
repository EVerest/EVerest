// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SUN_SPEC_MODBUS_SERVER_HPP
#define SUN_SPEC_MODBUS_SERVER_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for required interface implementations
#include <generated/interfaces/evse_manager/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <sunspec_map.hpp>
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string listen_address;
    int port;
    std::string manufacturer;
    std::string model;
    std::string options;
    std::string version;
    std::string serial_number;
    int device_address;
    std::string ac_type;
    double max_power_W;
    double max_current_A;
    double max_voltage_V;
};

class SunSpecModbusServer : public Everest::ModuleBase {
public:
    SunSpecModbusServer() = delete;
    SunSpecModbusServer(const ModuleInfo& info, std::unique_ptr<evse_managerIntf> r_evse_manager, Conf& config) :
        ModuleBase(info), r_evse_manager(std::move(r_evse_manager)), config(config){};

    const std::unique_ptr<evse_managerIntf> r_evse_manager;
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
    void accept_loop(int listen_socket);
    void serve_client(int client_socket);
    std::vector<std::uint8_t> read_registers(std::uint16_t address, std::uint16_t count);

    std::unique_ptr<sunspec::RegisterMap> register_map;
    std::mutex register_map_mutex;
    std::atomic_int active_clients{0};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // SUN_SPEC_MODBUS_SERVER_HPP
