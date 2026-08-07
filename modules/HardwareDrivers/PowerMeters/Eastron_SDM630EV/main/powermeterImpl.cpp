// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "powermeterImpl.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <string>

#include <date/date.h>
#include <fmt/core.h>
#include <utils/date.hpp>

#include "everest/logging.hpp"
#include "helper.hpp"
#include "registers.hpp"

namespace module {
namespace main {

namespace {

namespace regs = sdm630ev::registers;

constexpr auto SIGNATURE_POLL_INTERVAL = std::chrono::milliseconds{150};
constexpr auto CHARGING_STATUS_POLL_INTERVAL = std::chrono::milliseconds{100};
constexpr int CHARGING_STATUS_POLL_RETRIES = 20;

[[nodiscard]] types::powermeter::TransactionStartResponse
make_start_response(types::powermeter::TransactionRequestStatus status, std::string error = {}) {
    types::powermeter::TransactionStartResponse response;
    response.status = status;
    if (not error.empty()) {
        response.error = std::move(error);
    }
    return response;
}

[[nodiscard]] types::powermeter::TransactionStopResponse
make_stop_response(types::powermeter::TransactionRequestStatus status, std::string error = {}) {
    types::powermeter::TransactionStopResponse response;
    response.status = status;
    if (not error.empty()) {
        response.error = std::move(error);
    }
    return response;
}

} // namespace

powermeterImpl::~powermeterImpl() {
    stop_threads();
}

void powermeterImpl::init() {
    p_modbus_transport =
        std::make_unique<transport::SerialCommHubTransport>(*mod->r_modbus, config.powermeter_device_id);
}

void powermeterImpl::ready() {
    live_measure_thread_ = std::thread([this] {
        bool configured = false;
        while (!stop_requested_.load()) {
            try {
                if (!configured) {
                    configure_device();
                    configured = true;
                }
                read_powermeter_values();
                if (m_transaction_active.load()) {
                    monitor_charging_status(read_charging_status());
                }
                clear_communication_fault();
            } catch (const std::exception& e) {
                EVLOG_error << "Failed to communicate with the device, retry in "
                            << config.communication_error_pause_delay_s << " s: " << e.what();
                raise_communication_fault(e.what());
                configured = false;
                std::unique_lock<std::mutex> lock(stop_mutex_);
                stop_cv_.wait_for(lock, std::chrono::seconds{config.communication_error_pause_delay_s},
                                  [this] { return stop_requested_.load(); });
            }
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_cv_.wait_for(lock, std::chrono::milliseconds{config.live_measurement_interval_ms},
                              [this] { return stop_requested_.load(); });
        }
    });

    time_sync_thread_ = std::thread([this] { time_sync_loop(); });
}

void powermeterImpl::shutdown() {
    stop_threads();
}

void powermeterImpl::stop_threads() {
    stop_requested_.store(true);
    stop_cv_.notify_all();
    if (live_measure_thread_.joinable()) {
        live_measure_thread_.join();
    }
    if (time_sync_thread_.joinable()) {
        time_sync_thread_.join();
    }
}

void powermeterImpl::configure_device() {
    EVLOG_info << "Configuring the device...";

    read_public_key();

    p_modbus_transport->write_multiple_registers(regs::SIGNATURE_FORMAT_ADDRESS, {regs::SIGNATURE_FORMAT_HEX_ASN1});
    p_modbus_transport->write_multiple_registers(regs::SIGNATURE_ALGORITHM_ADDRESS,
                                                 {regs::SIGNATURE_ALGORITHM_ECDSA_SECP256R1});
    const auto algorithm_data =
        p_modbus_transport->fetch(regs::SIGNATURE_ALGORITHM_ADDRESS, 1, transport::RegisterType::Holding);
    if (modbus_utils::to_uint16(algorithm_data, 0) != regs::SIGNATURE_ALGORITHM_ECDSA_SECP256R1) {
        raise_vendor_error("SignatureNotConfigured",
                           "Device did not accept ECDSA-secp256r1-SHA256, it cannot sign transactions");
    } else {
        clear_vendor_error("SignatureNotConfigured");
    }

    // Both must be set before any charging starts, otherwise the device
    // reports 'invalid date time' when signing at the end of the session.
    set_timezone();
    synchronize_time();

    apply_charging_status_on_configure(read_charging_status());
    EVLOG_info << "Device configured";
}

void powermeterImpl::read_public_key() {
    const auto key_data = p_modbus_transport->fetch(regs::PUBLIC_KEY_ADDRESS, regs::PUBLIC_KEY_WORD_COUNT,
                                                    transport::RegisterType::Holding);
    const std::string key_hex = modbus_utils::to_hex_string(key_data, 0, key_data.size());
    const char* prefix =
        (config.public_key_format == "der") ? ocmf::PUBLIC_KEY_DER_PREFIX : ocmf::PUBLIC_KEY_RAW_PREFIX;
    m_public_key_hex = prefix + key_hex;
    EVLOG_info << "Public key (" << config.public_key_format << ", hex): " << m_public_key_hex;
    publish_public_key_ocmf(m_public_key_hex);
}

void powermeterImpl::set_timezone() {
    const float offset_hours = static_cast<float>(config.timezone_offset_minutes) / 60.0F;
    p_modbus_transport->write_multiple_registers(regs::TIMEZONE_ADDRESS,
                                                 modbus_utils::float_to_registers(offset_hours));
    EVLOG_info << "Timezone set to " << offset_hours << " hours";
}

void powermeterImpl::synchronize_time() {
    const std::time_t local_now = std::time(nullptr) + static_cast<std::time_t>(config.timezone_offset_minutes) * 60;
    std::tm local_tm{};
    gmtime_r(&local_now, &local_tm);
    p_modbus_transport->write_multiple_registers(regs::TIME_BCD_ADDRESS, bcd_utils::time_to_registers(local_tm));
    EVLOG_info << "Time synchronized to "
               << fmt::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", local_tm.tm_year + 1900, local_tm.tm_mon + 1,
                              local_tm.tm_mday, local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
}

void powermeterImpl::time_sync_loop() {
    const auto sync_interval = std::chrono::hours{1};
    while (!stop_requested_.load()) {
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_cv_.wait_for(lock, sync_interval, [this] { return stop_requested_.load(); });
        }
        if (stop_requested_.load()) {
            break;
        }
        if (m_transaction_active.load()) {
            EVLOG_info << "Time synchronization deferred: charging session in progress";
            continue;
        }
        try {
            synchronize_time();
        } catch (const std::exception& e) {
            EVLOG_warning << "Periodic time synchronization failed: " << e.what();
        }
    }
}

std::uint16_t powermeterImpl::read_charging_status() {
    const auto data = p_modbus_transport->fetch(regs::CHARGING_STATUS_ADDRESS, 1, transport::RegisterType::Holding);
    return modbus_utils::to_uint16(data, 0);
}

void powermeterImpl::apply_charging_status_on_configure(std::uint16_t status) {
    if (status == regs::CHARGING_STATUS_IN_PROGRESS) {
        EVLOG_info << "Device reports a running charging session, waiting for a stop transaction command";
        m_transaction_active.store(true);
    } else if (status == regs::CHARGING_STATUS_POWER_LOSS || status == regs::CHARGING_STATUS_RESET) {
        EVLOG_warning << "Device reports an interrupted charging session (status " << status << ")";
        m_transaction_active.store(true);
        m_pending_closed_transaction.store(true);
        raise_vendor_error("OcmfTransactionInterrupted",
                           fmt::format("Charging session was interrupted (device status {})", status));
    }
}

void powermeterImpl::monitor_charging_status(std::uint16_t status) {
    if (m_pending_closed_transaction.load()) {
        return;
    }
    if (status != regs::CHARGING_STATUS_POWER_LOSS && status != regs::CHARGING_STATUS_RESET) {
        return;
    }
    m_pending_closed_transaction.store(true);
    raise_vendor_error("OcmfTransactionInterrupted",
                       fmt::format("Charging session was interrupted (device status {})", status));
}

void powermeterImpl::write_identification_registers(const types::powermeter::TransactionReq& req) {
    // One FC16 write per parameter: the device rejects multi-parameter writes.
    p_modbus_transport->write_multiple_registers(regs::OCMF_IS_ADDRESS, {ocmf::is_to_value(req.identification_status)});

    p_modbus_transport->write_multiple_registers(
        regs::OCMF_IF_ADDRESS, modbus_utils::uint32_to_registers(ocmf::flags_to_bitfield(req.identification_flags)));

    p_modbus_transport->write_multiple_registers(regs::OCMF_IT_ADDRESS, {ocmf::type_to_value(req.identification_type)});

    p_modbus_transport->write_multiple_registers(
        regs::OCMF_ID_ADDRESS, modbus_utils::string_to_fixed_registers(req.identification_data.value_or(""),
                                                                       regs::OCMF_TEXT_FIELD_WORD_COUNT));

    p_modbus_transport->write_multiple_registers(
        regs::OCMF_CT_ADDRESS, {ocmf::charge_point_id_type_to_value(config.ocmf_charge_point_identification_type)});

    const std::string& charge_point_id =
        config.ocmf_charge_point_identification.empty() ? req.evse_id : config.ocmf_charge_point_identification;
    p_modbus_transport->write_multiple_registers(
        regs::OCMF_CI_ADDRESS,
        modbus_utils::string_to_fixed_registers(charge_point_id, regs::OCMF_TEXT_FIELD_WORD_COUNT));

    if (req.identification_level.has_value()) {
        EVLOG_debug << "identification_level is not supported by this device and will be ignored";
    }
    if (req.tariff_text.has_value()) {
        EVLOG_debug << "tariff_text is not supported by this device and will be ignored";
    }
}

void powermeterImpl::send_charge_control(std::uint16_t command) {
    p_modbus_transport->write_multiple_registers(regs::CHARGE_CONTROL_ADDRESS, {command});
}

void powermeterImpl::wait_for_signature() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{config.ocmf_signature_timeout_ms};
    while (true) {
        const auto data =
            p_modbus_transport->fetch(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding);
        const std::uint16_t status = modbus_utils::to_uint16(data, 0);
        if (status == regs::SIGNATURE_STATUS_OK) {
            return;
        }
        if (status != regs::SIGNATURE_STATUS_IDLE && status != regs::SIGNATURE_STATUS_IN_PROGRESS &&
            status != regs::SIGNATURE_STATUS_NOT_INITIALISED) {
            throw std::runtime_error("Signing failed: " + ocmf::signature_status_to_string(status));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("Timeout waiting for signature, last status: " +
                                     ocmf::signature_status_to_string(status));
        }
        std::this_thread::sleep_for(SIGNATURE_POLL_INTERVAL);
    }
}

