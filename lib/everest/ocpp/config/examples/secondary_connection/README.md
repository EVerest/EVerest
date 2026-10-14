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
it, restricts it with all three of `UpstreamMsgs`, `DownstreamMsgs` and `AllowSetVariables`, and
sends the asynchronous messages answering a request back to the connection that asked for it
(section 5.3): the `NotifyReport`s of a `GetBaseReport` or `GetReport`, the
`NotifyMonitoringReport`s of a `GetMonitoringReport`, the `NotifyCustomerInformation` of a
`CustomerInformation`, the `LogStatusNotification`s of a `GetLog`, and the
`FirmwareStatusNotification`s of an `UpdateFirmware`. A connection that wants to see the firmware
status of an update *another* connection asked for sets
`EnableFirmwareStatusNotificationOtherConnection` to `true` (section 5.3.2).

Being allowed to ask a question does not by itself allow the answer to be sent: a connection granted
`GetBaseReport` in `DownstreamMsgs` also needs `NotifyReport` in `UpstreamMsgs`, and so on for each
of the pairs above.

Not implemented yet: the monitor routing of section 5.5, the connection-scoped security events of
section 5.6, the per-connection meter values of section 4.5, and the per-connection TLS trust
anchors of section 3.4.3. The instanced `AlignedDataCtrlr`, `SampledDataCtrlr`, `MonitoringCtrlr`
and `SecurityCtrlr` components the application note also describes will be added together with the
features that read them.
