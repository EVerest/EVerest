// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "evse_board_support_API.hpp"

#include <everest_api_types/evse_board_support/API.hpp>
#include <everest_api_types/evse_board_support/codec.hpp>
#include <everest_api_types/evse_board_support/wrapper.hpp>
#include <everest_api_types/evse_manager/API.hpp>
#include <everest_api_types/evse_manager/codec.hpp>
#include <everest_api_types/evse_manager/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include "utils/error.hpp"

namespace module {

namespace API_evse_manager = API_types::evse_manager;
namespace API_generic = API_types::generic;
using ev_API::deserialize;

namespace {

bool is_phase_count(int value) {
    return value >= 1 and value <= 3;
}

// The schema limits every phase count to 1..3 and a minimum must not exceed its maximum
bool phase_counts_valid(API_types_ext::HardwareCapabilities const& caps) {
    return is_phase_count(caps.max_phase_count_import) and is_phase_count(caps.min_phase_count_import) and
           is_phase_count(caps.max_phase_count_export) and is_phase_count(caps.min_phase_count_export) and
           caps.min_phase_count_import <= caps.max_phase_count_import and
           caps.min_phase_count_export <= caps.max_phase_count_export;
}

} // namespace

void evse_board_support_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_rcd);
    invoke_init(*p_connector_lock);

    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    comm_params.request_reply_timeout_s = config.cfg_request_reply_to_s;
    helper.init(comm_params);
}

void evse_board_support_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_rcd);
    invoke_ready(*p_connector_lock);

    generate_api_var_event();
    generate_api_var_ac_nr_of_phases();
    generate_api_var_capabilities();
    generate_api_var_ac_pp_ampacity();
    generate_api_var_request_stop_transaction();
    generate_api_var_rcd_current();

    generate_api_var_raise_error();
    generate_api_var_clear_error();

    helper.generate_api_var_communication_check(&comm_check);
    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);
    helper.publish_ready_beacon();
}

