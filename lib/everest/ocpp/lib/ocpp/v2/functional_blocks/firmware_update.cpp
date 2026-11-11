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
                               std::optional<AllConnectorsUnavailableCallback> all_connectors_unavailable_callback,
                               std::atomic<RegistrationStatusEnum>& registration_status) :
    context(functional_block_context),
    availability(availability),
    security(security),
    registration_status(registration_status),
    update_firmware_request_callback(update_firmware_request_callback),
    all_connectors_unavailable_callback(all_connectors_unavailable_callback) {
}

void FirmwareUpdate::on_registration_accepted() {
    auto handle = this->pending_status.handle();
    auto& pending = *handle;
    if (pending.has_value()) {
        const ocpp::Call<FirmwareStatusNotificationRequest> call(pending.value());
        this->context.message_dispatcher.dispatch_call_async(call);
        pending.reset();
    }
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
    const bool cycle_ended =
        is_firmware_status_end_state(firmware_update_status) or firmware_update_status == FirmwareStatusEnum::Idle;

    bool is_duplicate = false;
    FirmwareStatusEnum install_pending_status = FirmwareStatusEnum::SignatureVerified;
    {
        auto handle = this->cycle.handle();
        auto& state = *handle;
        state.status_generation += 1;
        if (cycle_ended) {
            state.notification_state = InstallNotificationState::Idle;
        } else if (firmware_update_status != FirmwareStatusEnum::DownloadScheduled and
                   state.notification_state == InstallNotificationState::Idle) {
            // A deferred download keeps the connectors unavailable long before the install, so it does not arm
            // the notification
            state.notification_state = InstallNotificationState::Waiting;
        }
        install_pending_status = status_before_installing(state, request_id);
        if (cycle_ended) {
            state.requested_update_active = false;
        }
        if (state.firmware_status == firmware_update_status) {
            if (request_id == -1 or
                (state.firmware_status_id.has_value() and state.firmware_status_id.value() == request_id)) {
                // already sent, do not send again
                is_duplicate = true;
            }
        }
        if (!is_duplicate) {
            // Firmware status and id are stored for future trigger message request.
            state.firmware_status = firmware_update_status;
            if (request_id != -1) {
                state.firmware_status_id = request_id;
            }
        }
    }

    if (!is_duplicate) {
        FirmwareStatusNotificationRequest req;
        req.status = firmware_update_status;

        if (request_id != -1) {
            req.requestId = request_id; // L01.FR.20
        }

        {
            auto handle = this->pending_status.handle();
            auto& pending = *handle;
            if (this->registration_status == RegistrationStatusEnum::Accepted) {
                const ocpp::Call<FirmwareStatusNotificationRequest> call(req);
                this->context.message_dispatcher.dispatch_call_async(call);
                pending.reset();
            } else {
                pending = req;
            }
        }

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

        if (install_pending_status == req.status) {
            // FIXME(Kai): This is a temporary workaround, because the EVerest System module does not keep track of
            // transactions and can't inquire about their status from the OCPP modules. If the firmware status is
            // expected to become "Installing", but we still have a transaction running, the update will wait for the
            // transaction to finish, and so we send an "InstallScheduled" status. This is necessary for OCTT TC_L_15_CS
            // to pass.
            const auto transaction_active = this->context.evse_manager.any_transaction_active(std::nullopt);
            if (transaction_active) {
                this->cycle.handle()->firmware_status = FirmwareStatusEnum::InstallScheduled;
                req.status = FirmwareStatusEnum::InstallScheduled;
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

    {
        auto handle = this->cycle.handle();
        if (handle->firmware_status == FirmwareStatusEnum::Idle or
            handle->firmware_status == FirmwareStatusEnum::Installed) {
            // L01.FR.25
            // do not set requestId when idle: L01.FR.20
            request.status = FirmwareStatusEnum::Idle;
        } else { // L01.FR.26
            // So not Idle or Installed
            request.status = handle->firmware_status;
            request.requestId = handle->firmware_status_id;
        }
    }

    const ocpp::Call<FirmwareStatusNotificationRequest> call(request);
    this->context.message_dispatcher.dispatch_call(call, true);
}

void FirmwareUpdate::on_transaction_finished() {
    std::optional<UpdateFirmwareRequest> request;
    {
        auto handle = this->deferred_update_firmware_request.handle();
        auto& deferred = *handle;
        if (deferred.has_value() and !this->context.evse_manager.any_transaction_active(std::nullopt)) {
            request = std::move(deferred);
            deferred.reset();
        }
    }
    if (request.has_value()) {
        const UpdateFirmwareResponse response = this->request_firmware_update(request.value());
        if (response.status != UpdateFirmwareStatusEnum::Accepted) {
            EVLOG_warning << "Deferred firmware update was not accepted after transaction finished: "
                          << conversions::update_firmware_status_enum_to_string(response.status);
            this->on_firmware_update_status_notification(request.value().requestId, FirmwareStatusEnum::DownloadFailed);
        }
    }
}

void FirmwareUpdate::on_all_connectors_unavailable() {
    if (!this->all_connectors_unavailable_callback.has_value()) {
        return;
    }
    {
        auto handle = this->cycle.handle();
        if (handle->notification_state != InstallNotificationState::Waiting) {
            return;
        }
        handle->notification_state = InstallNotificationState::Notified;
        handle->notified_generation = handle->status_generation;
    }
    this->all_connectors_unavailable_callback.value()();
}

FirmwareUpdate::RequestedUpdate FirmwareUpdate::to_requested_update(const UpdateFirmwareRequest& request) {
    const bool is_signed = request.firmware.signingCertificate.has_value() or request.firmware.signature.has_value();
    return {request.requestId, is_signed ? FirmwareStatusEnum::SignatureVerified : FirmwareStatusEnum::Downloaded};
}

UpdateFirmwareResponse FirmwareUpdate::request_firmware_update(const UpdateFirmwareRequest& request) {
    std::uint64_t generation_before_callback = 0;
    {
        auto handle = this->cycle.handle();
        generation_before_callback = handle->status_generation;
        // Known before the callback returns, so a status reported from within it is classified by this request
        handle->offered_update = to_requested_update(request);
    }
    const auto response = this->update_firmware_request_callback(request);

    auto handle = this->cycle.handle();
    handle->offered_update.reset();
    if (response.status == UpdateFirmwareStatusEnum::Accepted or
        response.status == UpdateFirmwareStatusEnum::AcceptedCanceled) {
        const bool notified_by_this_update = handle->notification_state == InstallNotificationState::Notified and
                                             handle->notified_generation > generation_before_callback;
        if (!notified_by_this_update) {
            handle->notification_state = InstallNotificationState::Waiting;
        }
    }
    return response;
}

FirmwareStatusEnum FirmwareUpdate::status_before_installing(const CycleState& state, const std::int32_t request_id) {
    if (state.offered_update.has_value() and request_id == state.offered_update->request_id) {
        return state.offered_update->status_before_installing;
    }
    if (state.requested_update.has_value() and
        (request_id == state.requested_update->request_id or (request_id == -1 and state.requested_update_active))) {
        return state.requested_update->status_before_installing;
    }
    // An update the CSMS did not request: one reported without a request id is taken as unsigned
    return request_id == -1 ? FirmwareStatusEnum::Downloaded : FirmwareStatusEnum::SignatureVerified;
}

bool FirmwareUpdate::is_download_deferral_required() const {
    return this->context.device_model
               .get_optional_value<bool>(ControllerComponentVariables::DeferFirmwareDownloadDuringTransaction)
               .value_or(false) and
           this->context.evse_manager.any_transaction_active(std::nullopt);
}

void FirmwareUpdate::handle_firmware_update_req(Call<UpdateFirmwareRequest> call) {
    EVLOG_debug << "Received UpdateFirmwareRequest with messageId: " << call.uniqueId;
    UpdateFirmwareResponse response;
    const auto msg = call.msg;
    bool cert_valid_or_not_set = true;
    const auto generation_before_callback = this->cycle.handle()->status_generation;

    // L01.FR.22 check if certificate is valid
    if (msg.firmware.signingCertificate.has_value() and
        this->context.evse_security.verify_certificate(msg.firmware.signingCertificate.value().get(),
                                                       ocpp::LeafCertificateType::MF) !=
            ocpp::CertificateValidationResult::Valid) {
        response.status = UpdateFirmwareStatusEnum::InvalidCertificate;
        cert_valid_or_not_set = false;
    }

    bool download_deferred = false;
    if (cert_valid_or_not_set) {
        bool deferral_required = this->is_download_deferral_required();
        bool superseded_deferred_request = false;
        {
            // Stash and the on_transaction_finished release must not interleave, otherwise a
            // transaction that ends between the deferral check and the stash write would leave the
            // request stuck forever. Re-check under the lock and release it if that happened.
            auto handle = this->deferred_update_firmware_request.handle();
            auto& deferred = *handle;
            // A newer request supersedes a deferred one, which is a firmware update the station was going to
            // install at a later time.
            superseded_deferred_request = deferred.has_value();
            deferred.reset();
            if (deferral_required) {
                deferred = msg;
                if (!this->context.evse_manager.any_transaction_active(std::nullopt)) {
                    deferred.reset();
                    deferral_required = false;
                }
            }
        }
        if (deferral_required) {
            // L01.FR.13: busy charging, defer the download until the last transaction ends.
            response.status = UpdateFirmwareStatusEnum::Accepted;
            download_deferred = true;
        } else {
            response = this->request_firmware_update(msg);
        }
        if (superseded_deferred_request and response.status == UpdateFirmwareStatusEnum::Accepted) {
            // L01.FR.24: the previously deferred update is canceled by this one. Only the response reports it; the
            // spec leaves a DownloadFailed notification for the canceled request optional, and sending one here
            // would reach an end state and restore connectors that a re-deferral is about to disable again.
            response.status = UpdateFirmwareStatusEnum::AcceptedCanceled;
        }
    }

    const ocpp::CallResult<UpdateFirmwareResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);

    if ((response.status == UpdateFirmwareStatusEnum::Accepted) or
        (response.status == UpdateFirmwareStatusEnum::AcceptedCanceled)) {
        bool restore_connectors = false;
        {
            auto handle = this->cycle.handle();
            auto& state = *handle;
            state.requested_update = to_requested_update(msg);
            state.requested_update_active = true;
            // A status reported since the callback started already belongs to the accepted update and is kept
            if (state.status_generation == generation_before_callback) {
                // Reset the last-seen firmware status to prevent accidental deduplication in a new update cycle
                state.firmware_status = FirmwareStatusEnum::Idle;
                state.firmware_status_id = std::nullopt;
                restore_connectors = state.connectors_disabled;
            }
            if (download_deferred) {
                state.notification_state = InstallNotificationState::Idle;
            }
        }
        // An update that never reported its end must not keep the connectors unavailable, nor force an EVSE
        // Inoperative once its transaction ends
        if (restore_connectors) {
            this->restore_all_connector_states();
        } else {
            this->availability.drop_non_persistent_scheduled_changes();
        }
    }

    if (download_deferred) {
        this->on_firmware_update_status_notification(msg.requestId, FirmwareStatusEnum::DownloadScheduled);
        this->change_all_connectors_to_unavailable_for_firmware_update();
    } else if ((response.status == UpdateFirmwareStatusEnum::InvalidCertificate) or
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
    this->cycle.handle()->connectors_disabled = true;

    if (!transaction_active) {
        // execute change availability if possible
        for (auto& evse : this->context.evse_manager) {
            if (!evse.has_active_transaction()) {
                set_evse_connectors_unavailable(evse, false);
            }
        }
        if (this->context.evse_manager.are_all_connectors_effectively_inoperative()) {
            this->on_all_connectors_unavailable();
        }
    } else if (response.status == ChangeAvailabilityStatusEnum::Scheduled) {
        // put all EVSEs to unavailable that do not have active transaction
        for (auto& evse : this->context.evse_manager) {
            if (!evse.has_active_transaction()) {
                set_evse_connectors_unavailable(evse, false);
            } else {
                EVSE e;
                e.id = evse.get_id();
                msg.evse = e;
                // Queued alongside whatever the CSMS has scheduled for this EVSE
                this->availability.set_scheduled_change_availability_requests(evse.get_id(), {msg, false});
            }
        }
    }
}

void FirmwareUpdate::restore_all_connector_states() {
    this->cycle.handle()->connectors_disabled = false;
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
