.. _everest_modules_handwritten_Eastron_SDM630EV:

.. *****************
.. Eastron SDM630-EV
.. *****************

Driver module for the **Eastron SDM630-EV** power meter using Modbus RTU via EVerest's
``serial_communication_hub`` interface. It implements the standardized EVerest ``powermeter``
interface and supports **OCMF/Eichrecht** transaction flows. The SDM630-EV signs billing
documents inside the meter with a dedicated crypto chip (ECDSA-secp256r1-SHA256).

For the non-EV SDM630 variants (no signing hardware) use the ``GenericPowermeter`` module
with the ``Eastron_SDM630-V2`` register mapping instead.

Description
===========

This is an **EVerest Hardware Driver** module that:

- **Implements**: ``powermeter`` interface
- **Communicates**: Modbus RTU (through ``SerialCommHub``)
- **Provides**: Live meter values, OCMF transaction start/stop handling, public key publishing

Low-level Modbus retries and timeouts are handled by the ``SerialCommHub`` module
(``retries``, ``initial_timeout_ms``); this driver deliberately adds no second retry layer.
On a communication failure it raises ``powermeter/CommunicationFault``, waits
``communication_error_pause_delay_s`` and reconfigures the device, indefinitely.

Features
========

- **Live measurements**: voltage, current, power, reactive power per phase and totals,
  frequency and per-phase import/export energy, published every ``live_measurement_interval_ms``
- **OCMF transactions**:

  - ``start_transaction``: time synchronization, identification dataset (IS/IF/IT/ID/CT/CI),
    begin command
  - ``stop_transaction``: end command, waits for the signature, reads the OCMF document and
    returns it as ``signed_meter_value`` (``encoding_method: OCMF``)

- **Power-loss recovery**: the device reports interrupted sessions (charging status 2/3);
  the driver raises ``powermeter/VendorError`` (``OcmfTransactionInterrupted``) and returns the
  pending document on the next ``stop_transaction``
- **Public key publishing**: via the ``public_key_ocmf`` variable, either raw
  (``04`` + 64 bytes hex) or as DER SubjectPublicKeyInfo (needed by the Transparenzsoftware)

Limitations
===========

- The device has no register for the OCMF identification level (IL) and no tariff text;
  ``identification_level`` and ``tariff_text`` from the transaction request are ignored.
- The EVerest transaction id cannot be stored on the device. After a power loss the pending
  OCMF document is returned without verifying the requested transaction id.
- No serial number register is documented; set ``meter_id`` in the module config if needed.
- The meter signs one document per session (begin + end); there is no signed meter value
  at transaction start and no continuously signed live values.

Configuration
=============

.. list-table::
   :header-rows: 1

   * - Option
     - Description
   * - ``powermeter_device_id``
     - Modbus address of the meter (default 1)
   * - ``communication_error_pause_delay_s``
     - Pause before reconfiguring after a communication failure
   * - ``live_measurement_interval_ms``
     - Publish interval for live values
   * - ``timezone_offset_minutes``
     - Local time offset written to the meter (zone register + BCD time)
   * - ``ocmf_charge_point_identification_type``
     - OCMF CT field: ``EVSEID`` or ``CBIDC``
   * - ``ocmf_charge_point_identification``
     - OCMF CI field; empty uses the ``evse_id`` of the transaction request
   * - ``ocmf_signature_timeout_ms``
     - Maximum wait for the crypto chip after the end command
   * - ``public_key_format``
     - ``der`` (default, Transparenzsoftware) or ``raw``
   * - ``meter_id``
     - Meter id attached to live measurements

Troubleshooting
===============

- Signature status ``invalid date time``: time/timezone were not written before the charging
  session started. The driver does this at configuration and at every transaction start;
  check for communication errors before session start.
- The serial settings must match the meter (factory default: 9600 baud, no parity, 1 stop
  bit). These are configured in the ``SerialCommHub`` module.
- Reads are limited to 80 registers per request by the device; the driver chunks
  automatically.

Example configuration
=====================

See ``config/bringup/config-bringup-SDM630EV.yaml`` for a bench setup with ``SerialCommHub``
and the interactive ``BUPowermeter`` test module.