std::string powermeterImpl::read_ocmf_document() {
    const auto length_data =
        p_modbus_transport->fetch(regs::OCMF_JSON_LENGTH_ADDRESS, 1, transport::RegisterType::Holding);
    const std::uint16_t json_length = modbus_utils::to_uint16(length_data, 0);
    if (json_length == 0) {
        throw std::runtime_error("OCMF output message length is 0");
    }

    const auto json_data =
        p_modbus_transport->fetch(regs::OCMF_JSON_DATA_ADDRESS, static_cast<std::uint16_t>((json_length + 1) / 2),
                                  transport::RegisterType::Holding);
    const std::string payload(json_data.begin(), json_data.begin() + json_length);

    const auto signature_length_data =
        p_modbus_transport->fetch(regs::SIGNATURE_LENGTH_ADDRESS, 1, transport::RegisterType::Holding);
    const std::uint16_t signature_length = modbus_utils::to_uint16(signature_length_data, 0);
    std::string signature_hex;
    if (signature_length > 0) {
        const auto signature_data = p_modbus_transport->fetch(regs::SIGNATURE_DATA_ADDRESS,
                                                              static_cast<std::uint16_t>((signature_length + 1) / 2),
                                                              transport::RegisterType::Holding);
        signature_hex = modbus_utils::to_hex_string(signature_data, 0, signature_length);
    }

    return ocmf::assemble_ocmf(payload, signature_hex);
}

