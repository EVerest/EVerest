// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <generic_ocpp.hpp>

namespace stubs {

struct ConfigStub : public ocpp_multi::ConfigInterface {
    std::string ChargePointConfigPath{"config"};
    int CompositeScheduleIntervalS{500};
    std::string CoreDatabasePath{"core.db"};
    std::string CustomMrecErrorMapPath{};
    std::string DatabasePath{};
    int DelayOcppStart{1};
    std::string DeviceModelConfigMappings{};
    std::string DeviceModelConfigPath{"dm_config"};
    std::string DeviceModelDatabasePath{"dm.db"};
    std::string DeviceModelDatabaseMigrationPath{"device_model_migrations"};
    bool EnableExternalWebsocketControl{true};
    bool EnableLegacyConfigMigration{false};
    std::string EverestDeviceModelDatabasePath{"everest.db"};
    int grid_support_heartbeat_s{60};
    int Ocpp16NetworkConfigSlot{1};
    std::string MessageLogPath{"log"};
    int MessageQueueResumeDelay{120};
    int RequestCompositeScheduleDurationS{600};
    std::string RequestCompositeScheduleUnit{"A"};
    int ResetStopDelay{0};
    std::string UserConfigPath{"user_config"};

    [[nodiscard]] std::string getChargePointConfigPath() const override {
        return ChargePointConfigPath;
    }
    [[nodiscard]] int getCompositeScheduleIntervalS() const override {
        return CompositeScheduleIntervalS;
    }
    [[nodiscard]] std::string getCoreDatabasePath() const override {
        return CoreDatabasePath;
    }
    [[nodiscard]] std::string getCustomMrecErrorMapPath() const override {
        return CustomMrecErrorMapPath;
    }
    [[nodiscard]] int getDelayOcppStart() const override {
        return DelayOcppStart;
    }
    [[nodiscard]] std::string getDatabasePath() const override {
        return DeviceModelConfigPath;
    }
    [[nodiscard]] std::string getDeviceModelConfigMappings() const override {
        return DeviceModelConfigMappings;
    }
    [[nodiscard]] std::string getDeviceModelConfigPath() const override {
        return DeviceModelConfigPath;
    }
    [[nodiscard]] std::string getDeviceModelDatabasePath() const override {
        return DeviceModelDatabasePath;
    }
    [[nodiscard]] std::string getDeviceModelDatabaseMigrationPath() const override {
        return DeviceModelDatabaseMigrationPath;
    }
    [[nodiscard]] bool getEnableExternalWebsocketControl() const override {
        return EnableExternalWebsocketControl;
    }
    [[nodiscard]] bool getEnableLegacyConfigMigration() const override {
        return EnableLegacyConfigMigration;
    }
    [[nodiscard]] std::string getEverestDeviceModelDatabasePath() const override {
        return EverestDeviceModelDatabasePath;
    }
    [[nodiscard]] int getGridSupportHeartbeatS() const override {
        return grid_support_heartbeat_s;
    }
    [[nodiscard]] int getOcpp16NetworkConfigSlot() const override {
        return Ocpp16NetworkConfigSlot;
    }
    [[nodiscard]] std::string getMessageLogPath() const override {
        return MessageLogPath;
    }
    [[nodiscard]] int getMessageQueueResumeDelay() const override {
        return MessageQueueResumeDelay;
    }
    [[nodiscard]] int getRequestCompositeScheduleDurationS() const override {
        return RequestCompositeScheduleDurationS;
    }
    [[nodiscard]] std::string getRequestCompositeScheduleUnit() const override {
        return RequestCompositeScheduleUnit;
    }
    [[nodiscard]] int getResetStopDelay() const override {
        return ResetStopDelay;
    }
    [[nodiscard]] std::string getUserConfigPath() const override {
        return UserConfigPath;
    }
};

} // namespace stubs
