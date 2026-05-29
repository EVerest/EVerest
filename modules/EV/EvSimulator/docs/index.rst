.. _everest_modules_handwritten_EvSimulator:

===========
EvSimulator
===========

Simplified EV simulator with a versioned MQTT API and an ``fsm::v2`` state
machine. Coexists with ``EvManager`` and is targeted as its replacement.

Configuration
=============

``connector_id``
    Connector id of the EVSE manager to which this simulator is connected to.

``ac_nominal_voltage``
    Nominal AC voltage between phase and neutral in Volt. Default: ``230``.

``max_current_a``
    AC max current in Ampere. Default: ``16``.

``three_phases``
    Support three phase. Default: ``true``.

``dc_max_current_limit``
    Maximum current allowed by the EV. Default: ``300``.

``dc_max_power_limit``
    Maximum power allowed by the EV. Default: ``150000``.

``dc_max_voltage_limit``
    Maximum voltage allowed by the EV. Default: ``900``.

``dc_energy_capacity``
    Energy capacity of the EV. Default: ``60000``.

``dc_target_current``
    Target current requested by the EV. Default: ``5``.

``dc_target_voltage``
    Target voltage requested by the EV. Default: ``200``.

``soc_initial_pct``
    SoC at start of a simulated charging process. Default: ``30``.

``departure_time_s``
    Departure time in seconds after the session starts. Default: ``86400``.

``e_amount_wh``
    Energy amount in Wh that should be charged during the session. Default: ``0``.

``force_payment_option``
    Force the use of the selected payment option. Default: ``false``.

``keep_cross_boot_plugin_state``
    Keep plugin state across boot (use for simulation only). Default: ``false``.

``publish_bsp_measurements``
    Whether to publish synthetic BSP measurements. Default: ``false``.

``tick_interval_ms``
    FSM tick interval in milliseconds. Default: ``100``.

``cfg_communication_check_to_s``
    Communication check timeout in seconds. Default: ``5``.

``cfg_heartbeat_interval_ms``
    Heartbeat publish interval in milliseconds. Default: ``1000``.

External MQTT
-------------

The module exposes a versioned, typed MQTT API. All topics share the prefix:

::

    <mqtt_external_prefix>everest_api/1/ev_simulator/<module_id>/<m2e|e2m>/<suffix>

``m2e/*`` carries inbound commands (manager-to-EV); ``e2m/*`` carries outbound
state and events (EV-to-manager). Payload shapes for every suffix are defined
in the typed schema at
``lib/everest/everest_api_types/include/everest_api_types/ev_simulator/API.hpp``.

Inbound suffixes (``m2e/*``):

- ``enable``
- ``plug``
- ``unplug``
- ``stop_session``
- ``pause_session``
- ``resume_session``
- ``clear_fault``
- ``query_state``
- ``set_soc``
- ``start_session``
- ``set_charging_current``
- ``inject_fault``
- ``bcb_toggle``
- ``run_scenario``
- ``communication_check``
- ``raise_error``
- ``clear_error``

Outbound suffixes (``e2m/*``):

- ``state``
- ``ev_info``
- ``bsp_event``
- ``slac_state``
- ``iso_session_event``
- ``fault``
- ``command_ack``
- ``heartbeat``
- ``bsp_measurement``

Scenarios
---------

The ``run_scenario`` command selects a preset by ``ScenarioName``.

Implemented in v1:

- ``AcIecBasic``
- ``AcIsoBasic``
- ``DcIsoBasic``

Deferred (currently rejected via ``e2m/command_ack`` with
``reason: "scenario not implemented in v1"``):

- ``AcIecPauseResume``
- ``AcIsoD20Basic``
- ``DcIsoD20Basic``
- ``DcIsoPauseResume``
- ``DcIsoBpt``
- ``DcIsoMcs``
- ``DiodeFailSmoke``

Command rejection
-----------------

Out-of-state or unsupported commands are not silently dropped. They are
acknowledged on ``e2m/command_ack`` with ``status: "Rejected"`` and a
``reason`` string identifying why the command was refused. Accepted commands
likewise publish ``status: "Accepted"`` on the same channel where applicable.
