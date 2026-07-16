// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OCPPMULTI_HPP
#define OCPPMULTI_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/auth_token_provider/Implementation.hpp>
#include <generated/interfaces/auth_token_validator/Implementation.hpp>
#include <generated/interfaces/ocpp/Implementation.hpp>
#include <generated/interfaces/ocpp_data_transfer/Implementation.hpp>
#include <generated/interfaces/session_cost/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/auth/Interface.hpp>
#include <generated/interfaces/charger_information/Interface.hpp>
#include <generated/interfaces/display_message/Interface.hpp>
#include <generated/interfaces/evse_manager/Interface.hpp>
#include <generated/interfaces/evse_security/Interface.hpp>
#include <generated/interfaces/external_energy_limits/Interface.hpp>
#include <generated/interfaces/iso15118_extensions/Interface.hpp>
#include <generated/interfaces/ocpp_data_transfer/Interface.hpp>
#include <generated/interfaces/reservation/Interface.hpp>
#include <generated/interfaces/system/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include "generic_chargepoint.hpp"
#include "generic_ocpp.hpp"
namespace module {
struct Conf;

/// \brief Access configuration
class ConfigAccess : public ocpp_multi::ConfigInterface {
private:
    const module::Conf& m_config;

public:
    explicit ConfigAccess(const module::Conf& config) : m_config(config) {
    }

    [[nodiscard]] std::string getChargePointConfigPath() const override;
    [[nodiscard]] int getCompositeScheduleIntervalS() const override;
    [[nodiscard]] std::string getCoreDatabasePath() const override;
    [[nodiscard]] std::string getCustomMrecErrorMapPath() const override;
    [[nodiscard]] std::string getDatabasePath() const override;
    [[nodiscard]] int getDelayOcppStart() const override;
    [[nodiscard]] std::string getDeviceModelConfigMappings() const override;
    [[nodiscard]] std::string getDeviceModelConfigPath() const override;
    [[nodiscard]] std::string getDeviceModelDatabasePath() const override;
    [[nodiscard]] std::string getDeviceModelDatabaseMigrationPath() const override;
    [[nodiscard]] bool getEnableExternalWebsocketControl() const override;
    [[nodiscard]] bool getEnableLegacyConfigMigration() const override;
    [[nodiscard]] int getOcpp16NetworkConfigSlot() const override;
    [[nodiscard]] std::string getEverestDeviceModelDatabasePath() const override;
    [[nodiscard]] std::string getMessageLogPath() const override;
    [[nodiscard]] int getMessageQueueResumeDelay() const override;
    [[nodiscard]] int getRequestCompositeScheduleDurationS() const override;
    [[nodiscard]] std::string getRequestCompositeScheduleUnit() const override;
    [[nodiscard]] int getResetStopDelay() const override;
    [[nodiscard]] std::string getUserConfigPath() const override;
};

} // namespace module
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string ChargePointConfigPath;
    int CompositeScheduleIntervalS;
    std::string CoreDatabasePath;
    std::string CustomMrecErrorMapPath;
    std::string DatabasePath;
    int DelayOcppStart;
    std::string DeviceModelConfigPath;
    std::string DeviceModelDatabasePath;
    std::string DeviceModelDatabaseMigrationPath;
    bool EnableLegacyConfigMigration;
    std::string DeviceModelConfigMappings;
    int Ocpp16NetworkConfigSlot;
    bool EnableExternalWebsocketControl;
    std::string EverestDeviceModelDatabasePath;
    std::string MessageLogPath;
    int MessageQueueResumeDelay;
    int RequestCompositeScheduleDurationS;
    std::string RequestCompositeScheduleUnit;
    int ResetStopDelay;
    std::string UserConfigPath;
    std::string Mode;
};

