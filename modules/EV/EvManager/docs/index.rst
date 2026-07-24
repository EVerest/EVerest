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

``everest_external/nodered/{connector_id}/carsim/cmd/set_evcc_id``
    | Used to override the EVCCID that the simulated EV announces in ``SessionSetupReq``,
      so that one simulator can present itself as a series of different vehicles.
    | Accepts an optional ``VID:`` prefix and any of the usual separators. An empty payload
      restores the MAC address of the network interface.
    | Takes effect on the next V2G session, so set it before plugging in.
    | Example values:

    - ``VID:0242AC110099``
    - ``02:42:AC:11:00:99``
    - ``0242ac110099``

The module publishes to the following MQTT topics:

``everest_external/nodered/{connector_id}/carsim/state/evcc_id``
    | The EVCCID that will be announced, in the ``VID:`` form the charging station uses as the
      Autocharge token, or ``rejected`` if the requested value was not a MAC address.
    | Published retained, so a dashboard connecting later still sees the vehicle being simulated.

.. _everest_modules_handwritten_EvManager_simulator_commands:

Simulator Commands
------------------

``sleep {time in seconds}``
    | Sleeps for the specified time.
    | Example: ``sleep 10``

``set_evcc_id {mac address}``
    | Overrides the EVCCID announced by the simulated EV from the next V2G session onwards.
    | Only available when an ``ISO15118_ev`` implementation is connected.
    | Example: ``set_evcc_id 0242AC110099``

``test``
