.. _everest_modules_handwritten_EvSimulator:

===========
EvSimulator
===========

Simplified EV simulator with a versioned MQTT API and an ``fsm::v2`` state
machine. Coexists with ``EvManager`` and is targeted as its replacement.
Implements all 12 declared scenarios.

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

The ``run_scenario`` command selects a preset by ``ScenarioName``. All 12
scenarios below are implemented and dispatch through the FSM:

- ``AcIecBasic`` — Basic AC IEC 61851 session: Plug, StartSession, charge
  for 30 s.
- ``AcIecPauseResume`` — AC IEC session that pauses at 30 s then resumes at
  60 s, stops at 120 s.
- ``AcIsoBasic`` — Basic AC ISO 15118-2 session with EIM payment.
- ``AcIsoD20Basic`` — Basic AC ISO 15118-20 session.
- ``DcIsoBasic`` — Basic DC ISO 15118-2 session with EIM payment.
- ``DcIsoD20Basic`` — Basic DC ISO 15118-20 session.
- ``DcIsoPauseResume`` — DC ISO 15118-2 session that pauses then resumes
  before stopping.
- ``DcIsoBpt`` — DC ISO 15118-20 with Bidirectional Power Transfer.
- ``DcIsoMcs`` — DC ISO 15118-20 with Megawatt Charging System.
- ``DiodeFailSmoke`` — Smoke test that injects a diode fault and verifies
  the session aborts.
- ``AcIecRampUp`` — AC IEC session that ramps current from 0 to 8 to 16 to
  32 A.
- ``DcIsoTaper`` — DC ISO-2 with a tapering current profile
  (100 to 50 to 20 to 5 A).

Command rejection
-----------------

Out-of-state commands are not silently dropped. They are acknowledged on
``e2m/command_ack`` with ``status: "Rejected"`` and a ``reason`` string
identifying why the command was refused. Accepted commands likewise publish
``status: "Accepted"`` on the same channel where applicable.

Python test controller
----------------------

Integration tests drive the simulator through
``EvSimulatorTestController``. Minimal usage:

.. code-block:: python

    from everest.testing.core_utils.controller.evsim_test_controller import EvSimulatorTestController

    def test_my_scenario(everest_core, evsim_test_controller):
        evsim_test_controller.start()
        evsim_test_controller.plug_in_dc_iso(payment_type="eim")
        assert evsim_test_controller.state_collector.wait_for_state("Charging", timeout=30)
        evsim_test_controller.plug_out_iso()

For declarative charging profiles, run a preset by name:

.. code-block:: python

    evsim_test_controller.run_scenario("DcIsoTaper")

For ad-hoc current changes, ramp to a target:

.. code-block:: python

    evsim_test_controller.ramp_to_current(target_a=16, three_phases=True, duration_s=5)

SIL configurations
------------------

The following SIL configs wire the simulator to representative EVSE stacks:

.. list-table::
   :header-rows: 1
   :widths: 36 8 12 10 8 8

   * - Config
     - AC
     - DC ISO-2
     - ISO-20
     - BPT
     - MCS
   * - ``config-sil-evsim.yaml``
     - ✓
     -
     -
     -
     -
   * - ``config-sil-evsim-dc.yaml``
     -
     - ✓
     -
     -
     -
   * - ``config-sil-evsim-dc-isomux.yaml``
     -
     - ✓
     - ✓
     -
     -
   * - ``config-sil-evsim-dc-d20.yaml``
     -
     -
     - ✓
     -
     -
   * - ``config-sil-evsim-dc-bpt.yaml``
     -
     -
     - ✓
     - ✓
     - ✓
