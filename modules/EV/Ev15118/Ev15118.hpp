// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV15118_HPP
#define EV15118_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/ISO15118_ev/Implementation.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string device;
    std::string supported_d20_energy_services;
    bool supported_DIN70121;
    bool supported_ISO15118_2;
    bool tls_active;
    bool enforce_tls;
    bool enable_tls_1_3;
    bool verify_server_certificate;
    std::string evccid;
    std::string logging_path;
    bool enable_ssl_logging;
    bool enable_tls_key_logging;
    std::string tls_key_logging_path;
    std::string v2g_root_cert_path;
    std::string device_cert_chain_path;
    std::string device_key_path;
    std::string device_key_password_path;
};

class Ev15118 : public Everest::ModuleBase {
public:
    Ev15118() = delete;
    Ev15118(const ModuleInfo& info, std::unique_ptr<ISO15118_evImplBase> p_ev, Conf& config) :
        ModuleBase(info), p_ev(std::move(p_ev)), config(config){};

    const std::unique_ptr<ISO15118_evImplBase> p_ev;
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
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EV15118_HPP
