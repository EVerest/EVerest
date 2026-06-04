# SHM to MQTT Inspection Bridge

`shm_to_mqtt_bridge` is an observability example for the reusable SHM transport. It uses
`everest::lib::io::shm::shm_client` to subscribe to SHM topics and republishes received payloads
unchanged to MQTT.

The bridge is one-way only:

```text
SHM topics -> shm::shm_client -> bridge callback -> MQTT publish
```

Each bridge process becomes an additional SHM subscriber on every topic it monitors and therefore
participates in SHM back-pressure. Subscribing to large topic sets (whole-EVerest monitoring) makes
the bridge a real consumer that ACKs must drain; size the bridge process and its MQTT publish path
accordingly.

Whole-EVerest monitoring also increases file-descriptor pressure. The SHM transport already needs
an elevated `nofile` limit for larger configurations, and `subscribe_all: true` adds one bridge
subscriber to every registered SHM topic. If startup or bridge subscription fails with
`failed to create an eventfd: Too many open files`, raise the soft limit in the shell or service
that starts Manager and the bridge, for example `ulimit -n 4096`.

Only the framework-local SHM transport flows through the bridge. External MQTT (`external_mqtt`)
and framework MQTT telemetry stay on the MQTT side channel and should be observed directly on MQTT.

## Selecting topics

The bridge supports three independent selectors for SHM topics. They combine as a deduplicated
union; at least one of them must be specified.

- `shm.topics` — explicit exact topic allowlist. Each entry must be an exact, wildcard-free SHM
  topic. The bridge subscribes to each entry directly.
- `shm.subscribe_all: true` — discover every exact topic registered with the SHM Manager via a
  control-protocol registry query and subscribe to all of them.
- `shm.topic_filters` — MQTT-compatible wildcard filters (`+` and `#`). The bridge queries the
  Manager-owned registry and expands each filter to the matching exact topics. Filters that do not
  match any registered topic emit a warning and are ignored. Invalid filter syntax is rejected at
  config-parse time.

Wildcards are accepted only in `shm.topic_filters`. The low-level SHM transport itself accepts
exact topics only; the bridge performs filter expansion locally against the discovered registry.

Topic discovery uses a public SHM control-protocol path (`list_topics`); the bridge does not parse
the SHM segment directly.

## Examples

Explicit allowlist (default sample):

```bash
shm_to_mqtt_bridge lib/everest/io/examples/shm_to_mqtt_bridge/shm_to_mqtt_bridge.yaml
```

With the sample config, SHM topic `ping_pong/ping` is published as MQTT topic
`everest/shm_inspect/ping_pong/ping`.

Monitor every framework-local SHM topic:

```yaml
shm:
  client_id: shm-mqtt-bridge
  control_socket: /tmp/everest-shm-control.sock
  subscribe_all: true
```

Monitor a single module subtree with a wildcard:

```yaml
shm:
  client_id: shm-mqtt-bridge
  control_socket: /tmp/everest-shm-control.sock
  topic_filters:
    - everest/modules/connector_1/#
```

Combine an explicit allowlist with a filter:

```yaml
shm:
  client_id: shm-mqtt-bridge
  control_socket: /tmp/everest-shm-control.sock
  topics:
    - ping_pong/ping
  topic_filters:
    - everest/+/status
```
