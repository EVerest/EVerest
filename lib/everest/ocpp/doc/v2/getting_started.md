# Getting Started with OCPP2.0.1

## EVSE-scoped ISO 15118 configuration

`ChargePoint::validate_token` and `ChargePoint::on_get_15118_ev_certificate_request` have overloads accepting an
optional EVSE id. Supply the originating EVSE for contract authorization and certificate requests. The original
overloads remain available for non-certificate authorization, but certificate-bearing authorization without a positive
EVSE id fails. Certificate installation without an EVSE-specific enabled setting also fails.

With an EVSE id, central contract validation and contract certificate installation read the corresponding
`ISO15118Ctrlr` component with `component.evse.id` set. There is no station-wide fallback. Missing variables,
unset attributes, unreadable values and nonpositive EVSE ids yield no effective setting. No settings are copied
between EVSEs. OCPPmulti removes the three unscoped Plug & Charge variables from its composed model and registers
their scoped replacements from ConfigService; standalone library users must provision their scoped variables.

SECC certificate renewal remains station-wide. Its warning about disabled V2G certificate installation considers
whether any configured EVSE has Plug & Charge enabled, using the same scoped-only lookup.

The OCPP 1.6 Plug & Charge data-transfer API accepts optional effective enable and central-validation settings.
OCPPmulti supplies them from the originating EVSE's device-model bindings; standalone callers omitting them retain
the legacy configuration behavior. Unscoped OCPP 1.6 keys cannot address the scoped bindings and are not broadcast.

## Integrate this library with your Charging Station Implementation for OCPP2.0.1

OCPP is a protocol that affects, controls and monitors many areas of a charging station's operation.

If you want to integrate this library with your charging station implementation, you have to register a couple of **callbacks** and integrate **event handlers**. This is necessary for the library to interact with your charging station according to the requirements of OCPP.

Libocpp needs registered **callbacks** in order to execute control commands defined within OCPP (e.g ResetRequest or RemoteStartTransactionRequest)

The implementation must call **event handlers** of libocpp so that the library can track the state of the charging station and trigger OCPP messages accordingly (e.g. MeterValuesRequest , StatusNotificationRequest)

Your reference within libocpp to interact is a single instance to the class ocpp::v2::ChargePoint defined in `v2/charge_point.hpp` for OCPP 2.0.1.

Firmware update status reported before the BootNotification is accepted is held until registration succeeds.
Only the latest pending status is sent, once. An Installed status generates the FirmwareUpdated security event;
a FirmwareUpdate boot reason does not generate a second event.

The disable_connectors_during_install flag of on_firmware_update_status_notification decides whether all
connectors are made unavailable for the install. Left unset, connectors are disabled with the Downloaded status
of an unsigned update and the SignatureVerified status of a signed one. Set to false at that point, the
connectors stay available; a later InstallScheduled status with the flag set to true disables them after all.
Connector availability does not depend on registration; only the status message is held. Whether an update is
signed is taken from the accepted UpdateFirmware request with the same request ID; for a status that matches no
such request, request ID -1 is treated as unsigned and any other request ID as signed.
