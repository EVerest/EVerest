.. _howto-ocpp-custom-error-mapping:

####################################
Customize How Errors Reach the CSMS
####################################

This guide shows how to change the way the :ref:`OCPPmulti <everest_modules_OCPPmulti>` module reports EVerest
errors to the CSMS, using a custom error mapping file. Use it when the CSMS expects your own error codes and texts,
or when an OCPP 2.x error should be reported on a different component or variable of the device model.

Without a custom error mapping file, OCPPmulti reports every error with its built-in mapping, described in the
:ref:`error reporting <handwritten_ocppmulti_error-reporting>` section of the module documentation. The custom file
is an overlay on that mapping: each entry replaces only the fields it sets, and every other field and every error
without an entry keeps its built-in value.

What the file controls
======================

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Section
     - Fields
     - Reported in
   * - ``v16``
     - ``error_code``, ``vendor_id``, ``vendor_error_code``, ``info``
     - OCPP 1.6 **StatusNotification.req**
   * - ``v2``
     - ``tech_code``, ``tech_info``, ``component_name``, ``component_instance``, ``variable_name``,
       ``variable_instance``, ``severity``
     - OCPP 2.0.1 and 2.1 **NotifyEvent.req**

The following stay outside the file:

* **The EVSE and connector an error is reported on.** They always follow the
  :ref:`3-tier module mapping <tier_module_mapping>` of the module that raised the error, see
  `Choose where the error is reported`_.
* **The Faulted status.** An entry never makes a connector Faulted, see `Make a connector Faulted`_.
* **evse_manager/Inoperative.** This error drives the Faulted status and cannot be mapped.
* ``actualValue`` and ``cleared`` of the OCPP 2.x event. ``actualValue`` is ``"true"`` while the error is active
  and ``"false"`` once it is cleared.

Write the file
==============

