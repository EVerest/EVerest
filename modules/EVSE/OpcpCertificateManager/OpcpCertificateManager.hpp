// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OPCP_CERTIFICATE_MANAGER_HPP
#define OPCP_CERTIFICATE_MANAGER_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/opcp_certificate_manager/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/evse_security/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <memory>

#include <everest/opcp/http_client.hpp>

#include "certificate_manager.hpp"
#include "interface_security_store.hpp"
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string environment;
    std::string api_base_url;
    std::string est_base_url;
    std::string simplereenroll_path;
    std::string cacerts_path;
    std::string ca;
    std::string server_ca_bundle;
    bool manage_iso15118_2;
    bool manage_iso15118_20;
    std::string common_name_iso2;
    std::string common_name_iso20;
    std::string organization;
    std::string country;
    bool use_tpm;
    int renewal_threshold_days;
    int check_interval_s;
    int initial_delay_s;
    int retry_backoff_s;
    bool sync_root_certificates;
    std::string root_types;
    int root_sync_interval_s;
    int http_timeout_ms;
};

class OpcpCertificateManager : public Everest::ModuleBase {
public:
    OpcpCertificateManager() = delete;
    OpcpCertificateManager(const ModuleInfo& info, Everest::TelemetryProvider& telemetry,
                           std::unique_ptr<opcp_certificate_managerImplBase> p_main,
                           std::unique_ptr<evse_securityIntf> r_security, Conf& config) :
        ModuleBase(info),
        telemetry(telemetry),
        p_main(std::move(p_main)),
        r_security(std::move(r_security)),
        config(config){};

    Everest::TelemetryProvider& telemetry;
    const std::unique_ptr<opcp_certificate_managerImplBase> p_main;
    const std::unique_ptr<evse_securityIntf> r_security;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    /// Set once the configuration was accepted; commands are refused otherwise
    CertificateManager* manager() {
        return certificate_manager.get();
    }
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();
    void shutdown();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // insert your private definitions here
    std::unique_ptr<InterfaceSecurityStore> security_store;
    std::unique_ptr<opcp::CurlHttpClient> http_client;
    std::unique_ptr<CertificateManager> certificate_manager;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // OPCP_CERTIFICATE_MANAGER_HPP
