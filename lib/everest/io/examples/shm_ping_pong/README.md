# SHM Ping-Pong Example

This directory contains a multi-process SHM example intended for live inspection through
`shm_to_mqtt_bridge`.

Processes:

```text
shm_ping_pong_server
  owns the SHM segment and UDS control socket

shm_ping_pong_client_a
  publishes JSON ping payloads to ping_pong/ping every 500 ms
  subscribes to ping_pong/pong

shm_ping_pong_client_b
  subscribes to ping_pong/ping
  publishes JSON pong replies to ping_pong/pong

shm_to_mqtt_bridge
  subscribes to both SHM topics and republishes unchanged payloads to MQTT
```

Default resources:

```text
SHM name:       /everest-shm-ping-pong
Control socket: /tmp/everest-shm-ping-pong.sock
SHM topics:     ping_pong/ping, ping_pong/pong
MQTT topics:    everest/shm_inspect/ping_pong/ping
                everest/shm_inspect/ping_pong/pong
```

Build the example targets:

```bash
cmake --build build --target shm_ping_pong_server shm_ping_pong_client_a shm_ping_pong_client_b -j2
```

Run from separate terminals:

```bash
./build/lib/everest/io/examples/shm_ping_pong_server
./build/lib/everest/io/examples/shm_to_mqtt_bridge lib/everest/io/examples/shm_ping_pong/shm_ping_pong_bridge.yaml
./build/lib/everest/io/examples/shm_ping_pong_client_b
./build/lib/everest/io/examples/shm_ping_pong_client_a
```

Observe through MQTT:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'everest/shm_inspect/ping_pong/#' -v
```

Each binary accepts the same minimal overrides:

```bash
--control-socket /tmp/other-everest-shm.sock --shm-name /other-everest-shm
```

If the local environment blocks Unix domain socket binding with `EPERM`, the server prints a clear skip message and
exits successfully. Clients report a connection error until the server is available.
