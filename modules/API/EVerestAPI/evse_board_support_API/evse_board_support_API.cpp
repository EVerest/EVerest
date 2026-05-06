// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
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
        error_id = "acd_rcd/" + error_str;
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
