# Secondary OCPP connection: example component configuration

A secondary OCPP connection is an additional, deliberately limited OCPP connection that a charging
station keeps alongside its primary CSMS connection, as described in the OCA application note
*Secondary OCPP Connection on a Charging Station* (OCPP 2.0.1 and 2.1). Typical users are a home
energy management system, a maintenance party the operator delegates work to, or a data collector.

The connection is declared by adding an **instance of the `OCPPCommCtrlr` component** to the device
model. That is what makes the charging station report the connection in a device model report, and
it is what the charging station itself reads to discover the connection at startup.

These files are **examples, not shipped configuration**. A stock charging station has no
`OCPPCommCtrlr` instance, because reporting one tells a CSMS that the station really does offer a
secondary connection.

## Using them

Copy both files into the directory the `DeviceModelConfigPath` module configuration option points
at, alongside the standardized component configs, then edit them:

1. **Choose the connection name.** Change the `"instance"` field of `OCPPCommCtrlr_EMS.json`. The
   name is free to choose (section 3.3.1); `EMS`, `MAINT`, `MONITOR` and `DATACOLLECTION` are
   conventions that hint to the CSMS what the connection is for, and carry no meaning in the
   implementation. Use letters, digits, `_`, `.` or `-`, at most 50 characters. The file name itself
   is irrelevant; only the `"name"` and `"instance"` fields inside decide what is created.
2. **Choose the network configuration slots.** Slot numbers are station wide and a slot must belong
   to at most one connection (section 3.4.1). The shipped configuration gives slots 1 and 2 to the
   primary connection, so the example uses slot 3 and ships a matching
   `NetworkConfiguration_3.json`. List the slots in `NetworkConfigurationPriority`.
3. **Point the slot at the secondary system**: set `OcppCsmsUrl`, `SecurityProfile` and the
   credentials in `NetworkConfiguration_3.json`, or set them at runtime from the CSMS or locally.
   Do not reuse the primary connection's password.
4. **Grant the connection what it needs, and nothing more**: `UpstreamMsgs`, `DownstreamMsgs` and
   `AllowSetVariables`. All three default to empty, which means the connection may neither send nor
   receive anything and may not change any device model variable. The predefined use cases in the
   application note are simply particular values of these three variables.
5. **Switch it on** by setting `Enabled` to `true`.

Steps 3 to 5 can equally be done at runtime, from the CSMS with `SetVariables` or locally through
the `ocpp` interface's `set_variables` command, without restarting. Only adding the connection
itself, step 1, needs these files and a restart.

A connection that is switched off, has no network configuration slot, or whose only slots are
unusable is logged and ignored: a misconfigured secondary connection never prevents the charging
station from reaching its CSMS.

## What is honoured today

This is being implemented in steps. Right now the charging station discovers a configured secondary
connection, validates it, reports it on the `connections` variable of the `ocpp` interface, dials
it, and sends it only what `UpstreamMsgs` grants it. `DownstreamMsgs` and `AllowSetVariables` are
not enforced yet, so do not yet grant a secondary connection to a party you would not trust with the
whole device model. The instanced `AlignedDataCtrlr`, `SampledDataCtrlr`, `MonitoringCtrlr` and
`SecurityCtrlr` components the application note also describes will be added together with the
features that read them.
