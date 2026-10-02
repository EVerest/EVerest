# EVerest API client

An interactive command line client for the EVerest API — the stable,
MQTT-based external API provided by the modules under `modules/API/EVerestAPI`.
It can act as any external counterpart of those modules (send commands,
publish values, answer requests) and spy on all EVerest API traffic. Use it to
bring up, test, or debug an external module integration without writing code.

This tool was migrated from the basecamp repository
(`utilities/API/basecamp_api_client`).

## Architecture

The client is modular:

- `api_client.py` — a [cmd2](https://cmd2.readthedocs.io) interactive shell.
  Each API client package registers itself as a subcommand of `client_add`
  and contributes its own set of send/receive commands.
- `spyclient_api/` — a hand-written package that subscribes to the whole
  EVerest API topic tree (default prefix `everest_api`) and logs all traffic.
- One **generated** Python package per EVerest API module (21 packages, e.g.
  `powermeter_api`, `evse_bsp_api`, …). These are generated at build time from
  the AsyncAPI specs in `docs/source/reference/EVerest_API/` by the in-tree
  Python generator `everest-api-client-gen` (`applications/utils/
  everest-api-client-gen`), which uses only Jinja2 + PyYAML — no npm/asyncapi
  toolchain.

Each generated command set uses a short, unique command prefix (see the SET
table in `CMakeLists.txt`), e.g. `pm_` for the powermeter API:
`pm_send_powermeter_values`, `bsp_receive_request_enable`, …

## Build

Requirements:

- Python 3.8+ with the `build`, `jinja2` and `pyyaml` packages
  (`jinja2`/`pyyaml` come in via `ev-cli`; the build installs
  `everest-api-client-gen` into the build venv automatically)

The clients are not built by default. Enable them with:

```sh
cmake -B build -G Ninja -DEVEREST_BUILD_API_CLIENTS=ON
ninja -C build everest_api_clients
```

This generates the per-API packages, builds all wheels, and copies them to the
wheel install prefix (default: `${CMAKE_INSTALL_PREFIX}/../dist-wheels`).
`api_client.py` is installed into the binary directory on `ninja install`.

Install the wheels into a virtual environment together with the shell's
dependencies:

```sh
python3 -m venv venv && . venv/bin/activate
pip install dist-wheels/*.whl 'cmd2>=2.4,<3' 'paho-mqtt>=2.0'
```

## Usage

The client connects to the MQTT broker given by the environment variables
`MQTT_BROKER`/`MQTT_SERVER_ADDRESS`/`MQTT_HOST` (default `localhost`) and
`MQTT_BROKER_PORT`/`MQTT_SERVER_PORT`/`MQTT_PORT` (default `1883`).

```
$ ./api_client.py
(Cmd) client_add SpyClient                       # spy on everest_api/#
(Cmd) client_add PowermeterAPIClient my_meter    # module_id from your EVerest config
(Cmd) pm_send_powermeter_values my_meter '{...}'
```

`help` lists all commands; each API's commands are grouped by its category.
Useful settables (`set <name> <value>`): `show_topic`, `hide_topic`,
`prettify`, `colors`, `replyToPlaceholder`.

### Answering requests (replyTo)

Requests from EVerest carry a `replyTo` address in their payload headers, and
the `*_send_reply_*` commands need it as their second argument. The shell
caches every `replyTo` it sees in incoming messages, so you never have to
copy it by hand:

- press TAB on the replyTo argument — it completes from the cache, and once
  the module id is on the line only that client's addresses are offered:
  `pm_send_reply_stop_transaction powermeter_api <TAB>`
- or type `@last` (most recent) / `@1`, `@2`, … anywhere in a command
- `replyto` lists the cached addresses with their `@<n>` shortcuts

The cache fills from messages the shell actually receives, so subscribe to
the request (`pm_receive_request_stop_transaction …`) or run a `SpyClient`
before the request arrives.

### Waiting for a state

Two commands block so a script does not race EVerest:

- `*_receive_request_*` (and any `*_receive_*`) blocks until the next message
  on that topic arrives — use it to wait for a request before replying.
- `wait_for <topic-substring> [value-substring] [--timeout N] [--optional]`
  blocks until a spied message matches (needs a `SpyClient` active). Use it to
  gate on a published state, e.g. `wait_for /bsp_api/e2m/enable true` or
  `wait_for session_event Authorized`. On timeout it errors, unless
  `--optional` is given (then it prints a note and continues) — useful for a
  request that only appears sometimes, such as a startup handshake that is
  absent when the script is run a second time against the same EVerest.

### Quieting the console and marking phases

- `set mute true` suppresses all incoming/outgoing message logging so a script
  run is readable; `wait_for` and `@last` still work while muted (the messages
  are still processed, just not printed). `set mute false` restores it.
- `echo <text>` prints a line to the console — use it in scripts as a phase
  banner. It prints even while muted.

(`set quiet` — on by default — only hides heartbeat/communication_check
messages; `mute` hides everything. `set show_topic` / `hide_topic` filter by
substring when you want to see some traffic but not all.)

The module id passed to `client_add <Type>Client <module_id>` must match the
module id of the corresponding EVerest API module in your EVerest
configuration; topics have the form
`everest_api/1/<api_type>/<module_id>/{e2m|m2e}/<name>`.

A missing generated package only disables that API's commands (a warning is
printed at startup); the shell itself still starts.

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
`*_receive_request_*` commands so it stays in step with the manager.

`examples/all_apis_smoke.script` is a different kind of demo: it fires one
representative operation from **every** API module (all 21), including OCPP, to
confirm each generated client and module round-trips. It is fire-and-forget
(no reply waits), so it never blocks and can be re-run any time.

### Gotchas when driving a live charge

These bit us while bringing the tool up; the example script encodes the fixes:

- **Answer the startup stop-transaction.** On startup EvseManager sends a
  `stop_transaction` to the powermeter and blocks until it is answered, so the
  charger never enables until you reply (`pm_receive_request_stop_transaction`
  then `pm_send_reply_stop_transaction … @last {"status":"OK"}`).
- **Send capabilities before events.** EvseManager needs `bsp_send_capabilities`
  to initialize phases and enable the port; CP events sent before it are
  dropped ("Ignoring BSP Event, BSP is not enabled yet"). `max_current_A_import`
  in the capabilities is the charging current ceiling.
- **Grant an energy budget, and keep it fresh.** With no external limit the
  charger falls back to 0 A ("Power budget expired"). Send
  `eel_send_set_external_limits` with a **non-zero `limits_to_leaves`** and **no
  zero setpoint** (a `schedule_setpoints` entry of 0 caps the connector at 0),
  and re-send periodically — the limit expires (`valid_for` ~10s). The powermeter
  value is similarly periodic.
- **`tariff_messages` is required** in a validate-token reply (empty list is
  fine); older spec examples omitted it.
- **Read the `source` field** of `enforced_limits` to see which node is
  binding — e.g. `connector_1/evse_board_support_caps` means your BSP
  capabilities are the limit, `api` means your external energy limit is.
