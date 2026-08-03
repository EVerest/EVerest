.. _everest_modules_handwritten_EvManager:

.. =========
.. EvManager
.. =========

This Module implements the car simulator for a charging session.

Configuration
=============

``connector_id``
    The connector id of the EVSE Manager
    to which the simulator connects to.

External MQTT
-------------

The module listens to the following MQTT topics:

``everest_external/nodered/{connector_id}/carsim/cmd/enable``
    | Used to enable the car simulator.
    | Possible values are:

    - ``true``
    - ``false``

``everest_external/nodered/{connector_id}/carsim/cmd/execute_charging_session``
    | Used to execute a charging session based on the semicolon separated provided command string.
    
    ::

        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC;iso_wait_pwr_ready;sleep 36000"

    | (For all available commands see: :ref:`Simulator Commands <everest_modules_handwritten_EvManager_simulator_commands>`)

``everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session``
    | Used to modify the current charging session.
    | Follows the same format as ``execute_charging_session``.

The module publishes to the following MQTT topics:

``everest_external/nodered/{connector_id}/carsim/state/evcc_id``
    | Published whenever a ``set_evcc_id`` simulator command runs. Carries the EVCCID that the EV
      will announce, in the ``VID:`` form the charging station uses as the Autocharge token, or
      ``rejected`` if the requested value was not a MAC address.
    | Published retained, so a dashboard connecting later still sees the vehicle being simulated.

.. _everest_modules_handwritten_EvManager_simulator_commands:

Simulator Commands
------------------

``sleep {time in seconds}``
    | Sleeps for the specified time.
    | Example: ``sleep 10``

``set_evcc_id [{mac address}]``
    | Overrides the EVCCID announced by the simulated EV, so that one simulator can present itself
      as a series of different vehicles. ``EvseManager`` turns the EVCCID into the Autocharge token
      ``VID:{mac address}`` and sends it to the CSMS as the ``Authorize`` idTag.
    | Accepts an optional ``VID:`` prefix and any of the usual separators. Called without an
      argument it drops the override and goes back to the MAC address of the network interface.
    | The EVCCID is fixed at ``SessionSetupReq``, so put this in front of the command that starts
      the V2G session for it to apply to that session.
    | Only available when an ``ISO15118_ev`` implementation is connected.
    | Examples:

    ::

        "set_evcc_id 0242AC110099;iso_wait_slac_matched;iso_start_v2g_session DC 86400 0"
        "set_evcc_id VID:0242AC110099"
        "set_evcc_id 02:42:AC:11:00:99"
        "set_evcc_id"

``test``
