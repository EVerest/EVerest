**************************
How To: OCPP1.6 in EVerest
**************************

.. note::

  EVerest has an implementation of OCPP 1.6J and 2.0.1. This tutorial is about
  the 1.6 implementation. To get documentation about all implemented versions,
  see `the GitHub repository of libocpp <https://github.com/EVerest/libocpp>`_.

EVerest provides a complete implementation of Open Charge Point Protocol
(OCPP) 1.6J, supporting all feature profiles including Plug&Charge and the
Security Extensions.

The code of `libocpp` is at https://github.com/EVerest/libocpp

This is a tutorial about how to set up and configure OCPP 1.6 in EVerest.

This tutorial includes:

- How to run EVerest SIL with the default OCPP1.6J configuration connecting to
  SteVe
- How to load the OCPP1.6 module as part of the EVerest configuration 
- How to configure OCPP1.6 configuration keys
- How to connect to different CSMS

.. _prerequisites:

Prerequisites
=============

If you're new to EVerest start with our
:ref:`Quick Start Guide <quickstartguide_main>`
to get a simulation in EVerest running for the first time.
It is important that you have set up the required docker containers for
Mosquitto and SteVe, which we will use as an example CSMS.
If you have done that successfully, you can move on with this tutorial.

.. _run_with_steve:

Run EVerest SIL with SteVe
==========================

EVerest's `everest-core` repository provides a configuration that you can use to run EVerest with OCPP.
By default this configuration is connecting to the Open Source CSMS `SteVe <https://github.com/steve-community/steve>`_.
Make sure that SteVe is running on your machine as the CSMS we connect to. 

You have to add the chargepoint id *cp001* in SteVe's webinterface to allow to a charging station to connect.
If you want to simulate charging sessions, you also need to add OCPP tags for the authorization.

Simply run

.. code-block:: bash

    ${EVEREST_WORKSPACE:?}/everest-core/build/run-scripts/run-sil-ocpp.sh

to start EVerest with OCPP1.6J. You can start playing around with central system initiated messages and use 
the EVerest simulation to start charging sessions.

You can find the OCPP message log in different formats in the `/tmp/everest_ocpp_logs` directory.

.. _configure_ocpp_everest:

Configuring OCPP1.6 within EVerest
==================================

To be able to follow the further explanations, you should be familiar with the configuration of EVerest modules.
Take a look into :ref:`EVerest Module Concept <moduleconcept_main>` for that.

To configure the OCPP module of everest-core, find the available configuration parameters `in the manifest
of the module <https://github.com/EVerest/everest-core/blob/main/modules/OCPP/manifest.yaml>`_ and read the
`module documentation <https://everest.github.io/nightly/_generated/modules/OCPP.html>` carefully
in order to configure it according to your needs.

In order to enable OCPP1.6 in EVerest, you need to load the module in the EVerest configuration file and set up the module connections. The interfaces
provided and required by the OCPP module and its purposes are described in the `module documentation <https://everest.github.io/nightly/_generated/modules/OCPP.html>`.

The EVerest configuration file `config-sil-ocpp.yaml <https://github.com/EVerest/everest-core/blob/main/config/config-sil-ocpp.yaml>` which was used previously serves as a good example
for how the connections of the module could be set up.

.. _configure_ocpp:

OCPP Configuration file
=======================

In addition to the EVerest configuration yaml file, OCPP1.6 is configured using a JSON configuration file.
This configuration file can contain all configuration keys from the OCPP1.6 specification.
Examples for that can be found `here <https://github.com/EVerest/libocpp/tree/main/config/v16>`.

This is the one we used to connect to SteVe:

.. code-block:: json

  {
    "Internal": {
        "ChargePointId": "cp001",
        "CentralSystemURI": "127.0.0.1:8180/steve/websocket/CentralSystemService/",
        "ChargeBoxSerialNumber": "cp001",
        "ChargePointModel": "Yeti",
        "ChargePointVendor": "Pionix",
        "FirmwareVersion": "0.1",
        "AllowChargingProfileWithoutStartSchedule": true,
        "UseTPM" : false,
        "LogMessagesFormat": ["html","security"]
    },
    "Core": {
        "AuthorizeRemoteTxRequests": false,
        "ClockAlignedDataInterval": 900,
        "ConnectionTimeOut": 30,
        "ConnectorPhaseRotation": "0.RST,1.RST",
        "GetConfigurationMaxKeys": 100,
        "HeartbeatInterval": 86400,
        "LocalAuthorizeOffline": false,
        "LocalPreAuthorize": false,
        "MeterValuesAlignedData": "Energy.Active.Import.Register",
        "MeterValuesSampledData": "Energy.Active.Import.Register,SoC",
        "MeterValueSampleInterval": 60,
        "NumberOfConnectors": 1,
        "ResetRetries": 1,
        "StopTransactionOnEVSideDisconnect": true,
        "StopTransactionOnInvalidId": true,
        "StopTxnAlignedData": "Energy.Active.Import.Register",
        "StopTxnSampledData": "Energy.Active.Import.Register",
        "SupportedFeatureProfiles": "Core,FirmwareManagement,RemoteTrigger,Reservation,LocalAuthListManagement,SmartCharging",
        "TransactionMessageAttempts": 1,
        "TransactionMessageRetryInterval": 10,
        "UnlockConnectorOnEVSideDisconnect": true,
        "WebsocketPingInterval": 0
    },
    "FirmwareManagement": {
        "SupportedFileTransferProtocols": "FTP"
    },
    "Security": {
        "CpoName": "Pionix",
        "AuthorizationKey": "AABBCCDDEEFFGGHH",
        "SecurityProfile": 1
    },
    "LocalAuthListManagement": {
        "LocalAuthListEnabled": true,
        "LocalAuthListMaxLength": 42,
        "SendLocalListMaxLength": 42
    },
    "SmartCharging": {
        "ChargeProfileMaxStackLevel": 42,
        "ChargingScheduleAllowedChargingRateUnit": "Current,Power",
        "ChargingScheduleMaxPeriods": 42,
        "MaxChargingProfilesInstalled": 42
    },
    "PnC": {
        "ISO15118PnCEnabled": true,
        "ContractValidationOffline": true
    },
    "CostAndPrice": {
        "CustomDisplayCostAndPrice": false
    },
    "Custom": {
        "ExampleConfigurationKey": "example"
    }
}