Each key of the file is an EVerest error type, ``<namespace>/<type>``, as declared in the ``errors/*.yaml`` files.
Append ``#<sub_type>`` to map only the errors raised with that sub type; an entry for the type and sub type takes
precedence over an entry for the type alone. Each entry needs a ``v16`` section, a ``v2`` section, or both.

.. code-block:: json

  {
    "$schema": "custom_error_mapping.schema.json",
    "evse_board_support/MREC3HighTemperature": {
      "v16": {
        "vendor_id": "com.example",
        "vendor_error_code": "T-210"
      },
      "v2": {
        "tech_code": "T-210",
        "tech_info": "Connector temperature high on EVSE ${evse}: ${message}",
        "severity": 3
      }
    },
    "generic/VendorError#SurgeProtectionDevice": {
      "v16": {
        "error_code": "OtherError",
        "vendor_id": "com.example",
        "vendor_error_code": "SPD-1"
      },
      "v2": {
        "tech_code": "SPD-1",
        "component_name": "ChargingStation"
      }
    }
  }

The first entry keeps the built-in ``errorCode`` **HighTemperature** of the MREC mapping and replaces only the
vendor fields, the OCPP 2.x texts and the severity. The second entry is for a vendor error that a module raises with the sub type
``SurgeProtectionDevice``; over OCPP 2.x it is reported on the charging station, whichever EVSE the raising module
belongs to.

The file follows the JSON schema
``lib/everest/ocpp_module_common/schemas/custom_error_mapping.schema.json``, which describes every field. Point the
``$schema`` key at it to get completion and checks in your editor. A larger example is
``lib/everest/ocpp_module_common/schemas/custom_error_mapping.example.json``.

Set the severity
----------------

``severity`` (``v2`` only) is the OCPP 2.1 severity of the event, from ``0`` (Danger) to ``9`` (Debug). It is a
static value per entry: every occurrence of the error is reported with it, whatever severity the EVerest error was
raised with. Without the field, no severity is reported. Over OCPP 2.0.1, which has no severity, the field is
dropped.

Use placeholders in texts and codes
-----------------------------------

``vendor_id``, ``vendor_error_code`` and ``info`` (OCPP 1.6) and ``tech_code`` and ``tech_info`` (OCPP 2.x) may contain
placeholders such as ``${message}``, ``${evse}`` or ``${origin_module}``, which are replaced by fields of the reported
error. The module documentation lists :ref:`all placeholders <handwritten_ocppmulti_error-placeholders>`. The OCPP
limits of 255 (``vendorId``), 500 (``techInfo``) and 50 (the others) characters apply after substitution, so leave room
for the values; longer text is truncated.

Keep all texts and codes to printable ASCII characters, as OCPP requires for these fields.

Choose where the error is reported
==================================

The EVSE and connector of an error come from the module mapping of the module that raised it, never from the file:

.. code-block:: yaml

  active_modules:
    connector_lock_1:
      module: YourConnectorLockDriver
      mapping:
        module:
          evse: 1
          connector: 1

* **OCPP 1.6** reports the error on connector id ``1``, the EVSE of the mapping. Without a mapping it is reported
  on connector id ``0``, the whole charge point.
* **OCPP 2.x** fills the ``evse`` of the reported component from the mapping: the EVSE id, plus the connector id if
  the mapping names a connector. Without a mapping, the component has no ``evse``.

The ``v2`` section only names the component and variable. Pick a component that fits the mapping of the modules
raising the error:

* ``ChargingStation`` is always reported without an EVSE, even when the raising module is mapped to one.
* ``EVSE`` needs a module mapped to an EVSE.
* ``Connector`` needs a module mapped to an EVSE and a connector.

If the same error type is raised by modules on different EVSEs, each occurrence is reported on the EVSE of its own
module. To report errors of a module that has no hardware of its own on a particular EVSE, for example a bridge
module receiving errors from an external controller, give it a module mapping, or run one instance per EVSE with its
own mapping.

Since ``actualValue`` is always ``"true"`` or ``"false"``, map errors to a boolean variable, such as ``Problem``,
the built-in choice.

Make a connector Faulted
========================

Only **evse_manager/Inoperative** sets a connector to **Faulted**. EvseManager raises it when one of the modules it
requires, such as the board support, the connector lock or the RCD, raises an error that stops charging. To fault a
connector for a condition of your own, raise an error EvseManager treats as fatal from such a module, for example a
``VendorError``; a ``VendorWarning`` is reported but leaves the connector available. The custom file then only
controls how the causing error is described to the CSMS.

Configure the module
====================

Set ``CustomErrorMappingPath`` in the OCPPmulti configuration:

.. code-block:: yaml

  active_modules:
    ocpp:
      module: OCPPmulti
      config_module:
        CustomErrorMappingPath: custom_error_mapping.json

A relative path is resolved against the share directory of the module, ``<prefix>/share/everest/modules/OCPPmulti``.
An absolute path is used as is. Restart EVerest after changing the file; it is read once at startup.

Check the result
================

A misconfigured file never stops the charger. At startup, OCPPmulti logs a warning for each problem, prefixed with
the path of the file:

* An entry that breaks the schema, uses an error type no ``errors/*.yaml`` file declares, or maps
  evse_manager/Inoperative is ignored, and the built-in mapping applies to its errors. The warning ends with
  ``the entry is ignored``.
* An unknown placeholder, a missing closing ``}`` or text longer than the OCPP limit is warned about, and the entry is
  used.
* For OCPP 2.x, a component or variable that the device model does not contain is warned about, and the entry is
  used.
* If the file is missing, malformed or not a JSON object, it is ignored as a whole.

An entry that overrides fields of a built-in MREC mapping is logged at info level, as a reminder that the MREC codes
the CSMS may expect change.

To see what the CSMS receives, raise the error and look at the **StatusNotification.req** or **NotifyEvent.req** in
the OCPP message log, written to ``MessageLogPath``.
