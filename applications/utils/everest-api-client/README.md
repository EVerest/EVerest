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

Payloads are JSON: the payload is the rest of the line, as typed, so it may
contain spaces. `help` lists all commands, grouped by client. Useful settables
(`set <name> <value>`): `show_topic`, `hide_topic`, `quiet`, `mute`,
`prettify`, `colors`, `replyToPlaceholder`.

### Answering requests (replyTo)

Requests from EVerest carry a `replyTo` address in their payload headers, and
the `<module_id>.send_reply_*` commands need it as their first argument. The shell
caches every `replyTo` it sees in requests from EVerest (`e2m` topics), so you
never have to copy it by hand:

- press TAB on the replyTo argument; it completes from the client's own
  addresses: `powermeter_api.send_reply_stop_transaction <TAB>`
- or type `@last` (most recent) / `@1`, `@2`, … as a word of a command; if
  there is no such address, the command is not run and a running script stops
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

### Variables and rules (on, every)

Values can be kept and reused, and commands can run by themselves:

- `let <name> <value>` sets a variable (JSON if the value is JSON, text
  otherwise), `let --default <name> <value>` only if it is not set yet; `<module_id>.receive_* --save <name>` stores the received
  payload. `${name}`, `${name.key}` and `${name.list[0]}` are replaced by the
  value in any command (strings as they are, other values as compact JSON).
  `vars` lists the variables.
- `${= expression}` is replaced by the value of an expression over the
  variables: numbers, strings, `true` / `false`, lists and dicts, `+ - * / // %`,
  comparisons including `in`, `and` / `or` / `not`, `x if condition else y`,
  `min max abs round int float`, `now()` (the current UTC time as a timestamp),
  `json(value)` (the value as JSON, e.g. a string quoted and escaped to put it
  into a payload) and `a.key` / `a[0]`. For example
  `let energy ${= energy + amps * 230 / 3600}` or
  `pm.send_x {"text": ${= json(msg.text)}}`. Nothing else can be evaluated, and
  the size of repeated strings and formatted numbers is limited.
- `on [--if <condition>] [--once] <module_id>.<receive_operation> <command>`
  runs the command whenever that client receives a message. In the command,
  `${msg}` is the received payload (`${msg.headers.replyTo}`, ...) and
  `${topic}` its topic. A condition is `<path>=<value>`
  (`--if msg.allow_power_on=true`) or an expression
  (`--if ${= msg.voltage_V > limit}`). The client may be added later.
- `every [--if <condition>] [--count N] <seconds> <command>` runs the command
  periodically, with `--if` only while the condition holds (e.g.
  `--if imd_active=1`). The interval and the count may be variables.
- A rule runs `<module_id>.send_*` commands, `let` and `echo`; several
  commands separated by `;` run in order, e.g.
  `every 1 let energy ${= energy + power / 3600}; pm.send_powermeter_values ...`.
  The `${...}` in a command are expanded right before it runs, so it sees the
  variables the commands before it set. The conditions are checked right
  before the commands run.
  `rules` lists the rules and `rule_remove <n>` / `rule_remove all` removes
  them.
- Rules are global and run one after the other on one thread. The rules that
  match a message run in the order they were created, so a `let` rule can
  prepare a value for the next rule. Messages of different clients, the
  `every` rules and the commands of the shell itself are not ordered with
  respect to each other: what has to happen together belongs into one rule.
- An error in the definition of a rule or variable, a missing variable and
  an invalid payload stop a running script, like a timeout; errors while a
  rule runs are printed as `ERROR:`.

The shell does not redirect output: `>` and `|` are kept as typed, as
expressions need them.

For example, answering every powermeter start-transaction request, sending a
powermeter value every 2 s, and forwarding a car's CP state to the charger's
board support:

```
(Cmd) on pm.receive_request_start_transaction pm.send_reply_start_transaction ${msg.headers.replyTo} {"status":"OK"}
(Cmd) let meter {"energy_Wh_import":{"total":0},"timestamp":"2026-01-01T00:00:00Z"}
(Cmd) every 2 pm.send_powermeter_values ${meter}
(Cmd) on ev_bsp.receive_set_cp_state bsp.send_event {"event":"${msg}"}
```

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
API modules). The config contains the standalone module `ocpp_cli`
(BUOcppConsumer): EVerest only finishes starting up once it runs. From the
repository root, after `cmake --build build --target install`, use three
terminals in this order:

1. EVerest, which then waits for `ocpp_cli`:

   ```
   build/run-scripts/run-test-all-external-api.sh
   ```

2. The client, running the script. It waits up to 30 s for EVerest's startup
   handshake, so it can be started before EVerest is up:

   ```
   cd applications/utils/everest-api-client
   ./everest_api_client.py "run_script examples/ac_charging_session.script"
   ```

