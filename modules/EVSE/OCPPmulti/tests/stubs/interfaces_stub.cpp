// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "interfaces_stub.hpp"
#include <everest/logging.hpp>
#include <utils/error/error_database.hpp>
#include <utils/error/error_manager_impl.hpp>
#include <utils/error/error_manager_req.hpp>
#include <utils/error/error_manager_req_global.hpp>

#include <memory>

namespace {

// if "[info]    Type <type name> is not known, ignore subscription"
// is seen then add <type name> to the known_errors map

// type, description
const std::map<Everest::error::ErrorType, std::string> known_errors{
    {"evse_manager/Inoperative", "EVSE manager Inoperative"}};
} // namespace

namespace stubs {

std::optional<json> ModuleAdapter::auth_call_set_connection_timeout(const Requirement& req, const json& args) {
    // auth interface
    // set_connection_timeout:
    //   description: Sets the connection timeout
    //   arguments:
    //     connection_timeout:
    //       description: Connection timeout in seconds
    //       type: integer
    //       minimum: 10
    //       maximum: 300

    EVLOG_debug << "Call set_connection_timeout: " << args.dump();
    publish_fn("auth", "call_set_connection_timeout", args);
    return {};
}

std::optional<json> ModuleAdapter::auth_call_set_master_pass_group_id(const Requirement& req, const json& args) {
    // auth interface
    // set_master_pass_group_id:
    //   description: >-
    //     Sets the master pass group id. IdTokens that have this id as parent_id_token belong to the Master Pass Group.
    //     This means they can stop any ongoing transaction, but cannot start transactions. This can, for example, be
    //     used by law enforcement personal to stop any ongoing transaction when an EV has to be towed away. If
    //     master_pass_group_id is an empty string, it is not used.
    //   arguments:
    //     master_pass_group_id:
    //       description: The master pass group id
    //       type: string
    //       maxLength: 36

    EVLOG_debug << "Call set_master_pass_group_id: " << args.dump();
    publish_fn("auth", "call_set_master_pass_group_id", args);
    return {};
}

std::optional<json> ModuleAdapter::charger_information_call_get_charger_information(const Requirement& req,
                                                                                    const json& args) {

    // charger_information interface
    // get_charger_information:
    //   description: Queries the current charger information
    //   result:
    //     description: Returns an object with the current charger meta information
    //     type: object
    //     $ref: /charger_information#/ChargerInformation
    //

    // ChargerInformation:
    //   description: >-
    //     Type holding (mostly static) meta information about the whole charger.
    //     Note: Vendor and manufacturer refer here to the very same company - it is the brand
    //     which appears physically on the device label and/or which is shown on device's web
    //     frontend, in UPnP etc.
    //     We use both terms here to differentiate the different usages - with legal suffix, or without.
    //     This is why e.g. no `vendor_url` field is defined - it would be the very same
    //     as `manufacturer_url`.
    //   type: object
    //   additionalProperties: false
    //   required:
    //     - vendor
    //     - model
    //   properties:
    //     vendor:
    //       description: >-
    //         Name of vendor, not including any legal form. Is typically used with `model` to
    //         form a unique product name aka `vendor` + `single whitespace` + `model`.
    //         Example: Pionix
    //       type: string
    //     model:
    //       description: >-
    //         A human-friendly name of the model, aka the product name; without vendor name,
    //         without any device specific data/numbers (serial numbers/MAC addresses).
    //         Example: BelayBox
    //       type: string
    //     chargepoint_serial:
    //       description: >-
    //         The traditional serial number as string. It does not necessarily consist of digits only.
    //         Usually, this serial appears also on a label on the device.
    //         Example: SH4CAWN00123
    //       type: string
    //     chargebox_serial:
    //       description: >-
    //         The serial number of the controller as string (if any).
    //         It does not necessarily consist of digits only.
    //         Example: CB123456
    //       type: string
    //     friendly_name:
    //       description: >-
    //         A string with can be used to display the device eg. in network listings/enumerations.
    //         As mentioned above, this could typically consist of eg.:
    //         `vendor` + `single whitespace` + `model` + ` [` + `serial` + `]`
    //         Printers or wifi access points for example often use the last digits of their
    //         MAC address instead of the serial number to allow users to differentiate multiple
    //         instances in the network.
    //       type: string
    //     manufacturer:
    //       description: >-
    //         The name of the vendor, but this may include the legal form.
    //         Typically used in product property lists or as text for the following URL.
    //       type: string
    //     manufacturer_url:
    //       description: >-
    //         An URL to the vendor website, ideally not a deep link so that it is available for
    //         the whole product lifetime.
    //       type: string
    //     model_url:
    //       description: >-
    //         An URL to the model website (if any); as above, ideally not a deep link.
    //       type: string
    //     model_number:
    //       description: >-
    //         A model number as string (if any).
    //       type: string
    //     model_revision:
    //       description: >-
    //         A model or product revision string. Very often this is not defined for the first one
    //         so that users of this variable should consider reasonable fallback/default
    //         values. For later product/model revisions, this is typically printed also on a device
    //         label so that customers can refer to it when e.g requesting product support.
    //       type: string
    //     board_revision:
    //       description: >-
    //         The revision (aka version) of the internal (main) PCB, ie. that one with
    //         the CPU running the EVerest system.
    //       type: string
    //     firmware_version:
    //       description: >-
    //         A string containing the current running firmware version of the complete system.
    //         This is typically not the EVerest version itself, but a firmware version which appears
    //         e.g. on the web frontend and as part of the filenames the customer can download from
    //         the manufacturer's website.
    //         This is the only property which is typically not static for the product lifetime,
    //         but determined at runtime.
    //       type: string

    EVLOG_debug << "Call call_get_charger_information: " << args.dump();
    publish_fn("charger_information", "call_get_charger_information", req.index, args);
    auto result = get_cmd_response(R"({"vendor":"Pionix", "model":"BelayBox"})"_json);
    if (result) {
        EVLOG_debug << "result:                            " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::display_message_call_clear_display_message(const Requirement& req,
                                                                              const json& args) {
    // display_message interface
    // clear_display_message:
    //   description: Command to remove a display message
    //   arguments:
    //     request:
    //       description: The request to clear a message
    //       type: object
    //       $ref: /display_message#/ClearDisplayMessageRequest
    //   result:
    //     description: Response on the clear message request
    //     type: object
    //     $ref: /display_message#/ClearDisplayMessageResponse
    //
    // ClearDisplayMessageResponse:
    //  description: Response on the clear display message request.
    //  type: object
    //  properties:
    //    status:
    //      type: object
    //      $ref: /display_message#/ClearMessageResponseEnum
    //    status_info:
    //      type: string
    //  required:
    //    - status
    //
    // ClearMessageResponseEnum:
    //   description: Response on a clear display message request
    //   type: string
    //   enum:
    //     - Accepted
    //     - Unknown

    EVLOG_debug << "Call call_clear_display_message: " << args.dump();
    publish_fn("display_message", "call_clear_display_message", req.index, args);
    auto result = get_cmd_response(R"({"status":"Unknown"})"_json);
    if (result) {
        EVLOG_debug << "result:                          " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::display_message_call_get_display_messages(const Requirement& req, const json& args) {
    // display_message interface
    // get_display_messages:
    //   description: Command to get one or more display messages.
    //   arguments:
    //     request:
    //       description: The request for display messages
    //       type: object
    //       $ref: /display_message#/GetDisplayMessageRequest
    //   result:
    //     description: The display messages or an empty array if there are none
    //     type: object
    //     $ref: /display_message#/GetDisplayMessageResponse
    //
    // GetDisplayMessageResponse:
    //   description: Response on the 'get display message' request. Will return the requested display messages.
    //   type: object
    //   properties:
    //     status_info:
    //       description: Detailed status information
    //       type: string
    //     messages:
    //       description: Requested messages, if any
    //       type: array
    //       items:
    //         type: object
    //         $ref: /display_message#/DisplayMessage

    EVLOG_debug << "Call call_get_display_messages: " << args.dump();
    publish_fn("display_message", "call_get_display_messages", req.index, args);
    auto result = get_cmd_response(R"({"status_info":"Invalid criteria"})"_json);
    if (result) {
        EVLOG_debug << "result:                         " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::display_message_call_set_display_message(const Requirement& req, const json& args) {
    // display_message interface
    // set_display_message:
    //   description: >-
    //     Command to set or replace a display message.
    //   arguments:
    //     request:
    //       description: >-
    //         Request to set a display message
    //       type: array
    //       items:
    //         description: The display messages to set
    //         type: object
    //         $ref: /display_message#/DisplayMessage
    //   result:
    //     description: >-
    //       Response to the set display message request.
    //     type: object
    //     $ref: /display_message#/SetDisplayMessageResponse
    //
    // SetDisplayMessageResponse:
    //   description: >-
    //     Response to the set display message request.
    //   type: object
    //   properties:
    //     status:
    //       description: Whether the charging station is able to display the message
    //       $ref: /display_message#/DisplayMessageStatusEnum
    //       type: object
    //     status_info:
    //       description: Detailed status information
    //       type: string
    //   required:
    //     - status
    //
    // DisplayMessageStatusEnum:
    //   description: Response on a display message request
    //   type: string
    //   enum:
    //     - Accepted
    //     - NotSupportedMessageFormat
    //     - Rejected
    //     - NotSupportedPriority
    //     - NotSupportedState
    //     - UnknownTransaction

    EVLOG_debug << "Call call_set_display_message: " << args.dump();
    publish_fn("display_message", "call_set_display_message", req.index, args);
    auto result = get_cmd_response(R"({"status":"UnknownTransaction"})"_json);
    if (result) {
        EVLOG_debug << "result:                        " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_external_ready_to_start_charging(const Requirement& req,
                                                                                      const json& args) {
    // evse_manager interface
    // external_ready_to_start_charging:
    //   description: >-
    //     There are situations where another module needs to do some initialization after evse manager is in principle
    //     ready to start charging. This command can be used (optimally in combination with a configuration option) to
    //     delay charging ready until the external module is done with its initialization
    //   result:
    //     description: Returns true if the signal was used by the evse manager implementation
    //     type: boolean

    EVLOG_debug << "Call external_ready_to_start_charging: " << args.dump();
    publish_fn("evse_manager", "call_external_ready_to_start_charging", req.index, args);
    auto result = get_cmd_response("true"_json);
    if (result) {
        EVLOG_debug << "result:                                " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_enable_disable(const Requirement& req, const json& args) {
    // evse_manager interface
    // enable_disable:
    //   description: Enables or disables the evse. Turns off PWM with error F. Charging is only possible if an EVSE is
    //   enabled. arguments:
    //     connector_id:
    //       description: Specifies the ID of the connector to enable. If 0, the whole EVSE should be enabled
    //       type: integer
    //     cmd_source:
    //       description: Source of the enable command
    //       type: object
    //       $ref: /evse_manager#/EnableDisableSource
    //   result:
    //     description: >-
    //       Returns true if evse is enabled after the command, false if it is disabled.
    //       This may not be the same value as the request, since there may be a higher priority request
    //       from another source that is actually deciding whether it is enabled or disabled.
    //     type: boolean
    EVLOG_debug << "Call call_enable_disable: " << args.dump();
    publish_fn("evse_manager", "call_enable_disable", req.index, args);
    auto result = get_cmd_response("false"_json);
    if (result) {
        EVLOG_debug << "result:                   " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_force_unlock(const Requirement& req, const json& args) {
    // evse_manager interface
    //  force_unlock:
    //    description: >-
    //      Forces connector to unlock connector now. During normal operation, connector
    //      will be locked/unlocked in the correct sequence. Do not use this function except
    //      if explicitly requested by e.g. management cloud.
    //    arguments:
    //      connector_id:
    //        description: Specifies the ID of the connector that should be unlocked
    //        type: integer
    //    result:
    //      description: >-
    //        Returns true if unlocking command was accepted, or false if it is not supported.
    //        It does not reflect the success/failure of the actual unlocking.
    //        If unlocking fails, the connector_lock interface shall raise an error asynchronously.
    //      type: boolean

    EVLOG_debug << "Call call_force_unlock: " << args.dump();
    publish_fn("evse_manager", "call_force_unlock", req.index, args);
    auto result = get_cmd_response("false"_json);
    if (result) {
        EVLOG_debug << "result:                 " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_get_evse(const Requirement& req, const json& args) {
    // evse_manager interface
    // get_evse - no arguments
    // response - /evse_manager#/Evse
    //
    // Evse:
    //   description: Type that defines properties of an EVSE including its connectors
    //   type: object
    //   required:
    //     - id
    //     - connectors
    //   properties:
    //     id:
    //       description: ID of the EVSE
    //       type: integer
    //       minimum: 1
    //       maximum: 128
    //     connectors:
    //       description: List of connectors of this EVSE
    //       type: array
    //       items:
    //         description: A single connector
    //         type: object
    //         $ref: /evse_manager#/Connector
    //       minItems: 1
    //       maxItems: 128
    //
    // Connector:
    //   description: >-
    //     Type for a connector which is an independently operated and managed electrical outlet of an EVSE. It
    //     corresponds to a single physical connector
    //   type: object
    //   additionalProperties: false
    //   required:
    //     - id
    //   properties:
    //     id:
    //       description: Id of the connector. Connectors should be numbered starting with 1 counting upwards
    //       type: integer
    //       minimum: 1
    //     type:
    //       description: Type of the connector
    //       type: string
    //       $ref: /evse_manager#/ConnectorTypeEnum

    EVLOG_debug << "Call call_get_evse: " << args.dump();
    publish_fn("evse_manager", "call_get_evse", req.index, args);
    json default_result = R"({"id": 0,"connectors":[{"id":1}]})"_json;
    default_result["id"] = req.index + 1;
    auto result = get_cmd_response(default_result);
    if (result) {
        EVLOG_debug << "result:             " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_pause_charging(const Requirement& req, const json& args) {
    // evse_manager interface
    // pause_charging:
    //   description: Call to signal EVSE to pause charging
    //   result:
    //     description: >-
    //       Returns true if successfully paused or was already in paused_by_evse
    //       mode
    //     type: boolean

    EVLOG_debug << "Call call_pause_charging: " << args.dump();
    publish_fn("evse_manager", "call_pause_charging", req.index, args);
    auto result = get_cmd_response("false"_json);
    if (result) {
        EVLOG_debug << "result:                   " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_resume_charging(const Requirement& req, const json& args) {
    // evse_manager interface
    // resume_charging:
    //   description: Call to signal EVSE to resume charging
    //   result:
    //     description: >-
    //       Returns true if resume was successful, false otherwise (e.g. resuming
    //       a car pause won't work)
    //     type: boolean

    EVLOG_debug << "Call resume_charging: " << args.dump();
    publish_fn("evse_manager", "call_resume_charging", req.index, args);
    auto result = get_cmd_response("false"_json);
    if (result) {
        EVLOG_debug << "result:                    " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_set_der_available(const Requirement& req, const json& args) {
    // evse_manager interface
    // set_der_available:
    //   result:
    //     $ref: /evse_manager#/SetDerAvailableResult (enum: Accepted, NoHlc)
    EVLOG_debug << "Call call_set_der_available: " << args.dump();
    publish_fn("evse_manager", "call_set_der_available", req.index, args);
    auto result = get_cmd_response(R"("Accepted")"_json);
    if (result) {
        EVLOG_debug << "result:                     " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_set_plug_and_charge_configuration(const Requirement& req,
                                                                                       const json& args) {
    // evse_manager interface
    // set_plug_and_charge_configuration:
    //   description: >-
    //     Sets the configuration required for ISO15118 to handle contract authorization.
    //   arguments:
    //     plug_and_charge_configuration:
    //       description: The plug and charge configuration object
    //       type: object
    //       $ref: /evse_manager#/PlugAndChargeConfiguration
    EVLOG_debug << "Call call_set_plug_and_charge_configuration: " << args.dump();
    publish_fn("evse_manager", "call_set_plug_and_charge_configuration", req.index, args);
    return {};
}

std::optional<json> ModuleAdapter::evse_manager_call_stop_transaction(const Requirement& req, const json& args) {
    // evse_manager interface
    // stop_transaction:
    //   description: >-
    //     Stops transaction and cancels charging externally, charging can only
    //     be resumed by replugging car. EVSE will also stop transaction automatically
    //     e.g. on disconnect, so this only needs to be called if the transaction should
    //     end before.
    //   arguments:
    //     request:
    //       description: Request to stop the transaction.
    //       type: object
    //       $ref: /evse_manager#/StopTransactionRequest
    //   result:
    //     description: Returns true if successful
    //     type: boolean

    EVLOG_debug << "Call call_stop_transaction: " << args.dump();
    publish_fn("evse_manager", "call_stop_transaction", req.index, args);
    auto result = get_cmd_response("false"_json);
    if (result) {
        EVLOG_debug << "result:                     " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_update_allowed_energy_transfer_modes(const Requirement& req,
                                                                                          const json& args) {
    // evse_manager interface
    // update_allowed_energy_transfer_modes:
    //   description: >-
    //     Sets the supported energy transfer mode for ISO15118. It is expected that this command will update the
    //     ISO15118 software stack to only propose the list of allowed_energy_transfer_modes presented in this message.
    //     If a transaction is already in progress, it is expected that this triggers a service renegotiation.
    //     If no HLC is present, this will be accepted, but do nothing.
    //   arguments:
    //     allowed_energy_transfer_modes:
    //       description: >-
    //         The list of supported energy transfer modes. It cannot be empty as we need to propose something
    //         to the EV.
    //       type: array
    //       items:
    //         type: string
    //         $ref: /iso15118#/EnergyTransferMode
    //       minItems: 1
    //   result:
    //     description: Returns an enum indicating whether the update was successful or not.
    //     type: string
    //     $ref: /evse_manager#/UpdateAllowedEnergyTransferModesResult

    EVLOG_debug << "Call call_update_allowed_energy_transfer_modes: " << args.dump();
    publish_fn("evse_manager", "call_update_allowed_energy_transfer_modes", req.index, args);
    auto result = get_cmd_response(R"("NoHlc")"_json);
    if (result) {
        EVLOG_debug << "result:                                         " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::external_energy_limits_call_set_external_limits(const Requirement& req,
                                                                                   const json& args) {
    EVLOG_debug << "Call call_set_external_limits: " << args.dump();
    publish_fn("external_energy_limits", "call_set_external_limits", args);
    return {};
}

std::optional<json> ModuleAdapter::iso15118_extensions_call_set_get_certificate_response(const Requirement& req,
                                                                                         const json& args) {
    // iso15118_extensions interface
    // set_get_certificate_response:
    //   description: >-
    //     CertificateInstallationRes/CertificateUpdateRes - Set the new/updated Contract Certificate (including the
    //     certificate chain) and the corresponding encrypted private key. Should be forwarded to EVCC. This is an async
    //     response to a previously published iso15118_certificate_request
    //   arguments:
    //     certificate_response:
    //       description: The response raw exi stream and the status from the CSMS system
    //       type: object
    //       $ref: /iso15118#/ResponseExiStreamStatus
    EVLOG_debug << "Call call_set_get_certificate_response: " << args.dump();
    publish_fn("iso15118_extensions", "call_set_get_certificate_response", args);
    return {};
}

std::optional<json> ModuleAdapter::ocpp_data_transfer_call_data_transfer(const Requirement& req, const json& args) {
    // ocpp_data_transfer interface
    // data_transfer:
    //   description: >-
    //     Performs a OCPP data transfer request and returns the response
    //   arguments:
    //     request:
    //       description: >-
    //         Request object containing data transfer request
    //       type: object
    //       $ref: /ocpp#/DataTransferRequest
    //   result:
    //     description: >-
    //       Result object containing data transfer response
    //     type: object
    //     $ref: /ocpp#/DataTransferResponse
    //
    // DataTransferResponse:
    //   description: Type for data transfer response provided by OCPP
    //   type: object
    //   additionalProperties: false
    //   required:
    //     - status
    //   properties:
    //     status:
    //       description: Status of the data transfer
    //       type: string
    //       $ref: /ocpp#/DataTransferStatus
    //     data:
    //       description: Data provided by this data transfer
    //       type: string
    //     custom_data:
    //       description: Custom data extension
    //       type: object
    //       $ref: /ocpp#/CustomData
    //
    // DataTransferStatus:
    //   description: Data Transfer Status enum
    //   type: string
    //   enum:
    //     - Accepted
    //     - Rejected
    //     - UnknownMessageId
    //     - UnknownVendorId
    //     - Offline

    EVLOG_debug << "Call call_data_transfer: " << args.dump();
    publish_fn("ocpp_data_transfer", "call_data_transfer", args);
    auto default_response = R"({"status":"Offline"})"_json;
    auto result = get_cmd_response(default_response);
    if (result) {
        EVLOG_debug << "result:                  " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::reservation_call_cancel_reservation(const Requirement& req, const json& args) {
    // reservation interface
    // cancel_reservation:
    //   description: Cancels the reservation with the given reservation_id
    //   arguments:
    //     reservation_id:
    //       description: Id of the reservation
    //       type: integer
    //   result:
    //     description: >-
    //       Returns true if reservation was cancelled. Returns false if there
    //       was no reservation to cancel.
    //     type: boolean

    EVLOG_debug << "Call cancel_reservation: " << args.dump();
    publish_fn("reservation", "call_cancel_reservation", args);
    auto result = get_cmd_response(R"(false)"_json);
    if (result) {
        EVLOG_debug << "result:           " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::reservation_call_exists_reservation(const Requirement& req, const json& args) {
    // reservation interface
    // exists_reservation:
    //   description: >-
    //     Checks if there is a reservation made for the given connector and token. Will also return true if there
    //     is a reservation with this token for evse id 0.
    //   arguments:
    //     request:
    //       type: object
    //       $ref: /reservation#/ReservationCheck
    //       description: >-
    //         The information to send for the check if there is a reservation on the given connector for the given
    //         token.
    //   result:
    //     description: >-
    //       Returns an enum which indicates the reservation status of the given id / id token / group id token
    //       combination.
    //     type: string
    //     $ref: /reservation#/ReservationCheckStatus

    // NotReserved
    // ReservedForToken
    // ReservedForOtherToken
    // ReservedForOtherTokenAndHasParentToken

    EVLOG_debug << "Call exists_reservation: " << args.dump();
    publish_fn("reservation", "call_exists_reservation", args);
    auto result = get_cmd_response(R"("ReservedForOtherTokenAndHasParentToken")"_json);
    if (result) {
        EVLOG_debug << "result:                  " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::reservation_call_reserve_now(const Requirement& req, const json& args) {
    // reservation interface
    // reserve_now:
    //   description: Reserves an evse.
    //   arguments:
    //     request:
    //       type: object
    //       $ref: /reservation#/Reservation
    //       description: Requests to make a reservation
    //   result:
    //     description: >-
    //       Returns Accepted if reservation was successful or specifies error code.
    //     type: string
    //     $ref: /reservation#/ReservationResult

    // - Accepted
    // - Faulted
    // - Occupied
    // - Rejected
    // - Unavailable

    EVLOG_debug << "Call reserve_now: " << args.dump();
    publish_fn("reservation", "call_reserve_now", args);
    auto result = get_cmd_response(R"("Faulted")"_json);
    if (result) {
        EVLOG_debug << "result:           " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::system_call_allow_firmware_installation(const Requirement& req, const json& args) {
    // system interface
    // allow_firmware_installation:
    //   description: Call to allow a firmware installation to proceed

    EVLOG_debug << "Call allow_firmware_installation: " << args.dump();
    publish_fn("system", "call_allow_firmware_installation", args);
    return {};
}

std::optional<json> ModuleAdapter::system_call_get_boot_reason(const Requirement& req, const json& args) {
    // system interface
    // get_boot_reason:
    //   description: Call to get the boot reason of the system
    //   result:
    //     description: Returns the boot reason of the system
    //     type: string
    //     $ref: /system#/BootReason
    //
    // BootReason:
    //   description: Enum defining the boot reason
    //   type: string
    //   enum:
    //     - ApplicationReset
    //     - FirmwareUpdate
    //     - LocalReset
    //     - PowerUp
    //     - RemoteReset
    //     - ScheduledReset
    //     - Triggered
    //     - Unknown
    //     - Watchdog

    EVLOG_debug << "Call get_boot_reason: " << args.dump();
    publish_fn("system", "call_get_boot_reason", args);
    auto result = get_cmd_response(R"("PowerUp")"_json);
    if (result) {
        EVLOG_debug << "result:               " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::system_call_is_reset_allowed(const Requirement& req, const json& args) {
    // system interface
    // is_reset_allowed:
    //   description: Call to determine if a reset of the system is allowed
    //   arguments:
    //     type:
    //       description: Type of the reset (Soft or Hard)
    //       type: string
    //       $ref: /system#/ResetType
    //   result:
    //     description: >-
    //       Indicates if the system can be reset
    //     type: boolean

    EVLOG_debug << "Call is_reset_allowed: " << args.dump();
    publish_fn("system", "call_is_reset_allowed", args);
    auto result = get_cmd_response(R"(false)"_json);
    if (result) {
        EVLOG_debug << "result:                " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::system_call_reset(const Requirement& req, const json& args) {
    // system interface
    // reset:
    //   description: Call to reset the system immediately
    //   arguments:
    //     type:
    //       description: Type of the reset (Soft or Hard)
    //       type: string
    //       $ref: /system#/ResetType
    //     scheduled:
    //       description: >-
    //         Indicates if this reset command was scheduled or immediately executed. A scheduled reset means that the
    //         system was waiting for all transactions to finish before this command was executed. This value is only
    //         informational.
    //       type: boolean

    EVLOG_debug << "Call reset: " << args.dump();
    publish_fn("system", "call_reset", args);
    return {};
}

std::optional<json> ModuleAdapter::system_call_configure_network(const Requirement& req, const json& args) {
    // system interface
    // configure_network:
    //   description: >-
    //     Request preparation of the given network interface (e.g. modem/APN/VPN bring-up). May answer directly
    //     with Ready/Failed/Rejected, or with Processing, in which case the final outcome is published on the
    //     configure_network_status var with the same request_id. Providers without network-configuration support
    //     return NotSupported.
    //   arguments:
    //     request:
    //       description: The network configuration request
    //       type: object
    //       $ref: /network#/ConfigureNetworkRequest
    //   result:
    //     description: Direct response to the configuration request
    //     type: object
    //     $ref: /network#/ConfigureNetworkResponse

    EVLOG_debug << "Call configure_network: " << args.dump();
    publish_fn("system", "call_configure_network", args);
    auto result = get_cmd_response(R"({"status":"NotSupported"})"_json);
    if (result) {
        EVLOG_debug << "result:               " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::system_call_set_system_time(const Requirement& req, const json& args) {
    // system interface
    // set_system_time:
    //   description: Call to set the system time of EVerest
    //   arguments:
    //     timestamp:
    //       description: The new system time in RFC3339 format
    //       type: string
    //       format: date-time
    //   result:
    //     description: >-
    //       Returns true if the system time could be set successfully, else
    //       false
    //     type: boolean

    EVLOG_debug << "Call set_system_time: " << args.dump();
    publish_fn("system", "call_set_system_time", args);
    auto result = get_cmd_response(R"(false)"_json);
    if (result) {
        EVLOG_debug << "result:               " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::system_call_update_firmware(const Requirement& req, const json& args) {
    // system interface
    // update_firmware:
    //   description: Call to start a firmware update
    //   arguments:
    //     firmware_update_request:
    //       description: Meta data containing information about the firmware request
    //       type: object
    //       $ref: /system#/FirmwareUpdateRequest
    //   result:
    //     description: Returns the result of the attempt to update the firmware
    //     type: string
    //     $ref: /system#/UpdateFirmwareResponse
    //
    // UpdateFirmwareResponse:
    //   description: >-
    //     Enum for the response of an update_firmware command
    //       Accepted: Accepted this firmware update request. This does not mean the firmware update is successful, the
    //       system will now start the firmware update process Rejected: Firmware update request rejected
    //       AcceptedCanceled: Accepted this firmware update request, but in doing this has cancelled an ongoing
    //       firmware update InvalidCertificate: The certificate is invalid RevokedCertificate: Failure end state. The
    //       Firmware Signing certificate has been revoked
    //   type: string
    //   enum:
    //     - Accepted
    //     - Rejected
    //     - AcceptedCanceled
    //     - InvalidCertificate
    //     - RevokedCertificate

    EVLOG_debug << "Call update_firmware: " << args.dump();
    publish_fn("system", "call_update_firmware", args);
    auto result = get_cmd_response(R"("RevokedCertificate")"_json);
    if (result) {
        EVLOG_debug << "result:               " << result.value().dump();
    }
    return result;
}

std::optional<json> ModuleAdapter::system_call_upload_logs(const Requirement& req, const json& args) {
    // upload_logs:
    //   description: Call to start a log upload
    //   arguments:
    //     upload_logs_request:
    //       description: Meta data containing information about the log request request
    //       type: object
    //       $ref: /system#/UploadLogsRequest
    //   result:
    //     description: Returns the result of the attempt to upload the logs
    //     type: object
    //     $ref: /system#/UploadLogsResponse
    //
    // UploadLogsResponse:
    //   description: Response to a upload logs request
    //   type: object
    //   additionalProperties: false
    //   required:
    //     - upload_logs_status
    //   properties:
    //     file_name:
    //       description: This contains the name of the log file that will be uploaded.
    //       type: string
    //     upload_logs_status:
    //       description: The log status enum
    //       type: string
    //       $ref: /system#/UploadLogsStatus
    //
    // UploadLogsStatus:
    //   description: >-
    //     Enum for the upload logs status response
    //       Accepted: Request to upload logs has been accepted
    //       Rejected: Request to upload logs has been rejected
    //       AcceptedCanceled: Accepted this log upload, but in doing this has cancelled an ongoing log file upload
    //   type: string
    //   enum:
    //     - Accepted
    //     - Rejected
    //     - AcceptedCanceled

    EVLOG_debug << "Call upload_logs: " << args.dump();
    publish_fn("system", "call_upload_logs", args);
    auto result = get_cmd_response(R"({"upload_logs_status":"Rejected"})"_json);
    if (result) {
        EVLOG_debug << "result:           " << result.value().dump();
    }
    return result;
}

void ModuleAdapter::error_raised(const Everest::error::Error& error) {
    EVLOG_debug << "publish_raised_error";
}

void ModuleAdapter::error_cleared(const Everest::error::Error& error) {
    EVLOG_debug << "publish_cleared_error";
}

void ModuleAdapter::subscribe_error(const Everest::error::ErrorType& type, const Everest::error::ErrorCallback&,
                                    const Everest::error::ErrorCallback&) {
    EVLOG_debug << "Subscribe error: " << type;
}

ModuleAdapter::ModuleAdapter() : m_error_type_map(std::make_shared<Everest::error::ErrorTypeMap>()) {
    m_error_type_map->load_error_types_map(known_errors);
    for (const auto& item : known_errors) {
        m_supported_errors.push_back(item.first);
    }

    m_error_manager = std::make_shared<Everest::error::ErrorManagerImpl>(
        m_error_type_map, m_error_database, m_supported_errors,
        [this](const Everest::error::Error& error) { error_raised(error); },
        [this](const Everest::error::Error& error) { error_cleared(error); });

    m_error_manager_req = std::make_shared<Everest::error::ErrorManagerReq>(
        m_error_type_map, m_error_database, m_supported_errors,
        [this](const Everest::error::ErrorType& type, const Everest::error::ErrorCallback& raised,
               const Everest::error::ErrorCallback& cleared) { subscribe_error(type, raised, cleared); });

    m_error_manager_req_global = std::make_shared<Everest::error::ErrorManagerReqGlobal>(
        m_error_type_map, m_error_database, [this](auto&&... args) {});

    m_mqtt_abstraction = std::make_shared<MqttStub>();
    m_config_client =
        std::make_shared<Everest::config::ConfigServiceClient>(m_mqtt_abstraction, "ocpp", m_module_names);

    m_call_implementations.insert(
        {"allow_firmware_installation", &ModuleAdapter::system_call_allow_firmware_installation});
    m_call_implementations.insert({"cancel_reservation", &ModuleAdapter::reservation_call_cancel_reservation});
    m_call_implementations.insert(
        {"clear_display_message", &ModuleAdapter::display_message_call_clear_display_message});
    m_call_implementations.insert({"configure_network", &ModuleAdapter::system_call_configure_network});
    m_call_implementations.insert({"data_transfer", &ModuleAdapter::ocpp_data_transfer_call_data_transfer});
    m_call_implementations.insert({"enable_disable", &ModuleAdapter::evse_manager_call_enable_disable});
    m_call_implementations.insert({"exists_reservation", &ModuleAdapter::reservation_call_exists_reservation});
    m_call_implementations.insert(
        {"external_ready_to_start_charging", &ModuleAdapter::evse_manager_call_external_ready_to_start_charging});
    m_call_implementations.insert({"force_unlock", &ModuleAdapter::evse_manager_call_force_unlock});
    m_call_implementations.insert({"get_boot_reason", &ModuleAdapter::system_call_get_boot_reason});
    m_call_implementations.insert(
        {"get_charger_information", &ModuleAdapter::charger_information_call_get_charger_information});
    m_call_implementations.insert({"get_display_messages", &ModuleAdapter::display_message_call_get_display_messages});
    m_call_implementations.insert({"get_evse", &ModuleAdapter::evse_manager_call_get_evse});
    m_call_implementations.insert({"is_reset_allowed", &ModuleAdapter::system_call_is_reset_allowed});
    m_call_implementations.insert({"pause_charging", &ModuleAdapter::evse_manager_call_pause_charging});
    m_call_implementations.insert({"reserve_now", &ModuleAdapter::reservation_call_reserve_now});
    m_call_implementations.insert({"reset", &ModuleAdapter::system_call_reset});
    m_call_implementations.insert({"set_der_available", &ModuleAdapter::evse_manager_call_set_der_available});
    m_call_implementations.insert({"resume_charging", &ModuleAdapter::evse_manager_call_resume_charging});
    m_call_implementations.insert({"set_connection_timeout", &ModuleAdapter::auth_call_set_connection_timeout});
    m_call_implementations.insert({"set_display_message", &ModuleAdapter::display_message_call_set_display_message});
    m_call_implementations.insert(
        {"set_external_limits", &ModuleAdapter::external_energy_limits_call_set_external_limits});
    m_call_implementations.insert(
        {"set_get_certificate_response", &ModuleAdapter::iso15118_extensions_call_set_get_certificate_response});
    m_call_implementations.insert({"set_master_pass_group_id", &ModuleAdapter::auth_call_set_master_pass_group_id});
    m_call_implementations.insert(
        {"set_plug_and_charge_configuration", &ModuleAdapter::evse_manager_call_set_plug_and_charge_configuration});
    m_call_implementations.insert({"set_system_time", &ModuleAdapter::system_call_set_system_time});
    m_call_implementations.insert({"stop_transaction", &ModuleAdapter::evse_manager_call_stop_transaction});
    m_call_implementations.insert({"update_allowed_energy_transfer_modes",
                                   &ModuleAdapter::evse_manager_call_update_allowed_energy_transfer_modes});
    m_call_implementations.insert({"update_firmware", &ModuleAdapter::system_call_update_firmware});
    m_call_implementations.insert({"upload_logs", &ModuleAdapter::system_call_upload_logs});
}

Result ModuleAdapter::call_fn(const Requirement& req, const std::string& fn, Parameters args) {
    Result result;
    if (const auto it = m_call_implementations.find(fn); it != m_call_implementations.end()) {
        result = std::invoke(it->second, this, std::cref(req), std::cref(args));
    } else {
        EVLOG_warning << "Call <not implemented>: " << fn << ": " << args;
    }
    return result;
}

void ModuleAdapter::publish_fn(const std::string& interface, const std::string& variable, Value value) {
    publish_fn(interface, variable, -1, value);
}

void ModuleAdapter::subscribe_fn(const Requirement& req, const std::string& fn, ValueCallback callback) {
    const auto topic = to_topic(req, fn);
    EVLOG_debug << "Subscribe: " << topic;
    // there can be multiple subscribes to a topic
    m_callbacks.emplace_back(topic, std::move(callback));
}

std::shared_ptr<Everest::error::ErrorManagerImpl> ModuleAdapter::get_error_manager_impl_fn(const std::string&) {
    EVLOG_debug << "get_error_manager_impl_fn called";
    return m_error_manager;
}

std::shared_ptr<Everest::error::ErrorStateMonitor> ModuleAdapter::get_error_state_monitor_impl_fn(const std::string&) {
    EVLOG_debug << "get_error_state_monitor_impl_fn called";
    return std::make_shared<Everest::error::ErrorStateMonitor>(m_error_database);
}

std::shared_ptr<Everest::error::ErrorManagerReqGlobal> ModuleAdapter::get_global_error_manager_fn() {
    EVLOG_debug << "get_global_error_manager_fn called";
    return m_error_manager_req_global;
}

std::shared_ptr<Everest::error::ErrorStateMonitor> ModuleAdapter::get_global_error_state_monitor_fn() {
    EVLOG_debug << "get_global_error_state_monitor_fn called";
    return {};
}

std::shared_ptr<Everest::error::ErrorFactory> ModuleAdapter::get_error_factory_fn(const std::string&) {
    EVLOG_debug << "get_error_factory_fn called";
    return std::make_shared<Everest::error::ErrorFactory>(std::make_shared<Everest::error::ErrorTypeMap>());
}

std::shared_ptr<Everest::error::ErrorManagerReq> ModuleAdapter::get_error_manager_req_fn(const Requirement&) {
    EVLOG_debug << "get_error_manager_req_fn called";
    return m_error_manager_req;
}

std::shared_ptr<Everest::error::ErrorStateMonitor> ModuleAdapter::get_error_state_monitor_req_fn(const Requirement&) {
    EVLOG_debug << "get_error_state_monitor_req_fn called";
    return std::make_shared<Everest::error::ErrorStateMonitor>(Everest::error::ErrorStateMonitor(m_error_database));
}

void ModuleAdapter::ext_mqtt_publish_fn(const std::string&, const std::string&) {
    EVLOG_debug << "ext_mqtt_publish_fn called";
}

std::function<void()> ModuleAdapter::ext_mqtt_subscribe_fn(const std::string&, StringHandler) {
    EVLOG_debug << "ext_mqtt_subscribe_fn called";
    return nullptr;
}

std::function<void()> ModuleAdapter::ext_mqtt_subscribe_pair_fn(const std::string& topic,
                                                                const StringPairHandler& handler) {
    EVLOG_debug << "ext_mqtt_subscribe_pair_fn called";
    return {};
}

void ModuleAdapter::telemetry_publish_fn(const std::string&, const std::string&, const std::string&,
                                         const Everest::TelemetryMap&) {
    EVLOG_debug << "telemetry_publish_fn called";
}

std::optional<ModuleTierMappings> ModuleAdapter::get_mapping_fn() {
    EVLOG_debug << "get_mapping_fn called";
    return {};
}

std::shared_ptr<Everest::config::ConfigServiceClient> ModuleAdapter::get_config_service_client_fn() {
    EVLOG_debug << "get_config_service_client_fn called";
    return m_config_client;
}

std::optional<json> ModuleAdapter::get_cmd_response(const std::optional<json>& default_response) {
    auto result = default_response;
    if (!m_cmd_response_list.empty()) {
        result = m_cmd_response_list.front();
        m_cmd_response_list.pop_front();
    }
    return result;
}

void ModuleAdapter::publish_fn(const std::string& interface, const std::string& variable, int instance,
                               const json& value) {
    const auto key_all = to_key(interface, variable, -1);
    const auto key = to_key(interface, variable, instance);
    EVLOG_debug << "publish_fn: " << key << ": " << value.dump();
    for (const auto& i : m_subscribe_var_callbacks) {
        if ((i.first == key) || (i.first == key_all) || (i.first == "/")) {
            i.second(interface, variable, value);
        }
    }
}

void ModuleAdapter::publish(const Requirement& req, const std::string& fn, json args) {
    const auto topic = to_topic(req, fn);
    bool found{false};
    for (const auto& i : m_callbacks) {
        if (i.first == topic) {
            i.second(args);
            found = true;
        }
    }
    if (found) {
        EVLOG_debug << "Publish: " << topic << ": " << args;
    } else {
        EVLOG_warning << "Publish: " << topic << ": <not subscribed>";
    }
}

void ModuleAdapter::add_cmd_result(const json& data) {
    m_cmd_response_list.push_back(data);
}

void ModuleAdapter::subscribe_var(const std::string_view& interface, const std::string_view& variable, int instance,
                                  var_cb_t cb) {
    m_subscribe_var_callbacks.emplace_back(to_key(interface, variable, instance), std::move(cb));
}

} // namespace stubs
