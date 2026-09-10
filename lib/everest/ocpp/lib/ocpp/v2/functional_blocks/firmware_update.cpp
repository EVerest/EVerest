// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/functional_blocks/firmware_update.hpp>

#include <ocpp/v2/functional_blocks/availability.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/functional_blocks/security.hpp>
#include <ocpp/v2/messages/FirmwareStatusNotification.hpp>
#include <ocpp/v2/messages/UpdateFirmware.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/evse_manager.hpp>

namespace ocpp::v2 {

bool is_firmware_status_end_state(const FirmwareStatusEnum& status) {
    return status == FirmwareStatusEnum::DownloadFailed or status == FirmwareStatusEnum::InstallationFailed or
           status == FirmwareStatusEnum::Installed or status == FirmwareStatusEnum::InstallVerificationFailed or
           status == FirmwareStatusEnum::InvalidSignature;
}

FirmwareUpdate::FirmwareUpdate(const FunctionalBlockContext& functional_block_context,
                               AvailabilityInterface& availability, SecurityInterface& security,
                               UpdateFirmwareRequestCallback update_firmware_request_callback,
                               std::optional<AllConnectorsUnavailableCallback> all_connectors_unavailable_callback) :
    context(functional_block_context),
    availability(availability),
    security(security),
    update_firmware_request_callback(update_firmware_request_callback),
    all_connectors_unavailable_callback(all_connectors_unavailable_callback),
    firmware_status(FirmwareStatusEnum::Idle) {
}

void FirmwareUpdate::handle_message(const ocpp::EnhancedMessage<MessageType>& message) {
    if (message.messageType == MessageType::UpdateFirmware) {
        this->handle_firmware_update_req(message.message);
    } else {
        throw MessageTypeNotImplementedException(message.messageType);
    }
}

void FirmwareUpdate::on_firmware_update_status_notification(std::int32_t request_id,
                                                            const FirmwareStatusEnum& firmware_update_status,
                                                            std::optional<bool> disable_connectors_during_install) {
    bool is_duplicate = false;
    if (this->firmware_status == firmware_update_status) {
        if (request_id == -1 or
            (this->firmware_status_id.has_value() and this->firmware_status_id.value() == request_id)) {
            // already sent, do not send again
            is_duplicate = true;
        }
    }

    if (!is_duplicate) {
        FirmwareStatusNotificationRequest req;
        req.status = firmware_update_status;
        // Firmware status and id are stored for future trigger message request.
        this->firmware_status = req.status;

        if (request_id != -1) {
            req.requestId = request_id; // L01.FR.20
            this->firmware_status_id = request_id;
        }

        const ocpp::Call<FirmwareStatusNotificationRequest> call(req);
        this->context.message_dispatcher.dispatch_call_async(call);

        if (req.status == FirmwareStatusEnum::Installed) {
            std::string firmwareVersionMessage = "New firmware succesfully installed! Version: ";
            firmwareVersionMessage.append(
                this->context.device_model.get_value<std::string>(ControllerComponentVariables::FirmwareVersion));
            this->security.security_event_notification_req(CiString<50>(ocpp::security_events::FIRMWARE_UPDATED),
                                                           std::optional<CiString<255>>(firmwareVersionMessage), true,
                                                           true); // L01.FR.31
        } else if (req.status == FirmwareStatusEnum::InvalidSignature) {
            this->security.security_event_notification_req(
                CiString<50>(ocpp::security_events::INVALIDFIRMWARESIGNATURE),
                std::optional<CiString<255>>("Signature of the provided firmware is not valid!"), true,
                true); // L01.FR.03 - critical because TC_L_06_CS requires this message to be sent
        }

        if (is_firmware_status_end_state(req.status) || req.status == FirmwareStatusEnum::Idle) {
            // One of the end states is reached. Restore all connector states.
            // Also restore if the firmware update status is Idle (for example if an update died)
            this->restore_all_connector_states();
        }

        if (this->firmware_status_before_installing == req.status) {
            // FIXME(Kai): This is a temporary workaround, because the EVerest System module does not keep track of
            // transactions and can't inquire about their status from the OCPP modules. If the firmware status is
            // expected to become "Installing", but we still have a transaction running, the update will wait for the
            // transaction to finish, and so we send an "InstallScheduled" status. This is necessary for OCTT TC_L_15_CS
            // to pass.
            const auto transaction_active = this->context.evse_manager.any_transaction_active(std::nullopt);
            if (transaction_active) {
                this->firmware_status = FirmwareStatusEnum::InstallScheduled;
                req.status = firmware_status;
                const ocpp::Call<FirmwareStatusNotificationRequest> call(req);
                this->context.message_dispatcher.dispatch_call_async(call);
            }

            // Disabling the connectors right before installing is the default when the caller did not specify otherwise
            if (disable_connectors_during_install.value_or(true)) {
                this->change_all_connectors_to_unavailable_for_firmware_update();
            }
        }
    }

    // Explicitly allow disabling the connectors when an update is scheduled
    if (firmware_update_status == FirmwareStatusEnum::InstallScheduled and
        disable_connectors_during_install.value_or(false)) {
        this->change_all_connectors_to_unavailable_for_firmware_update();
    }
}

void FirmwareUpdate::on_firmware_status_notification_request() {
    FirmwareStatusNotificationRequest request;

    if (this->firmware_status == FirmwareStatusEnum::Idle or this->firmware_status == FirmwareStatusEnum::Installed) {
        // L01.FR.25
        // do not set requestId when idle: L01.FR.20
        request.status = FirmwareStatusEnum::Idle;
    } else { // L01.FR.26
        // So not Idle or Installed
        request.status = this->firmware_status;
        request.requestId = this->firmware_status_id;
    }

    const ocpp::Call<FirmwareStatusNotificationRequest> call(request);
    this->context.message_dispatcher.dispatch_call(call, true);
}

void FirmwareUpdate::handle_firmware_update_req(Call<UpdateFirmwareRequest> call) {
    EVLOG_debug << "Received UpdateFirmwareRequest: " << call.msg << "\nwith messageId: " << call.uniqueId;
    UpdateFirmwareResponse response;
    const auto msg = call.msg;
    bool cert_valid_or_not_set = true;

    // L01.FR.22 check if certificate is valid
    if (msg.firmware.signingCertificate.has_value() and
        this->context.evse_security.verify_certificate(msg.firmware.signingCertificate.value().get(),
                                                       ocpp::LeafCertificateType::MF) !=
            ocpp::CertificateValidationResult::Valid) {
        response.status = UpdateFirmwareStatusEnum::InvalidCertificate;
        cert_valid_or_not_set = false;
    }

    if (cert_valid_or_not_set) {
        // execute firwmare update callback
        response = update_firmware_request_callback(msg);
    }

    const ocpp::CallResult<UpdateFirmwareResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);

