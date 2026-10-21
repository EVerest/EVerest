# EVerest API client

An interactive command line client for the EVerest API — the stable,
MQTT-based external API provided by the modules under `modules/API/EVerestAPI`.
It can act as any external counterpart of those modules (send commands,
publish values, answer requests) and spy on all EVerest API traffic. Use it to
bring up, test, or debug an external module integration without writing code.

## How it works

There is no code generation and nothing to build or install. At startup
`everest_api_client.py` reads the AsyncAPI specs of the EVerest API
(`docs/source/reference/EVerest_API/*_API.yaml`), builds a model of every API
in memory and creates its commands and MQTT clients from it. A changed or added
spec is picked up the next time the client starts.

- `everest_api_client.py` — the entry point.
- `everest_api/` — the [cmd2](https://cmd2.readthedocs.io) shell (`shell.py`),
  the spec model (`model.py`, `spec_loader.py`, `specs.py`), the per-API
  commands (`commands.py`), the MQTT client (`mqtt_client.py`) and the spy
  (`spy.py`).

Each spec is an API named after its file: `powermeter_API.yaml` is the API
`powermeter`. `client_add powermeter <module_id>` adds a client for one module
of that API and creates its commands, one per operation of the spec, named
`<module_id>.<operation>`:

```
(Cmd) client_add powermeter pm
(Cmd) pm.send_powermeter_values {...}
(Cmd) pm.receive_request_start_transaction
```

`client_remove <module_id>` removes the client and its commands again. Several
modules of the same API are added under their own ids
(`meter1.send_powermeter_values`, `meter2.send_powermeter_values`). `apis`
lists the loaded APIs and their topic prefix, `apis <api>` the operations of
one API. A spec that cannot be processed is reported at startup and only that
API is missing.

## Requirements

Python 3.8+ with the packages in `requirements.txt`:

```sh
pip install -r applications/utils/everest-api-client/requirements.txt
```

## Usage

```sh
applications/utils/everest-api-client/everest_api_client.py [--spec-dir DIR] [COMMAND ...]
```

The specs are read from `--spec-dir`, else from `$EVEREST_API_SPEC_DIR`, else
from the source tree the script is in. Further arguments are run as commands
on startup, e.g. `"run_script examples/ac_charging_session.script"`.

The client connects to the MQTT broker given by the environment variables
`MQTT_BROKER`/`MQTT_SERVER_ADDRESS`/`MQTT_HOST` (default `localhost`) and
`MQTT_BROKER_PORT`/`MQTT_SERVER_PORT`/`MQTT_PORT` (default `1883`).

```
$ ./everest_api_client.py
(Cmd) client_add spy                         # spy on everest_api/#
(Cmd) client_add powermeter my_meter         # module_id from your EVerest config
(Cmd) my_meter.send_powermeter_values {...}
(Cmd) client_remove spy                      # a spy's id is 'spy', or its topic if one was given
```

Payloads are JSON, written without spaces or quoted as a whole
(`'{"a": 1}'`). `help` lists all commands, grouped by client. Useful settables
(`set <name> <value>`): `show_topic`, `hide_topic`, `quiet`, `mute`,
`prettify`, `colors`, `replyToPlaceholder`.

### Answering requests (replyTo)

Requests from EVerest carry a `replyTo` address in their payload headers, and
the `<module_id>.send_reply_*` commands need it as their first argument. The shell
caches every `replyTo` it sees in incoming messages, so you never have to
copy it by hand:

- press TAB on the replyTo argument; it completes from the client's own
  addresses: `powermeter_api.send_reply_stop_transaction <TAB>`
- or type `@last` (most recent) / `@1`, `@2`, … as a word of a command
- `replyto` lists the cached addresses with their `@<n>` shortcuts

The cache fills from the messages of the added clients and spies.

In the other direction, a request you send gets a fresh reply address when
`headers.replyTo` in its payload is the `replyToPlaceholder`
(`everest_api/1/{interface_type}/{module_id}/e2m/{operation_name}/{uuid}`):
it is replaced by the request topic with `e2m` instead of `m2e` plus a unique
id. TAB on the payload of a request inserts the example from the spec with the
placeholder already in place, and the reply is received with the matching
`<module_id>.receive_reply_*`:

```
(Cmd) client_add lifecycle
(Cmd) lifecycle.send_request_get_everest_version <TAB>
(Cmd) lifecycle.send_request_get_everest_version {"headers":{"replyTo":"everest_api/1/{interface_type}/{module_id}/e2m/{operation_name}/{uuid}"}}
(Cmd) lifecycle.receive_reply_get_everest_version --timeout 5
```

`help replyto` and the help of each request and reply command explain this in
the shell.

### Waiting for a state

Two commands block so a script does not race EVerest:

- `<module_id>.receive_* [--next] [--timeout N] [--optional]` returns the
  oldest message of that operation no receive has returned yet, and blocks
  until one arrives if there is none. A request that arrived before the
  command is therefore not lost. With `--next` the messages received so far
  are dropped and it waits for a new one (e.g. for heartbeats). `@last` then
  refers to the replyTo of the request it returned.
- `wait_for <topic-substring> [value-substring] [--timeout N] [--optional]`
  blocks until a spied message matches (needs a `spy` active). Use it to
  gate on a published state, e.g. `wait_for /bsp_api/e2m/enable true` or
  `wait_for session_event Authorized`. Messages that arrived since the
  previous command started count as well, so a state published in reaction to
  that command is not missed.

A timeout of either is an error: it stops a running script (not the shell)
and sets the exit code of the client to 1. With `--optional` it prints a note
and continues instead, for a message that only appears sometimes, such as a
startup handshake that is absent when the script is run a second time
against the same EVerest.

### Quieting the console and marking phases

- `set mute true` suppresses all incoming/outgoing message logging so a script
  run is readable; `wait_for`, the receive commands and `@last` still work
  while muted (the messages are still processed, just not printed), and
  errors are still shown. `set mute false` restores it.
- `echo <text>` prints a line to the console — use it in scripts as a phase
  banner. It prints even while muted.

(`quiet`, on by default, hides heartbeat and communication_check messages;
`mute` hides everything. `set show_topic` / `hide_topic` filter by substring
when you want to see some traffic but not all: with `show_topic` only matching
topics are shown, and `hide_topic` hides matching topics from those.)

The module id passed to `client_add <api> <module_id>` must match the
module id of the corresponding EVerest API module in your EVerest
configuration; topics have the form
`everest_api/1/<api_type>/<module_id>/{e2m|m2e}/<name>`.
The configuration, lifecycle and entrypoint APIs are served under a fixed
topic, so their `client_add` takes no module id.

`<module_id>.send_communication_check true` and `<module_id>.receive_heartbeat`
are ordinary operations of the APIs that have them. The id of a client must be
unique and is part of command names and MQTT topics, so it cannot contain
whitespace, quotes, `/`, the MQTT wildcards `+` `#` or the characters
`; | < >`; every EVerest module id satisfies this.

### Example: a full AC charging session

`examples/ac_charging_session.script` drives a complete plug → authorize →
charge sequence against an EVerest started with
`config/config-test-all-external-api.yaml` (which instantiates all EVerest
API modules). Start the client, then:

```
run_script examples/ac_charging_session.script
```

It plays every counterpart of one AC EvseManager (board support, powermeter,
token provider + validator, external energy limit) and uses the blocking
`<module_id>.receive_request_*` commands so it stays in step with the manager.

`examples/all_apis_smoke.script` is a different kind of demo: it fires one
representative operation from **every** API module, including OCPP, to
confirm each API client and module round-trips. It is fire-and-forget
(no reply waits), so it never blocks and can be re-run any time.

### Gotchas when driving a live charge

The example script takes care of these:

- **Answer the startup stop-transaction.** On startup EvseManager sends a
  `stop_transaction` to the powermeter and blocks until it is answered, so the
  charger never enables until you reply (`powermeter_api.receive_request_stop_transaction`
  then `powermeter_api.send_reply_stop_transaction @last {"status":"OK"}`).
  It is sent only once, when the first powermeter value arrives or at the
  latest 5 s after EVerest is up, so start `ac_charging_session.script` right
  after EVerest starts. A request nobody saw is only answered by the
  powermeter_API's reply timeout (`cfg_request_reply_to_s`, 550 s by default).
- **Send capabilities before events.** EvseManager needs `<bsp>.send_capabilities`
  to initialize phases and enable the port; CP events sent before it are
  dropped ("Ignoring BSP Event, BSP is not enabled yet"). `max_current_A_import`
  in the capabilities is the charging current ceiling.
- **Grant an energy budget, and keep it fresh.** With no external limit the
  charger falls back to 0 A ("Power budget expired"). Send
  `<eel>.send_set_external_limits` with a **non-zero `limits_to_leaves`** and **no
  zero setpoint** (a `schedule_setpoints` entry of 0 caps the connector at 0),
  and re-send periodically — the limit expires (`valid_for` ~10s). The powermeter
  value is similarly periodic.
- **`tariff_messages` is required** in a validate-token reply (empty list is
  fine).
- **Read the `source` field** of `enforced_limits` to see which node is
  binding — e.g. `connector_1/evse_board_support_caps` means your BSP
  capabilities are the limit, `api` means your external energy limit is.

## Tests

```sh
python3 -m pytest applications/utils/everest-api-client/tests
```

The tests load every spec of the tree and need no MQTT broker.