void powermeterImpl::clear_transaction_states() {
    try {
        send_charge_control(regs::CHARGE_CONTROL_END);
        wait_for_signature();
        static_cast<void>(read_ocmf_document());
        EVLOG_warning << "Discarded a pending OCMF document during cleanup";
    } catch (const std::exception& e) {
        EVLOG_warning << "Best-effort transaction cleanup: " << e.what();
    }
    m_transaction_active.store(false);
    m_pending_closed_transaction.store(false);
    clear_vendor_error("OcmfTransactionInterrupted");
}

types::powermeter::TransactionStartResponse
powermeterImpl::handle_start_transaction(types::powermeter::TransactionReq& value) {
    try {
        EVLOG_info << "Starting transaction " << value.transaction_id << " on evse " << value.evse_id;

        if (read_charging_status() != regs::CHARGING_STATUS_IDLE) {
            EVLOG_warning << "Device has an open charging session at transaction start, cleaning up. "
                             "Billing data of the previous session is discarded; call stop_transaction "
                             "first if it matters.";
            clear_transaction_states();
        }

        synchronize_time();
        write_identification_registers(value);
        send_charge_control(regs::CHARGE_CONTROL_BEGIN);

        // Confirm the meter accepted the begin command.
        int retries = 0;
        while (read_charging_status() != regs::CHARGING_STATUS_IN_PROGRESS) {
            if (++retries > CHARGING_STATUS_POLL_RETRIES) {
                throw std::runtime_error("Device did not enter 'charge in progress' after begin command");
            }
            std::this_thread::sleep_for(CHARGING_STATUS_POLL_INTERVAL);
        }

        {
            std::lock_guard<std::mutex> lock(m_transaction_mutex);
            m_transaction_id = value.transaction_id;
        }
        m_transaction_active.store(true);
        m_pending_closed_transaction.store(false);
        clear_vendor_error("OcmfTransactionInterrupted");

        // The device produces one OCMF document covering begin and end, so
        // there is no separate signed meter value at transaction start.
        return make_start_response(types::powermeter::TransactionRequestStatus::OK);
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to start transaction: " << e.what();
        return make_start_response(types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR,
                                   fmt::format("can't start transaction: {}", e.what()));
    }
}

