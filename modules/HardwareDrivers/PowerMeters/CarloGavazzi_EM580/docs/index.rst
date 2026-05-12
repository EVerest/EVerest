.. _everest_modules_handwritten_CarloGavazzi_EM580:

.. *******************
.. Carlo Gavazzi EM580
.. *******************

Module implementing the Carlo Gavazzi EM580 power meter driver adapter via Modbus RTU (through SerialCommHub).
This module also supports models without OCMF/Eichrecht support (e.g. EM300 series).

Description
===========

The module consists of a single ``main`` implementation that serves the ``powermeter`` interface. Modbus access is done
via the required ``serial_communication_hub`` interface.

Features
========

- Live meter reads and ``powermeter`` publishing (interval configurable)
- Resilient Modbus transport with retries and protocol-compliant write chunking

If supported by meter:
- OCMF/Eichrecht transaction start/stop logic
- Public key reading and publishing (hex)

Module Configuration
====================

The module configuration parameters are defined in ``manifest.yaml``. A complete example configuration can be found at
``config/bringup/config-bringup-CGEM580.yaml``.

Transaction flow (OCMF)
=======================

Start transaction
-----------------

At transaction start the module:

1. Ensures OCMF state is ``NOT_READY``
2. Writes OCMF identification data, EVSE ID and tariff text (TT) (0-terminated, used bytes only)
3. Writes session modality
4. Sends the start command (``'B'``)

Stop transaction
----------------

At transaction stop the module:

1. Sends the end command (``'E'``) for the tracked transaction
2. Waits for OCMF state ``READY``
3. Reads the OCMF file (size + content)
4. Confirms the file read by setting state back to ``NOT_READY``

Notes / Limitations
===================

- Modbus ``Write Multiple Registers`` requests are chunked to max 123 registers per request.
- TT is a ``CHAR[252]`` field (126 words); overlong strings are warned and truncated.

Device identification code (register ``300012`` / ``000Bh``)
----------------------------------------------------------
At startup the driver reads the Carlo Gavazzi **Controls identification code** from Modbus register ``300012``
(``000Bh``) to decide whether OCMF transactions are exposed.

The following identification codes are **explicitly supported** as **EM300/ET300 series** (live metering only;
``start_transaction`` / ``stop_transaction`` return ``NOT_SUPPORTED``): **331**, **332**, **335**, **336**, **340**,
**341**, **345**, **346**, **355**.

Any **other** identification code is treated as an OCMF-capable device (e.g. EM580 class): the full transaction flow
applies. If a new meter without OCMF uses a code not listed above, the driver should be updated to recognise it;
otherwise it may incorrectly attempt the OCMF path.


