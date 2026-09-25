.. _exp-telemetry:

#########
Telemetry
#########

Telemetry is data about a charging station that helps to operate and diagnose it but that no other module
acts on: hardware temperatures, firmware states, counters of events, communication statistics. EVerest
modules declare their telemetry in their manifest and publish it through generated typed handles. A telemetry
receiver, the TelemetryRouter module, forwards it to backends such as the OCPP device model, according to
rules that decide what is dropped and what goes where.

The design, the wire protocol and the plan for the legacy telemetry API are described in
`Telemetry.md <https://github.com/EVerest/EVerest/blob/main/lib/everest/framework/docs/Telemetry.md>`_.

*******************
Declaring telemetry
*******************

The ``telemetry`` section of a module manifest maps element names to declarations:

.. code-block:: yaml

  telemetry:
    temperature:
      kind: gauge
      type: number
      unit: Celsius
      description: Board temperature
    plug_ins:
      kind: counter
      type: integer
      description: Plug-in events
    firmware_state:
      kind: state
      type: string
      enum: [Idle, Measuring, Error]
      description: Firmware state reported by the meter

There are four kinds:

gauge
  A current numeric value (``type: number`` or ``integer``), e.g. a temperature.

counter
  A monotonic count (``type: number`` or ``integer``). The framework keeps the total.

state
  A current discrete value: ``boolean``, a plain ``string``, or a string enum, declared inline with ``enum``
  or referenced from ``types/`` with ``$ref``.

event
  A structured occurrence (``type: object`` with ``$ref`` to an object type in ``types/``).

Gauges and counters may have a ``unit``. The EVSE and connector an element belongs to are not part of the
declaration; they come from the module's mapping in the configuration (see :ref:`tier_module_mapping`).

*********************
Publishing telemetry
*********************

ev-cli generates one handle per element; the module class reaches them through its ``tel`` member:

.. code-block:: cpp

  tel.temperature.set(41.2);
  tel.plug_ins.increase();
  tel.firmware_state.set(tel::types::FirmwareState::Measuring);

After adding telemetry to a manifest, regenerate the module header with ``ev-cli mod update``. Handles do
nothing while telemetry is disabled. Publishing never blocks; telemetry that cannot be delivered is dropped
and counted. Telemetry may be published from ``init()``. The example module ``TelemetryExample`` publishes one
element of every kind.

********************
Enabling telemetry
********************

.. code-block:: yaml

  active_modules:
    telemetry_router:
      module: TelemetryRouter
      config_module:
        rules_file: telemetry_rules.example.yaml
      connections:
        ocpp:
          - module_id: ocpp
            implementation_id: ocpp_generic
  settings:
    telemetry_socket_enabled: true
    telemetry_socket_path: /run/everest/telemetry.sock   # optional

Telemetry does not travel over MQTT. With ``telemetry_socket_enabled``, the manager binds a Unix datagram
socket before any module starts and hands it to the one module whose manifest sets ``telemetry_receiver``;
the configuration is rejected if there is none or more than one. All modules send their telemetry to this
socket.

The kernel queues at most ``net.unix.max_dgram_qlen`` datagrams per socket, often only 10; set it to 512 or
more (``sysctl -w net.unix.max_dgram_qlen=512``, the systemd default) so that bursts are not dropped.

``config/config-sil-telemetry.yaml`` adds TelemetryExample and TelemetryRouter to the OCPP 2.0.1 SIL
configuration. The rules file format and the sinks are described in the documentation of the TelemetryRouter
module.