class OCPPmulti : public Everest::ModuleBase {
public:
    OCPPmulti() = delete;
    OCPPmulti(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
              std::unique_ptr<auth_token_validatorImplBase> p_auth_validator,
              std::unique_ptr<auth_token_providerImplBase> p_auth_provider,
              std::unique_ptr<ocpp_data_transferImplBase> p_data_transfer, std::unique_ptr<ocppImplBase> p_ocpp_generic,
              std::unique_ptr<session_costImplBase> p_session_cost, std::unique_ptr<authIntf> r_auth,
              std::vector<std::unique_ptr<charger_informationIntf>> r_charger_information,
              std::vector<std::unique_ptr<ocpp_data_transferIntf>> r_data_transfer,
              std::vector<std::unique_ptr<display_messageIntf>> r_display_message,
              std::vector<std::unique_ptr<external_energy_limitsIntf>> r_evse_energy_sink,
              std::vector<std::unique_ptr<evse_managerIntf>> r_evse_manager,
              std::vector<std::unique_ptr<iso15118_extensionsIntf>> r_extensions_15118,
              std::vector<std::unique_ptr<reservationIntf>> r_reservation,
              std::unique_ptr<evse_securityIntf> r_security, std::unique_ptr<systemIntf> r_system, Conf& config) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        p_auth_validator(std::move(p_auth_validator)),
        p_auth_provider(std::move(p_auth_provider)),
        p_data_transfer(std::move(p_data_transfer)),
        p_ocpp_generic(std::move(p_ocpp_generic)),
        p_session_cost(std::move(p_session_cost)),
        r_auth(std::move(r_auth)),
        r_charger_information(std::move(r_charger_information)),
        r_data_transfer(std::move(r_data_transfer)),
        r_display_message(std::move(r_display_message)),
        r_evse_energy_sink(std::move(r_evse_energy_sink)),
        r_evse_manager(std::move(r_evse_manager)),
        r_extensions_15118(std::move(r_extensions_15118)),
        r_reservation(std::move(r_reservation)),
        r_security(std::move(r_security)),
        r_system(std::move(r_system)),
        config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<auth_token_validatorImplBase> p_auth_validator;
    const std::unique_ptr<auth_token_providerImplBase> p_auth_provider;
    const std::unique_ptr<ocpp_data_transferImplBase> p_data_transfer;
    const std::unique_ptr<ocppImplBase> p_ocpp_generic;
    const std::unique_ptr<session_costImplBase> p_session_cost;
    const std::unique_ptr<authIntf> r_auth;
    const std::vector<std::unique_ptr<charger_informationIntf>> r_charger_information;
    const std::vector<std::unique_ptr<ocpp_data_transferIntf>> r_data_transfer;
    const std::vector<std::unique_ptr<display_messageIntf>> r_display_message;
    const std::vector<std::unique_ptr<external_energy_limitsIntf>> r_evse_energy_sink;
    const std::vector<std::unique_ptr<evse_managerIntf>> r_evse_manager;
    const std::vector<std::unique_ptr<iso15118_extensionsIntf>> r_extensions_15118;
    const std::vector<std::unique_ptr<reservationIntf>> r_reservation;
    const std::unique_ptr<evse_securityIntf> r_security;
    const std::unique_ptr<systemIntf> r_system;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here

    // members destruct in reverse order, i.e. m_ocpp (the callback sink) before m_charge_point
    // (the callback source); tear down libocpp first so no callback fires into a dead GenericOcpp
    ~OCPPmulti();

    ConfigAccess m_config{config};
    ocpp_multi::GenericChargePoint m_charge_point{*r_security};
    ocpp_multi::GenericOcpp m_ocpp{
        m_charge_point,
        info,
        m_config,
        {*p_auth_validator, *p_auth_provider, *p_data_transfer, *p_ocpp_generic, *p_session_cost},
        {*r_auth, r_charger_information, r_data_transfer, r_display_message, r_evse_energy_sink, r_evse_manager,
         r_extensions_15118, r_reservation, *r_security, *r_system}};
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

#endif // OCPPMULTI_HPP
