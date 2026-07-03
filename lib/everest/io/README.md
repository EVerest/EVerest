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
a connection down: the destructor unregisters the connection's fds from the
handler, so dropping a still-registered endpoint is safe and leaves no stale
handler entries. Calling `unregister_event_handler(conn.get())` before the drop
is the tidy, explicit path. The `fd_event_handler` **must outlive** every
endpoint registered on it — the endpoint's destructor unregisters from it.

See `examples/test_tls_server.cpp` for a complete listener + echo example.

### Client side

`tls_client` is a `tls_endpoint_base<tls_client_socket>`-derived register
interface. Register it with `register_event_handler(&client)`; the TLS handshake
is driven internally on the loop by the class — no handshake hooks or
`desired_events` wiring in user code. Tear it down with
`unregister_event_handler(&client)`, or simply destroy it: the destructor
unregisters from the handler. As with the server side, the `fd_event_handler`
must outlive the client.

A DNS `host_for_sni` is sent in the TLS SNI extension; an IP literal is not (RFC
6066). Setting `tls.verify_subject_name = true` additionally pins the peer
certificate to `host_for_sni` — a DNS name via RFC-6125 SAN/CN matching, an IP
literal via IP-SAN matching — not to the TCP connect target. Pinning is only
enforced when `tls.verify_server` is also true; with an IP-literal target and no
matching SAN, leave `verify_subject_name` at its default (`false`).

See `examples/test_tls_client.cpp` for a complete event-loop-driven client example.

### Threading and signals

`tx()` and its queue are loop-thread-oriented: `tx()` enqueues a payload and wakes
the loop through an internal `event_fd`. The queue itself is not synchronized, so
call `tx()` only from the loop thread (e.g. from the rx handler or the on-ready
action), never from another thread.

OpenSSL drives its socket BIO through `write()` without `MSG_NOSIGNAL`, so a write
to a peer-reset connection raises `SIGPIPE`. Processes using this layer must
install `signal(SIGPIPE, SIG_IGN)` (the examples and the test main do); otherwise
a peer reset during `tx()` aborts the process.

Example binaries live in `lib/everest/io/examples/`:
 - `test_tls_server` — `tls_listener` + `tls_server` event-loop echo demo
 - `test_tls_client` — event-loop-driven `tls_client` demo