void evse_board_support_API::generate_api_var_event() {
    helper.subscribe_api_topic("event", [=](std::string const& data) {
        API_types_ext::BspEvent ext;
        if (deserialize(data, ext)) {
            p_main->publish_event(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_ac_nr_of_phases() {
    helper.subscribe_api_topic("ac_nr_of_phases", [=](std::string const& data) {
        int ac_nr_of_phases_available = 0;
        if (deserialize(data, ac_nr_of_phases_available)) {
            if (not is_phase_count(ac_nr_of_phases_available)) {
                EVLOG_warning << "Ignoring ac_nr_of_phases " << ac_nr_of_phases_available << ": must be 1 to 3";
                return true;
            }
            p_main->publish_ac_nr_of_phases_available(ac_nr_of_phases_available);
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_capabilities() {
    helper.subscribe_api_topic("capabilities", [=](std::string const& data) {
        API_types_ext::HardwareCapabilities ext;
        if (deserialize(data, ext)) {
            if (not phase_counts_valid(ext)) {
                EVLOG_warning
                    << "Ignoring capabilities: phase counts must be 1 to 3 with minimum <= maximum, got import "
                    << ext.min_phase_count_import << ".." << ext.max_phase_count_import << ", export "
                    << ext.min_phase_count_export << ".." << ext.max_phase_count_export;
                return true;
            }
            p_main->publish_capabilities(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_ac_pp_ampacity() {
    helper.subscribe_api_topic("ac_pp_ampacity", [=](std::string const& data) {
        API_types_ext::ProximityPilot ext;
        if (deserialize(data, ext)) {
            p_main->publish_ac_pp_ampacity(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_request_stop_transaction() {
    helper.subscribe_api_topic("request_stop_transaction", [=](std::string const& data) {
        API_evse_manager::StopTransactionRequest ext;
        if (deserialize(data, ext)) {
            p_main->publish_request_stop_transaction(to_internal_api(ext));
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_rcd_current() {
    helper.subscribe_api_topic("rcd_current", [=](std::string const& data) {
        double rcd_current;
        if (deserialize(data, rcd_current)) {
            p_rcd->publish_rcd_current_mA(rcd_current);
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_raise_error() {
    helper.subscribe_api_topic("raise_error", [=](std::string const& data) {
        API_types_ext::Error error;
        if (deserialize(data, error)) {
            auto handler = make_error_handler(error);
            handler.raiser();
            return true;
        }
        return false;
    });
}

void evse_board_support_API::generate_api_var_clear_error() {
    helper.subscribe_api_topic("clear_error", [=](std::string const& data) {
        API_types_ext::Error error;
        if (deserialize(data, error)) {
            auto handler = make_error_handler(error);
            handler.clearer();
            return true;
        }
        return false;
    });
}

evse_board_support_API::ErrorHandler evse_board_support_API::make_error_handler(API_types_ext::Error const& error) {
    using namespace API_types_ext;
    auto error_str = API_generic::trimmed(serialize(error.type));
    ErrorHandler result;
    auto sub_type_str = error.sub_type ? error.sub_type.value() : "";
    auto message_str = error.message ? error.message.value() : "";
    std::string error_id;

    switch (error.type) {
    case ErrorEnum::DiodeFault:
    case ErrorEnum::VentilationNotAvailable:
    case ErrorEnum::BrownOut:
    case ErrorEnum::EnergyManagement:
    case ErrorEnum::PermanentFault:
    case ErrorEnum::MREC2GroundFailure:
    case ErrorEnum::MREC3HighTemperature:
    case ErrorEnum::MREC4OverCurrentFailure:
    case ErrorEnum::MREC5OverVoltage:
    case ErrorEnum::MREC6UnderVoltage:
    case ErrorEnum::MREC8EmergencyStop:
    case ErrorEnum::MREC10InvalidVehicleMode:
    case ErrorEnum::MREC14PilotFault:
    case ErrorEnum::MREC15PowerLoss:
    case ErrorEnum::MREC17EVSEContactorFault:
    case ErrorEnum::MREC18CableOverTempDerate:
    case ErrorEnum::MREC19CableOverTempStop:
    case ErrorEnum::MREC20PartialInsertion:
    case ErrorEnum::MREC23ProximityFault:
    case ErrorEnum::MREC24ConnectorVoltageHigh:
    case ErrorEnum::MREC25BrokenLatch:
    case ErrorEnum::MREC26CutCable:
    case ErrorEnum::TiltDetected:
    case ErrorEnum::WaterIngressDetected:
    case ErrorEnum::EnclosureOpen:
    case ErrorEnum::VendorError:
    case ErrorEnum::VendorWarning:
    case ErrorEnum::CommunicationFault:
        error_id = "evse_board_support/" + error_str;
        result.raiser = [this, sub_type_str, message_str, error_id]() {
            auto ev_error = p_main->error_factory->create_error(error_id, sub_type_str, message_str,
                                                                Everest::error::Severity::High);
            p_main->raise_error(ev_error);
        };
        result.clearer = [this, error_id, sub_type_str] { p_main->clear_error(error_id, sub_type_str); };
        break;
    case ErrorEnum::ConnectorLockCapNotCharged:
    case ErrorEnum::ConnectorLockUnexpectedOpen:
    case ErrorEnum::ConnectorLockUnexpectedClose:
    case ErrorEnum::ConnectorLockFailedLock:
    case ErrorEnum::ConnectorLockFailedUnlock:
    case ErrorEnum::MREC1ConnectorLockFailure:
        error_id = "connector_lock/" + error_str;
        result.raiser = [this, sub_type_str, message_str, error_id]() {
            auto ev_error = p_connector_lock->error_factory->create_error(error_id, sub_type_str, message_str,
                                                                          Everest::error::Severity::High);
            p_connector_lock->raise_error(ev_error);
        };
        result.clearer = [this, error_id, sub_type_str] { p_connector_lock->clear_error(error_id, sub_type_str); };

        break;
    case ErrorEnum::Selftest:
    case ErrorEnum::DC:
    case ErrorEnum::AC:
        error_id = "ac_rcd/" + error_str;
        result.raiser = [this, sub_type_str, message_str, error_id]() {
            auto ev_error =
                p_rcd->error_factory->create_error(error_id, sub_type_str, message_str, Everest::error::Severity::High);
            p_rcd->raise_error(ev_error);
        };
        result.clearer = [this, error_id, sub_type_str] { p_rcd->clear_error(error_id, sub_type_str); };
        break;
    }
    return result;
}

} // namespace module