    if ((response.status == UpdateFirmwareStatusEnum::Accepted) or
        (response.status == UpdateFirmwareStatusEnum::AcceptedCanceled)) {
        // Drop what a previous cycle queued behind a running transaction, so a cycle that died without reaching
        // an end state cannot force the EVSE Inoperative once that transaction ends
        this->availability.drop_non_persistent_scheduled_changes();
        if (call.msg.firmware.signingCertificate.has_value() or call.msg.firmware.signature.has_value()) {
            this->firmware_status_before_installing = FirmwareStatusEnum::SignatureVerified;
        } else {
            this->firmware_status_before_installing = FirmwareStatusEnum::Downloaded;
        }

        // Reset the last-seen firmware status to prevent accidental deduplication in a new update cycle
        this->firmware_status = FirmwareStatusEnum::Idle;
        this->firmware_status_id = std::nullopt;
    }

    if ((response.status == UpdateFirmwareStatusEnum::InvalidCertificate) or
        (response.status == UpdateFirmwareStatusEnum::RevokedCertificate)) {
        // L01.FR.02
        this->security.security_event_notification_req(
            CiString<50>(ocpp::security_events::INVALIDFIRMWARESIGNINGCERTIFICATE),
            std::optional<CiString<255>>("Provided signing certificate is not valid!"), true,
            true); // critical because TC_L_05_CS requires this message to be sent
    }
}

void FirmwareUpdate::change_all_connectors_to_unavailable_for_firmware_update() {
    ChangeAvailabilityResponse response;
    response.status = ChangeAvailabilityStatusEnum::Scheduled;

    ChangeAvailabilityRequest msg;
    msg.operationalStatus = OperationalStatusEnum::Inoperative;

    const auto transaction_active = this->context.evse_manager.any_transaction_active(std::nullopt);

    if (!transaction_active) {
        // execute change availability if possible
        for (auto& evse : this->context.evse_manager) {
            if (!evse.has_active_transaction()) {
                set_evse_connectors_unavailable(evse, false);
            }
        }
        // Check succeeded, trigger the callback if needed
        if (this->all_connectors_unavailable_callback.has_value() and
            this->context.evse_manager.are_all_connectors_effectively_inoperative()) {
            this->all_connectors_unavailable_callback.value()();
        }
    } else if (response.status == ChangeAvailabilityStatusEnum::Scheduled) {
        // put all EVSEs to unavailable that do not have active transaction
        for (auto& evse : this->context.evse_manager) {
            if (!evse.has_active_transaction()) {
                set_evse_connectors_unavailable(evse, false);
            } else if (!this->availability.has_persistent_scheduled_change(evse.get_id())) {
                // A CSMS ChangeAvailability already waiting on this transaction wins the slot, because there is
                // only one per EVSE and ours would drop its persist flag and then be erased again at the end of
                // the cycle. TODO: Track what queued a change instead of inferring it from the persist flag
                EVSE e;
                e.id = evse.get_id();
                msg.evse = e;
                this->availability.set_scheduled_change_availability_requests(evse.get_id(), {msg, false});
            }
        }
    }
}

void FirmwareUpdate::restore_all_connector_states() {
    for (auto& evse : this->context.evse_manager) {
        const std::uint32_t number_of_connectors = evse.get_number_of_connectors();

        for (std::uint32_t i = 1; i <= number_of_connectors; ++i) {
            evse.restore_connector_operative_status(static_cast<std::int32_t>(i));
        }
    }
    // The cycle is over, so a change it queued behind a running transaction must not force the EVSE Inoperative
    // once that transaction ends
    this->availability.drop_non_persistent_scheduled_changes();
}

} // namespace ocpp::v2