3. The standalone `ocpp_cli`, which lets EVerest finish starting up (use
   `build/dist/lib64` if your libraries are installed there):

   ```
   LD_LIBRARY_PATH=build/dist/lib build/dist/libexec/everest/modules/BUOcppConsumer/BUOcppConsumer --module ocpp_cli
   ```

The script then runs through to `Session finished`. To run it again, restart
EVerest and `ocpp_cli` first: the startup handshake happens once per start.
Without the `ocpp_consumer_API`, remove `ocpp_api` and `ocpp_cli` from the
config; step 3 is then not needed.

It plays every counterpart of one AC EvseManager (board support, powermeter,
token provider + validator, external energy limit) and uses the blocking
`<module_id>.receive_request_*` commands so it stays in step with the manager.

`examples/all_apis_smoke.script` is a different kind of demo: it fires one
representative operation from **every** API module, including OCPP, to
confirm each API client and module round-trips. It is fire-and-forget
(no reply waits), so it never blocks and can be re-run any time.

### Simulators

`examples/simulators/` plays the simulation modules of EVerest through the
EVerest API, each with fixed client ids that match the test configs. They
are building blocks for session scripts, run with `@@ simulators/<name>.script`
from a script in `examples/`, and leave their rules running.

| Script | Simulates | Clients |
|---|---|---|
| `cable.script` | the charging cable of the YetiSimulator: CP state, PWM, relays | `bsp_api`, `ev_bsp_api` |
| `ac_powermeter.script` | the AC powermeter of the YetiSimulator, from the current the car draws | `powermeter_api` |
| `yeti.script` | the YetiSimulator: `cable.script` and `ac_powermeter.script` | |
| `dc_power_supply.script` | the DCSupplySimulator and its DC powermeter | `psu_api`, `dc_powermeter_api` |
| `imd.script` | the IMDSimulator | `imd_api` |
| `ovm.script` | the OVMSimulator, from the DC supply's voltage | `ovm_api` |
| `token_provider.script` | the DummyTokenProvider: a token when a session starts | `token_provider_api`, `evse_manager_api` |
| `token_validator.script` | the DummyTokenValidator | `token_validator_api` |
| `session_cost.script` | the DummySessionCostProvider | `session_cost_api` |

Each script documents its variables; the ones set with `let --default` can be
set before the script runs to configure it, like a module's config (e.g.
`let token 123456` before `@@ simulators/token_provider.script`). Simulators
share state through variables: `ovm.script` reports the `dc_voltage` of
`dc_power_supply.script`. The SlacSimulator stays a module, as the car side of
SLAC has no EVerest API.

### Example: AC charging with ISO 15118-2

`examples/ac_iso15118_2_session.script` charges with ISO 15118-2 against an
EVerest started with `config/config-test-ac-iso15118-external-api.yaml`. The
ISO 15118 stacks are real EVerest modules: the charger (`Evse15118D20`) and a
simulated car (`PyEvJosev` and `EvManager`), connected by the `SlacSimulator`.
Everything else is played by the simulators (`yeti.script`, the token
provider and validator); the session script follows the session. The car plugs in by itself a few seconds after EVerest is up, so
start the script right after EVerest:

```
run_script examples/ac_iso15118_2_session.script
```

The ISO 15118 modules need a network interface with an IPv6 link-local
address (`device: auto` picks the first one).

### Example: DC charging with ISO 15118-2 or DIN SPEC 70121

`examples/dc_session.script` charges DC against an EVerest started with
`config/config-test-dc-iso15118-external-api.yaml`. Like the AC example, the
ISO 15118 / DIN 70121 stacks and the car are EVerest modules; the cable, the
DC power supply with its powermeter, the isolation monitor, the over voltage
monitor and the token provider and validator are the simulators. The session
goes through cable check, pre-charge and current demand. The car uses
ISO 15118-2; with its `supported_ISO15118_2` set to false in the config it
charges with DIN SPEC 70121, and the header of the config says how to switch
it to ISO 15118-20 (TLS 1.3). The same goes for the AC config.

### Gotchas when driving a live charge

The example script takes care of these:

- **Answer the startup stop-transaction.** On startup EvseManager sends a
  `stop_transaction` to the powermeter and blocks until it is answered, so the
  charger never enables until you reply (`powermeter_api.receive_request_stop_transaction`
  then `powermeter_api.send_reply_stop_transaction @last {"status":"OK"}`).
  It is sent only once, when the first powermeter value arrives or at the
  latest 5 s after EVerest is up; `ac_charging_session.script` waits up to 30 s
  for it, so start the script before EVerest is up or right after. A request nobody saw is only answered by the
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