types::powermeter::TransactionStopResponse powermeterImpl::handle_stop_transaction(std::string& transaction_id) {
    EVLOG_info << "Stopping transaction " << (transaction_id.empty() ? "(cleanup)" : transaction_id);

    // An empty id is a cleanup request (e.g. EvseManager startup).
    if (transaction_id.empty()) {
        try {
            if (m_transaction_active.load() && read_charging_status() != regs::CHARGING_STATUS_IDLE) {
                clear_transaction_states();
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Transaction cleanup failed: " << e.what();
        }
        m_transaction_active.store(false);
        m_pending_closed_transaction.store(false);
        clear_vendor_error("OcmfTransactionInterrupted");
        return make_stop_response(types::powermeter::TransactionRequestStatus::OK);
    }

    try {
        if (m_pending_closed_transaction.load()) {
            // Recovery after power loss/reset. The device has no register that
            // could carry the EVerest transaction id, so it cannot be matched
            // against the requested id; the pending document is returned as is.
            EVLOG_warning << "Returning OCMF document of an interrupted session; the transaction id "
                             "cannot be verified on this device";
            send_charge_control(regs::CHARGE_CONTROL_END);
            wait_for_signature();
            const std::string ocmf_document = read_ocmf_document();

            m_transaction_active.store(false);
            m_pending_closed_transaction.store(false);
            clear_vendor_error("OcmfTransactionInterrupted");

            auto signed_meter_value = types::units_signed::SignedMeterValue{ocmf_document, "", "OCMF"};
            signed_meter_value.public_key = m_public_key_hex;
            signed_meter_value.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
            return types::powermeter::TransactionStopResponse{
                types::powermeter::TransactionRequestStatus::OK, {}, signed_meter_value};
        }

        std::string active_transaction_id;
        {
            std::lock_guard<std::mutex> lock(m_transaction_mutex);
            active_transaction_id = m_transaction_id;
        }
        if (!m_transaction_active.load() || active_transaction_id != transaction_id) {
            EVLOG_error << "No open transaction or unknown transaction id: " << transaction_id;
            return make_stop_response(types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR,
                                      "No open transaction or unknown transaction id");
        }

        send_charge_control(regs::CHARGE_CONTROL_END);
        wait_for_signature();
        const std::string ocmf_document = read_ocmf_document();

        m_transaction_active.store(false);
        m_pending_closed_transaction.store(false);
        clear_vendor_error("OcmfTransactionInterrupted");
        EVLOG_info << "Transaction " << transaction_id << " stopped";

        auto signed_meter_value = types::units_signed::SignedMeterValue{ocmf_document, "", "OCMF"};
        signed_meter_value.public_key = m_public_key_hex;
        signed_meter_value.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
        return types::powermeter::TransactionStopResponse{
            types::powermeter::TransactionRequestStatus::OK, {}, signed_meter_value};
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to stop transaction: " << e.what();
        return make_stop_response(types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR,
                                  fmt::format("can't stop transaction: {}", e.what()));
    }
}

void powermeterImpl::read_powermeter_values() {
    namespace live = regs::live_offsets;
    namespace energy = regs::energy_offsets;
    namespace phase = regs::phase_energy_offsets;

    const auto data = p_modbus_transport->fetch(regs::LIVE_BLOCK_ADDRESS, regs::LIVE_BLOCK_REGISTER_COUNT,
                                                transport::RegisterType::Input);
    const auto energy_data = p_modbus_transport->fetch(regs::ENERGY_BLOCK_ADDRESS, regs::ENERGY_BLOCK_REGISTER_COUNT,
                                                       transport::RegisterType::Input);
    const auto phase_energy_data = p_modbus_transport->fetch(
        regs::PHASE_ENERGY_BLOCK_ADDRESS, regs::PHASE_ENERGY_BLOCK_REGISTER_COUNT, transport::RegisterType::Input);

    types::powermeter::Powermeter powermeter{};
    powermeter.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    if (!config.meter_id.empty()) {
        powermeter.meter_id = config.meter_id;
    }

    types::units::Voltage voltage_V;
    voltage_V.L1 = modbus_utils::to_float32(data, live::V_L1);
    voltage_V.L2 = modbus_utils::to_float32(data, live::V_L2);
    voltage_V.L3 = modbus_utils::to_float32(data, live::V_L3);
    powermeter.voltage_V = voltage_V;

    types::units::Current current_A;
    current_A.L1 = modbus_utils::to_float32(data, live::A_L1);
    current_A.L2 = modbus_utils::to_float32(data, live::A_L2);
    current_A.L3 = modbus_utils::to_float32(data, live::A_L3);
    powermeter.current_A = current_A;

    types::units::Power power_W;
    power_W.L1 = modbus_utils::to_float32(data, live::W_L1);
    power_W.L2 = modbus_utils::to_float32(data, live::W_L2);
    power_W.L3 = modbus_utils::to_float32(data, live::W_L3);
    power_W.total = modbus_utils::to_float32(data, live::W_TOTAL);
    powermeter.power_W = power_W;

    types::units::ReactivePower VAR;
    VAR.L1 = modbus_utils::to_float32(data, live::VAR_L1);
    VAR.L2 = modbus_utils::to_float32(data, live::VAR_L2);
    VAR.L3 = modbus_utils::to_float32(data, live::VAR_L3);
    VAR.total = modbus_utils::to_float32(data, live::VAR_TOTAL);
    powermeter.VAR = VAR;

    types::units::Frequency frequency_Hz;
    frequency_Hz.L1 = modbus_utils::to_float32(data, live::FREQUENCY);
    powermeter.frequency_Hz = frequency_Hz;

    // Device reports kWh, the powermeter type wants Wh.
    constexpr float KWH_TO_WH = 1000.0F;
    powermeter.energy_Wh_import.total = KWH_TO_WH * modbus_utils::to_float32(energy_data, energy::IMPORT_KWH);
    powermeter.energy_Wh_import.L1 = KWH_TO_WH * modbus_utils::to_float32(phase_energy_data, phase::IMPORT_L1);
    powermeter.energy_Wh_import.L2 = KWH_TO_WH * modbus_utils::to_float32(phase_energy_data, phase::IMPORT_L2);
    powermeter.energy_Wh_import.L3 = KWH_TO_WH * modbus_utils::to_float32(phase_energy_data, phase::IMPORT_L3);

    types::units::Energy energy_Wh_export;
    energy_Wh_export.total = KWH_TO_WH * modbus_utils::to_float32(energy_data, energy::EXPORT_KWH);
    energy_Wh_export.L1 = KWH_TO_WH * modbus_utils::to_float32(phase_energy_data, phase::EXPORT_L1);
    energy_Wh_export.L2 = KWH_TO_WH * modbus_utils::to_float32(phase_energy_data, phase::EXPORT_L2);
    energy_Wh_export.L3 = KWH_TO_WH * modbus_utils::to_float32(phase_energy_data, phase::EXPORT_L3);
    powermeter.energy_Wh_export = energy_Wh_export;

    publish_powermeter(powermeter);
}

void powermeterImpl::raise_communication_fault(const std::string& message) {
    if (error_state_monitor == nullptr || error_factory == nullptr) {
        return;
    }
    if (!error_state_monitor->is_error_active("powermeter/CommunicationFault", "CommunicationError")) {
        raise_error(error_factory->create_error("powermeter/CommunicationFault", "CommunicationError", message,
                                                Everest::error::Severity::High));
    }
}

void powermeterImpl::clear_communication_fault() {
    if (error_state_monitor == nullptr) {
        return;
    }
    if (error_state_monitor->is_error_active("powermeter/CommunicationFault", "CommunicationError")) {
        EVLOG_info << "Communication restored";
        clear_error("powermeter/CommunicationFault", "CommunicationError");
    }
}

void powermeterImpl::raise_vendor_error(const std::string& sub_type, const std::string& message) {
    EVLOG_error << message;
    if (error_state_monitor == nullptr || error_factory == nullptr) {
        return;
    }
    if (!error_state_monitor->is_error_active("powermeter/VendorError", sub_type)) {
        raise_error(error_factory->create_error("powermeter/VendorError", sub_type, message));
    }
}

void powermeterImpl::clear_vendor_error(const std::string& sub_type) {
    if (error_state_monitor == nullptr) {
        return;
    }
    if (error_state_monitor->is_error_active("powermeter/VendorError", sub_type)) {
        clear_error("powermeter/VendorError", sub_type);
    }
}

} // namespace main
} // namespace module