The configuration keys are split up into the feature profiles that are
specified in OCPP1.6 plus the extra profiles *Internal*, *Security*, *PnC* and *CostAndPrice*.
Here's a short overview of the purpose of each profile in the configuration file:

- Internal: Used for internal configuration keys that are not specified in
  OCPP1.6
- Core: Includes Core configuration keys of OCPP1.6
- FirmwareManagement: Includes configuration keys that apply when the feature
  profile FirmwareManagement is implemented
- Security: Includes configuration parameters that have been introduced within
  the OCPP1.6J Security Whitepaper
- LocalAuthListManagement: Includes configuration parameters that apply when
  the feature profile LocalAuthListManagement is implemented
- SmartCharging: Includes configuration parameters that apply when the feature
  profile SmartCharging is implemented
- PnC: Used for Plug&Charge and includes configuration parameters that have
  been introduced within the OCPP1.6J Plug&Charge Whitepaper
- CostAndPrice: Used for enabling support for the California Pricing Whitepaper

EVerest's `libocpp` supports almost all configuration parameters that are specified
within OCPP 1.6. Despite that, it is possible to omit configuration parameters
that are not required and it is even possible to omit a whole feature profile
in the configuration file if it is not supported. This means that the
configuration of the `libocpp` provides maximum flexibility and can be
tailored to your specific charging station.

Make sure to thoroughly read through the OCPP1.6 specification and the
`profile schemas <https://github.com/EVerest/libocpp/tree/main/config/v16/profile_schemas>`_ 
and configure OCPP according to your needs.

.. _different_csms:

Connect to a different CSMS
===========================

In order to connect to a different CSMS, you have to modify the connection
details within the ocpp configuration file. The ocpp config is a separate
JSON file in which all configuration keys of OCPP1.6 plus some internal parameters
can be configured.

You can specify the path to this configuration file inside the `everest-core`
configuration file using the configuration parameter `ChargePointConfigPath`
of the OCPP module within everest-core. This defaults to *ocpp-config.json*.
If this path is relative the default path for the ocpp configuration
dist/share/everest/modules/OCPP will be prepended.

To connect to a different CSMS, you have to modify the connection details of
the ocpp configuration file. This includes the parameter *CentralSystemURI*
and it might also include to change the parameters *AuthorizationKey* and
*SecurityProfile*. Here's a short overview of the purpose of the parameters:

- ChargePointId: Identity of the charging station
- CentralSystemURI: Specifies the endpoint of the CSMS
  - Can optionally include the ChargePointId after the last back-slash of the URI

- SecurityProfile: Specifies the SecurityProfile which defines type of
  transport layer connection between ChargePoint and CSMS

  - Can have the value 0, 1, 2 or 3
  - SecurityProfile 0: Unsecure transport without Basic Authentication (ws://)
  - SecurityProfile 1: Unsecure transport with Basic Authentication (ws://)
  - SecurityProfile 2: TLS with Basic authentication (wss://)
  - SecurityProfile 3: TLS with client side certificates (wss://)

- AuthorizationKey: Specifies the password used for HTTP Basic Authentication

  - Must be set if SecurityProfile is 1 or 2, can be omitted if
    SecurityProfile is 0 or 3
  - Minimal length: 16 bytes

Modify these parameters according to the connection requirements of the CSMS. Find all available configuration keys
and their descriptions in `here <https://github.com/EVerest/libocpp/tree/main/config/v16/profile_schemas>`_

.. _logging:

Logging
=======

The implementation allows to log all OCPP messages in different formats

The default logging path is /tmp/everest_ocpp_logs but can be set using the
configuration parameter *MessageLogPath* of the OCPP module of everest-core.
Within the ocpp configuration file, you can configure *LogMessages*, to enable
or disable logging and  *LogMessagesFormat* to specify to one or more log
formats. For the latter, you can specify the following values:

- console: Logs all OCPP messages
- log: Logs all OCPP messages in a text file
- html: Logs all OCPP messages using a html format (recommended)
- session_logging: Logs all OCPP messages in html format into a path that is
  optionally provided by the EvseManager at the start of a session
