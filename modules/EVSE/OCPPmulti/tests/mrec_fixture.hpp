// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <array>
#include <string_view>

namespace mrec_fixture {

// Frozen MREC (ChargeX) error mapping as reported to the CSMS.
//
// Typed out by hand: never generate this table from, or include, the production mapping
// tables or a default mapping data file. It pins the current behavior so that any change
// to the reported codes is visible in review. A diff to this file must cite the
// requirement or decision that makes the change deliberate.

struct Entry {
    std::string_view type;            // EVerest error type (the lookup key)
    std::string_view v16_error_code;  // OCPP 1.6 StatusNotification.errorCode
    std::string_view v16_vendor_code; // OCPP 1.6 StatusNotification.vendorErrorCode
    std::string_view v2_tech_code;    // OCPP 2.x NotifyEvent eventData.techCode
};

constexpr std::string_view V16_VENDOR_ID = "https://chargex.inl.gov";

constexpr std::array<Entry, 23> ENTRIES{{
    {"connector_lock/MREC1ConnectorLockFailure", "ConnectorLockFailure", "CX001", "CX001"},
    {"evse_board_support/MREC2GroundFailure", "GroundFailure", "CX002", "CX002"},
    {"evse_board_support/MREC3HighTemperature", "HighTemperature", "CX003", "CX003"},
    {"evse_board_support/MREC4OverCurrentFailure", "OverCurrentFailure", "CX004", "CX004"},
    {"evse_board_support/MREC5OverVoltage", "OverVoltage", "CX005", "CX005"},
    {"evse_board_support/MREC6UnderVoltage", "UnderVoltage", "CX006", "CX006"},
    {"evse_board_support/MREC8EmergencyStop", "OtherError", "CX008", "CX008"},
    {"evse_board_support/MREC10InvalidVehicleMode", "OtherError", "CX010", "CX010"},
    {"evse_board_support/MREC14PilotFault", "OtherError", "CX014", "CX014"},
    {"evse_board_support/MREC15PowerLoss", "OtherError", "CX015", "CX015"},
    {"evse_board_support/MREC17EVSEContactorFault", "OtherError", "CX017", "CX017"},
    {"evse_board_support/MREC18CableOverTempDerate", "OtherError", "CX018", "CX018"},
    {"evse_board_support/MREC19CableOverTempStop", "OtherError", "CX019", "CX019"},
    {"evse_board_support/MREC20PartialInsertion", "OtherError", "CX020", "CX020"},
    {"evse_board_support/MREC23ProximityFault", "OtherError", "CX023", "CX023"},
    {"evse_board_support/MREC24ConnectorVoltageHigh", "OtherError", "CX024", "CX024"},
    {"evse_board_support/MREC25BrokenLatch", "OtherError", "CX025", "CX025"},
    {"evse_board_support/MREC26CutCable", "OtherError", "CX026", "CX026"},
    {"evse_manager/MREC4OverCurrentFailure", "OverCurrentFailure", "CX004", "CX004"},
    {"ac_rcd/MREC2GroundFailure", "GroundFailure", "CX002", "CX002"},
    {"evse_manager/MREC22ResistanceFault", "OtherError", "CX022", "CX022"},
    {"evse_manager/MREC11CableCheckFault", "OtherError", "CX011", "CX011"},
    {"evse_manager/MREC5OverVoltage", "OverVoltage", "CX005", "CX005"},
}};

} // namespace mrec_fixture
