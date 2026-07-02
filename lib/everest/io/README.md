# everest_io {#mainpage}

libeverest_io provides utilities for socket based communication.

Currently there are clients for
 - UDP
 - SocketCAN
 - MQTT
 - PTY
 - TCP
 - TAP
 - TLS (enabled via `EVEREST_IO_ENABLE_TLS=ON`, default ON)

The clients are single threaded and epoll based. Utilities for file descriptor based event handling are provided and used.

## TLS

Drives the libtls (`everest::tls`) `Server` and `Client` through the same
`fd_event_handler` pattern as the other clients. Disable with
`-DEVEREST_IO_ENABLE_TLS=OFF` if libtls / OpenSSL are not desired.

### Server side

`tls_listener` owns the listen socket and an embedded `tls::Server`. For each
accepted connection it yields a `std::unique_ptr<tls::tls_server>` (a
`tls_endpoint_base<tls_server_socket>`-derived register interface) to the accept
callback. The receiver registers it with the same `fd_event_handler` via
`register_event_handler(conn.get())` — the connection then drives the TLS
handshake and rx/tx on the loop internally — and keeps it alive for the
connection's lifetime. Drop the `unique_ptr` (e.g. `connections.clear()`) to tear
a connection down.

See `examples/test_tls_server.cpp` for a complete listener + echo example.

### Client side

`tls_client` is a `tls_endpoint_base<tls_client_socket>`-derived register
interface. Register it with `register_event_handler(&client)`; the TLS handshake
is driven internally on the loop by the class — no handshake hooks or
`desired_events` wiring in user code. Tear it down with
`unregister_event_handler(&client)`.

`host_for_sni` is sent in the TLS SNI extension. Setting
`tls.verify_subject_name = true` additionally opts into RFC-6125 hostname
verification: the server certificate's SAN/CN is matched against `host_for_sni`
(not the TCP connect target). Hostname verification is only enforced when
`tls.verify_server` is also true; with an IP-literal target and no matching SAN,
leave `verify_subject_name` at its default (`false`).

See `examples/test_tls_client.cpp` for a complete event-loop-driven client example.

Example binaries live in `lib/everest/io/examples/`:
 - `test_tls_server` — `tls_listener` + `tls_server` event-loop echo demo
 - `test_tls_client` — event-loop-driven `tls_client` demo
