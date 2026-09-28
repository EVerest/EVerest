// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "systemImpl.hpp"
#include "diagnostics_handler.hpp"

#include <fstream>

#include <everest/run_application/run_application.hpp>

using namespace everest::run_application;

namespace module {
namespace main {

const std::string CONSTANTS = "constants.env";
const std::string DIAGNOSTICS_UPLOADER = "diagnostics_uploader.sh";

namespace fs = std::filesystem;

// FIXME (aw): this function needs to be refactored into some kind of utility library
fs::path create_temp_file(const fs::path& dir, const std::string& prefix) {
    const std::string fn_template = (dir / prefix).string() + "XXXXXX" + std::string(1, '\0');
    std::vector<char> fn_template_buffer{fn_template.begin(), fn_template.end()};

    // mkstemp needs to have at least 6 XXXXXX at the end and it will replace these
    // with a valid file name
    auto fd = mkstemp(fn_template_buffer.data());

    if (fd == -1) {
        EVLOG_AND_THROW(Everest::EverestBaseRuntimeError("Failed to create temporary file at: " + fn_template));
    }

    // close the file descriptor
    close(fd);

    return fn_template_buffer.data();
}

void systemImpl::init() {
    this->scripts_path = mod->info.paths.libexec;
}

void systemImpl::ready() {
}

types::system::UpdateFirmwareResponse
systemImpl::handle_update_firmware(types::system::FirmwareUpdateRequest& firmware_update_request) {
    // FIXME: implement planned updates at a specific time
    // FIXME: we don't care about the certificate and signature provided as an argument for now.
    // RAUC will not use them anyhow and updates will be equally secure whether they are launched by OCPP secure update
    // mechanism or the old non-secure mechanism.
    if (mod->rauc.is_idle()) {
        EVLOG_info << "Installing bundle from URL: " << firmware_update_request.location;
        this->mod->install_firmware_bundle(firmware_update_request.location, firmware_update_request.request_id);
        return types::system::UpdateFirmwareResponse::Accepted;
    } else {
        return types::system::UpdateFirmwareResponse::Rejected;
    }
}

types::system::UploadLogsResponse
systemImpl::handle_upload_logs(types::system::UploadLogsRequest& upload_logs_request) {
    types::system::UploadLogsResponse response;

    {
        auto state = this->log_upload_state.handle();
        if (*state == LogUploadState::Uploading) {
            EVLOG_info << "Received Log upload request and log upload already running - cancelling current upload";
            this->interrupt_log_upload->store(true);
            response.upload_logs_status = types::system::UploadLogsStatus::AcceptedCanceled;
        } else {
            response.upload_logs_status = types::system::UploadLogsStatus::Accepted;
        }
    }

    const auto date_time = Everest::Date::to_rfc3339(date::utc_clock::now());
    const auto diagnostics_file_path = create_temp_file(fs::temp_directory_path(), "diagnostics-" + date_time);
    const auto diagnostics_file_name = diagnostics_file_path.filename().string();

    response.file_name = diagnostics_file_name;

    // populate file with available logs within the specified time window
    DiagnosticsHandler diag(mod->info.paths.libexec, mod->config.OCPPLogPath, mod->config.SessionLogPath);
    const auto create_result = diag.create_log(diagnostics_file_path.c_str(), upload_logs_request.oldest_timestamp,
                                               upload_logs_request.latest_timestamp);

    this->upload_logs_thread =
        std::thread([this, create_result, upload_logs_request, diagnostics_file_name, diagnostics_file_path]() {
            this->begin_log_upload();
            const auto log_status =
                this->run_log_upload(upload_logs_request, diagnostics_file_name, diagnostics_file_path, create_result);
            this->finish_log_upload(log_status);
            EVLOG_info << "Log upload thread finished";
        });
    this->upload_logs_thread.detach();

    return response;
}

void systemImpl::begin_log_upload() {
    auto state = this->log_upload_state.handle();
    if (*state != LogUploadState::Idle) {
        EVLOG_info << "Waiting for other log upload to finish...";
        state.wait([&state]() { return *state == LogUploadState::Idle; });
        EVLOG_info << "Previous Log upload finished!";
    }
    *state = LogUploadState::Uploading;
    this->interrupt_log_upload->store(false);
}

types::system::LogStatus systemImpl::run_log_upload(const types::system::UploadLogsRequest& upload_logs_request,
                                                    const std::string& diagnostics_file_name,
                                                    const fs::path& diagnostics_file_path,
                                                    DiagnosticsHandler::log_result_t create_result) {
    EVLOG_info << "Starting upload of log file";
    const auto diagnostics_uploader = this->scripts_path / DIAGNOSTICS_UPLOADER;
    const auto constants = this->scripts_path / CONSTANTS;

    std::vector<std::string> args = {constants.string(), upload_logs_request.location, diagnostics_file_name,
                                     diagnostics_file_path.string()};
    int32_t retries = 0;
    const auto total_retries = upload_logs_request.retries.value_or(this->mod->config.DefaultRetries);
    const auto retry_interval =
        std::chrono::seconds(upload_logs_request.retry_interval_s.value_or(this->mod->config.DefaultRetryInterval));

    types::system::LogStatus log_status{types::system::LogStatusEnum::Idle,
                                        upload_logs_request.request_id.value_or(-1)};
    if (create_result == DiagnosticsHandler::log_result_t::error_file) {
        // problem creating the file - nothing to upload
        log_status.log_status = types::system::LogStatusEnum::UploadFailure;
        this->publish_log_status(log_status);
        return log_status;
    }

    RunOptions options;
    options.stop_requested = this->interrupt_log_upload;
    options.callback = [this, &log_status](const std::string& output_line) {
        if (this->interrupt_log_upload->load()) {
            return CmdControl::Terminate;
        }
        if (output_line == "Uploaded") {
            log_status.log_status = types::system::string_to_log_status_enum(output_line);
        } else if (output_line == "UploadFailure" || output_line == "PermissionDenied" || output_line == "BadMessage" ||
                   output_line == "NotSupportedOperation") {
            log_status.log_status = types::system::LogStatusEnum::UploadFailure;
        } else {
            log_status.log_status = types::system::LogStatusEnum::Uploading;
        }
        this->publish_log_status(log_status);
        return CmdControl::Continue;
    };
    while (retries <= total_retries) {
        retries += 1;
        run_application(diagnostics_uploader.string(), args, options);
        if (log_status.log_status == types::system::LogStatusEnum::Uploaded) {
            break;
        }
        // after the last attempt only the interrupt is checked
        const auto wait = retries <= total_retries ? retry_interval : std::chrono::seconds(0);
        if (this->wait_for_log_upload_retry(wait)) {
            break;
        }
    }
    return log_status;
}

bool systemImpl::wait_for_log_upload_retry(std::chrono::seconds interval) {
    if (this->interrupt_log_upload->load()) {
        return true;
    }
    const auto retry_at = std::chrono::steady_clock::now() + interval;
    while (std::chrono::steady_clock::now() < retry_at) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (this->interrupt_log_upload->load()) {
            return true;
        }
    }
    return false;
}

void systemImpl::finish_log_upload(types::system::LogStatus log_status) {
    bool cancelled = false;
    {
        auto state = this->log_upload_state.handle();
        cancelled =
            log_status.log_status != types::system::LogStatusEnum::Uploaded && this->interrupt_log_upload->load();
        *state = LogUploadState::Idle;
    }
    this->log_upload_state.notify_all();
    if (cancelled) {
        EVLOG_info << "Uploading Logs was interrupted, terminating upload script, requestId: " << log_status.request_id;
        log_status.log_status = types::system::LogStatusEnum::AcceptedCanceled;
        this->publish_log_status(log_status);
    }
}

bool systemImpl::handle_is_reset_allowed(types::system::ResetType& type) {
    // Allow resets at any time for now
    return true;
}

void systemImpl::handle_reset(types::system::ResetType& type, bool& scheduled) {
    if (type == types::system::ResetType::Soft) {
        EVLOG_info << "Performing soft reset";
        // This will effectivly stop everest and make it restart via systemd
        exit(255);
    } else {
        EVLOG_info << "Performing hard reset";

        // this reboots the whole linux system
        system("/sbin/reboot");
    }
}

bool systemImpl::handle_set_system_time(std::string& timestamp) {
    // currently not supported, system runs on network time
    return true;
}

types::system::BootReason systemImpl::handle_get_boot_reason() {
    return types::system::BootReason::Unknown;
}

void systemImpl::handle_allow_firmware_installation() {
    EVLOG_info << "Received allow_firmware_installation command - allow firmware update to proceed with reboot.";
    this->mod->firmware_update_may_proceed_with_reboot_callback();
}

types::network::ConfigureNetworkResponse
systemImpl::handle_configure_network(types::network::ConfigureNetworkRequest& request) {
    types::network::ConfigureNetworkResponse response;
    response.status = types::network::ConfigureNetworkStatusEnum::NotSupported;
    return response;
}

} // namespace main
} // namespace module
