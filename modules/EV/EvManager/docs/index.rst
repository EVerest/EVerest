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

``session_logging``
    | Logs the id of every V2G response the EV accepted to the console, the EV-side
      counterpart of the EvseManager option of the same name. The ISO15118_ev provider
      reports no requests, so every line reads ``EVSE ISO V2G <message>``.
    | Default: ``false``

``dc_min_voltage_limit``
    | Minimum voltage in V the EV can accept, sent to the ISO15118_ev provider with the
      other DC parameters at startup. It is emitted into DC_PreChargeReq and
      DC_ChargeLoopReq, where it is mandatory.
    | Default: ``150``

External MQTT
-------------

The module listens to the following MQTT topics:

``everest_external/nodered/{connector_id}/carsim/cmd/enable``
    | Used to enable or disable the car simulator.
    | Disabling an active vehicle discards its running command list and unplugs it
      before disabling the board support. Re-enabling does not resume that list.
      An idle plugged-in vehicle stays plugged in.
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

Ending a command list does not stop charging. The vehicle keeps its active
state until a stop command, a charger stop, or an unplug ends the session.
Unless the vehicle is at A or B, ``execute_charging_session`` logs a
refusal, even if no list is running (this includes E, diode fault and BCB
toggle). With an ISO15118_ev connected, use ``modify_charging_session`` to
send a stop command such as ``iso_stop_charging``. At A or B, execute can
replace a parked or waiting list, or start another list after one finishes.

In basic AC charging (``CHARGING_REGULATED``), an ended list leaves no
state-machine ticks running. PWM changes, including an EVSE pause by PWM,
are not followed until a list runs again.

.. _everest_modules_handwritten_EvManager_simulator_commands:

Simulator Commands
------------------

``sleep {time in seconds}``
    | Sleeps for the specified time.
    | Example: ``sleep 10``

``test``

``cp_c_pulse {time in seconds}``
    | Holds the control pilot in state C for the given time, then releases it to B
      (to A if the vehicle is unplugged). A command list that replaces it mid-pulse
      releases the pilot the same way.
    | On MCS this is the readiness claim the EV firmware turns into the wake-up toggle
      of a sleeping EVSE; hold it for at least 2 seconds.
    | Example: ``cp_c_pulse 4``

``iso_wait_pwm_is_running``
    | Sets the vehicle plugged in (CP B) and waits until the EVSE drives a PWM duty
      cycle between 4 % and 97 %.

``iso_wait_pwm_is_running {timeout in seconds}``
    | Like ``iso_wait_pwm_is_running``, but continues without PWM once the timeout
      expires, so matching can start on the CP state alone against an EVSE that
      keeps a static +12 V.
    | Example: ``iso_wait_pwm_is_running 10``

``iso_start_v2g_session {energy mode}[,{payment option}][,{departure time},{energy amount}]``
    | Asks the ISO15118_ev provider to start a V2G session. Payment option is
      ``auto`` (default), ``externalpayment`` or ``contract``. Departure time and
      energy amount default to the ``departure_time`` and ``e_amount`` configuration.
    | Energy modes:

    - ``ac``: AC, single phase or three phase per ``three_phases``
    - ``ac_bpt``: AC bidirectional power transfer (ISO 15118-20)
    - ``ac_der``: AC with IEC DER control (ISO 15118-20)
    - ``dc``: DC
    - ``dc_bpt``: DC bidirectional power transfer (ISO 15118-20)
    - ``mcs``: Megawatt Charging System, an ISO 15118-20 DC service of its own

    | ``three_phases`` selects the phase count an AC session starts with (and the
      single or three phase service for ``ac``); ``iso_draw_power_regulated`` sets
      the phases it draws on. DC modes use the DC power path. An unknown energy mode, or a start the provider rejects,
      fails the command and ends the command list.
    | Example: ``iso_start_v2g_session dc_bpt,contract``

``iso_wait_for_stop {time in seconds}``
    Requests a stop when the timer expires or the charger requests it. CP stays
    in C until ``power_delivery_stopped`` confirms PowerDelivery(Stop), or the
    V2G session ends. MCS ignores ``power_delivery_stopped`` and holds C until
    session end because C-exit under power latches CEFAULT on the bench and release
    after an accepted Stop has not been verified there. The hold expires after
    10 seconds if the session has not ended and no applicable stop signal arrives.
    The contactor then opens and CP stays in B for the stop dwell.
