// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <cstdint>

#include <everest/util/async/monitor.hpp>

#include <ocpp/v2/message_handler.hpp>
#include <ocpp/v2/messages/FirmwareStatusNotification.hpp>
#include <ocpp/v2/messages/UpdateFirmware.hpp>

namespace ocpp::v2 {
// Formward declarations.
struct FunctionalBlockContext;
class AvailabilityInterface;
class SecurityInterface;

// Typedef
using UpdateFirmwareRequestCallback = std::function<UpdateFirmwareResponse(const UpdateFirmwareRequest& request)>;
using AllConnectorsUnavailableCallback = std::function<void()>;

bool is_firmware_status_end_state(const FirmwareStatusEnum& status);

class FirmwareUpdateInterface : public MessageHandlerInterface {
public:
    ~FirmwareUpdateInterface() override = default;

    virtual void
    on_firmware_update_status_notification(std::int32_t request_id, const FirmwareStatusEnum& firmware_update_status,
                                           std::optional<bool> disable_connectors_during_install = std::nullopt) = 0;
    virtual void on_firmware_status_notification_request() = 0;
    virtual void on_transaction_finished() = 0;
    virtual void on_registration_accepted() = 0;

    /// \brief Signals that no connector is available any more. Reaches the all_connectors_unavailable_callback once
    /// per update cycle, and only while an update is in progress.
    virtual void on_all_connectors_unavailable() = 0;
};

class FirmwareUpdate : public FirmwareUpdateInterface {
private: // Members
    const FunctionalBlockContext& context;
    AvailabilityInterface& availability;
    SecurityInterface& security;
    std::atomic<RegistrationStatusEnum>& registration_status;

    UpdateFirmwareRequestCallback update_firmware_request_callback;
    std::optional<AllConnectorsUnavailableCallback> all_connectors_unavailable_callback;

    everest::lib::util::monitor<std::optional<FirmwareStatusNotificationRequest>> pending_status;

    // Whether the running firmware update still has to be told that all connectors became unavailable
    enum class InstallNotificationState {
        Idle,
        Waiting,
        Notified,
    };

    struct RequestedUpdate {
        std::int32_t request_id;
        // The last firmware status which will be posted before the firmware is installed.
        FirmwareStatusEnum status_before_installing;
    };

    // Written from the message handler thread and from the thread reporting firmware statuses
    struct CycleState {
        FirmwareStatusEnum firmware_status{FirmwareStatusEnum::Idle};
        // The request ID in the last firmware update status received
        std::optional<std::int32_t> firmware_status_id;
        // Counts the firmware statuses reported by the application
        std::uint64_t status_generation{0};
        // The UpdateFirmwareRequest that is being passed to the update_firmware_request_callback
        std::optional<RequestedUpdate> offered_update;
        // The last UpdateFirmwareRequest that was accepted
        std::optional<RequestedUpdate> requested_update;
        // False once the requested update reported an end state
        bool requested_update_active{false};
        InstallNotificationState notification_state{InstallNotificationState::Idle};
        // The status_generation at which notification_state became Notified
        std::uint64_t notified_generation{0};
        // Connectors were made unavailable for an update and not restored since
        bool connectors_disabled{false};
    };
    everest::lib::util::monitor<CycleState> cycle;

    // Download deferred until the last transaction ends (L01.FR.13). This is memory-only and does
    // not survive a restart: after a reboot while in DownloadScheduled the CSMS must re-send
    // UpdateFirmware. Written from the message handler thread and read/cleared from the module event
    // thread, so all access goes through the monitor handle.
    everest::lib::util::monitor<std::optional<UpdateFirmwareRequest>> deferred_update_firmware_request;

public:
    FirmwareUpdate(const FunctionalBlockContext& functional_block_context, AvailabilityInterface& availability,
                   SecurityInterface& security, UpdateFirmwareRequestCallback update_firmware_request_callback,
                   std::optional<AllConnectorsUnavailableCallback> all_connectors_unavailable_callback,
                   std::atomic<RegistrationStatusEnum>& registration_status);
    void handle_message(const ocpp::EnhancedMessage<MessageType>& message) override;
    void on_firmware_update_status_notification(
        std::int32_t request_id, const FirmwareStatusEnum& firmware_update_status,
        std::optional<bool> disable_connectors_during_install = std::nullopt) override;
    void on_firmware_status_notification_request() override;
    void on_transaction_finished() override;
    void on_registration_accepted() override;
    void on_all_connectors_unavailable() override;

private: // Functions
    // Functional Block L: Firmware management
    void handle_firmware_update_req(Call<UpdateFirmwareRequest> call);

    /// \brief Passes \p request to the update_firmware_request_callback and arms the all-connectors-unavailable
    /// notification if it is accepted.
    UpdateFirmwareResponse request_firmware_update(const UpdateFirmwareRequest& request);

    static RequestedUpdate to_requested_update(const UpdateFirmwareRequest& request);

    /// \brief The status after which the update reported with \p request_id is installed.
    static FirmwareStatusEnum status_before_installing(const CycleState& state, std::int32_t request_id);

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
