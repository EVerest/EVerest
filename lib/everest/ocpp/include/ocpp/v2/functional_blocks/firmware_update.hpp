// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <mutex>

#include <ocpp/v2/message_handler.hpp>
#include <ocpp/v2/messages/UpdateFirmware.hpp>

namespace ocpp::v2 {
// Formward declarations.
struct FunctionalBlockContext;
class AvailabilityInterface;
class SecurityInterface;

// Typedef
using UpdateFirmwareRequestCallback = std::function<UpdateFirmwareResponse(const UpdateFirmwareRequest& request)>;
using AllConnectorsUnavailableCallback = std::function<void()>;

class FirmwareUpdateInterface : public MessageHandlerInterface {
public:
    ~FirmwareUpdateInterface() override = default;

    virtual void on_firmware_update_status_notification(std::int32_t request_id,
                                                        const FirmwareStatusEnum& firmware_update_status,
                                                        const bool disable_connectors_during_install = true) = 0;
    virtual void on_firmware_status_notification_request() = 0;
    virtual void on_transaction_finished() = 0;
};

class FirmwareUpdate : public FirmwareUpdateInterface {
private: // Members
    const FunctionalBlockContext& context;
    AvailabilityInterface& availability;
    SecurityInterface& security;

    UpdateFirmwareRequestCallback update_firmware_request_callback;
    std::optional<AllConnectorsUnavailableCallback> all_connectors_unavailable_callback;

    FirmwareStatusEnum firmware_status;
    // The request ID in the last firmware update status received
    std::optional<std::int32_t> firmware_status_id;
    // The last firmware status which will be posted before the firmware is installed.
    FirmwareStatusEnum firmware_status_before_installing = FirmwareStatusEnum::SignatureVerified;
    // Download deferred until the last transaction ends (L01.FR.13). This is memory-only and does
    // not survive a restart: after a reboot while in DownloadScheduled the CSMS must re-send
    // UpdateFirmware. Written from the message handler thread and read/cleared from the module event
    // thread, so all access is guarded by deferred_update_firmware_request_mutex.
    std::optional<UpdateFirmwareRequest> deferred_update_firmware_request;
    std::mutex deferred_update_firmware_request_mutex;

public:
    FirmwareUpdate(const FunctionalBlockContext& functional_block_context, AvailabilityInterface& availability,
                   SecurityInterface& security, UpdateFirmwareRequestCallback update_firmware_request_callback,
                   std::optional<AllConnectorsUnavailableCallback> all_connectors_unavailable_callback);
    void handle_message(const ocpp::EnhancedMessage<MessageType>& message) override;
    void on_firmware_update_status_notification(std::int32_t request_id,
                                                const FirmwareStatusEnum& firmware_update_status,
                                                bool disable_connectors_during_install = true) override;
    void on_firmware_status_notification_request() override;
    void on_transaction_finished() override;

private: // Functions
    // Functional Block L: Firmware management
    void handle_firmware_update_req(Call<UpdateFirmwareRequest> call);

    /// \brief True if the download shall wait until the last transaction ends (L01.FR.13, gated by
    /// DeferFirmwareDownloadDuringTransaction)
    bool is_download_deferral_required() const;

    /// \brief Changes all unoccupied connectors to unavailable. If a transaction is running schedule an availabilty
    /// change
    /// If all connectors are unavailable signal to the firmware updater that installation of the firmware update can
    /// proceed
    void change_all_connectors_to_unavailable_for_firmware_update();

    /// \brief Restores all connectors to their persisted state
    void restore_all_connector_states();
};
} // namespace ocpp::v2
