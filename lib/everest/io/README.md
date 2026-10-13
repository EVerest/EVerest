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

The clients are driven from a single event loop and are epoll based; the async connect path uses a short-lived detached thread for the blocking connect. Utilities for file descriptor based event handling are provided and used.

## Client tx contract

`fd_event_client::tx()` returns a `bool` and every call site should check it. It rejects once the
connection has failed, and once `max_buffered_tx_payloads` are already queued, which a peer that
stops reading reaches on its own. A rejection on a live connection is backpressure, not a failure:
the connection stays up and everything already accepted is still delivered in order.

Payloads written before the connection is up are **discarded by default**. Holding one across a
connect replays it onto a connection it was not written for, which delivers a stale value for the
periodic setpoints and stateful line protocols most consumers carry.

A consumer whose payload survives the delay asks for the buffer, per policy or per instance:

```cpp
// Per policy: every client built on it buffers unless told otherwise.
struct my_socket {
    static constexpr bool buffer_tx_before_connect{true};
    // ...
};

// Per instance, overriding the policy default in either direction.
tcp::tcp_client client(utilities::tx_buffering::buffer, host, port, timeout_ms);
```

Only a policy that connects asynchronously has a pre-connect window. Asking a synchronous policy to
buffer is accepted and does nothing.

`reset()` clears the buffer in both modes, so a `true` from `tx()` after a reset always means
buffered for the connection the reset opens. `reset()` also moves the connection state at call
time: `on_error()` reports `true` from the call until the reopen.

`tx()` and `reset()` are loop-thread only: call them from the thread that drives `sync()`, or from
a callback that thread dispatches.

## Flow control between clients

A consumer forwarding from one client into another (pty into TCP) can throttle the source instead
of dropping when the sink's buffer is full:

- `pause_rx()` / `resume_rx()` / `rx_paused()`: stop and restart monitoring the source for
  readability; the kernel buffer fills and the writer blocks. A paused stream socket still reports
  the peer closing through the error handler. Rejected without a monitored connection (before the
  first `sync()`, during a reopen or a handshake), idempotent, and a reconnect starts unpaused:
  re-apply from `set_on_ready_action`.
- `tx_queue_depth()` / `set_tx_drained_action()`: pause the source past a watermark, resume it from
  the drained action. `reset()` and a failed connection clear the buffer without draining; resume
  the source from the sink's `set_on_ready_action`, not from the error handler: without pre-connect
  buffering `tx()` rejects until the reopen completes.
- `tx_coalescing(payload, max_payload_size)`: append to the newest waiting payload instead of
  queueing another. Exists only for a policy declaring `supports_tx_coalescing{true}`: a byte stream
  whose `tx()` leaves exactly the unsent bytes in the payload (`tcp_socket`, `pty_handler`). Frame
  transports and TLS do not compile with it.

All share the loop-thread contract of `tx()`.

## PTY

`pty_handler` opens the master non blocking. A write the slave has not drained returns early,
`tx()` keeps the unsent bytes and the client retries; a blocking master would stall the whole loop.
Undrained data queues up to `max_buffered_tx_payloads`, then `tx()` rejects. A feeder that must not
lose data throttles its source with the flow control above, otherwise it checks the return value of
`tx()`.

## TLS

Drives the libtls (`everest::tls`) `Server` and `Client` through the same
`fd_event_handler` pattern as the other clients. Disable with
`-DEVEREST_IO_ENABLE_TLS=OFF` when libtls / OpenSSL are unavailable.

### Server side

`tls_listener` owns the listen socket and an embedded `tls::Server`. For each
accepted connection it yields a `std::unique_ptr<tls::tls_server>` to the accept
callback. Register it on the same `fd_event_handler` with
`register_event_handler(conn.get())`, which makes the loop drive the handshake and
rx/tx, and keep it alive for the connection's lifetime.

Dropping the `unique_ptr` tears the connection down and unregisters its fds.

`tls_server::tx()` buffers and returns a `bool`. Check it: a rejection is
backpressure, not a failure, but ignoring it turns a stalled peer into silently
dropped payloads.

`tls_listener::set_error_handler` is the only way to observe a failed accept, and
is never called with code `0`. Descriptor exhaustion is reported as `EMFILE` or
`ENFILE` and costs the queued connection; serving resumes by itself once
descriptors are available.

### Client side

`tls_client` is an alias for `event::fd_event_client<tls_client_socket>::type`,
not a class. `tls_client_interface` names the second parameter of the rx callback
without naming the concrete type.

The client is an `fd_event_sync_interface` nesting its own `fd_event_handler`:
`register_event_handler(&client)` resolves to the sync-interface overload, and the
outer handler polls `get_poll_fd()` and calls `sync()`. Only the blocking TCP
connect runs off-loop, on the generic client's short-lived detached thread; the TLS
handshake is driven loop-side through the handshake trait of `tls_client_socket`, so
user code wires no handshake hooks or `desired_events`.

`unregister_event_handler(&client)` is the explicit way to leave the handler, but
the destructor drops the registration anyway.

What is required is the ordering. The `fd_event_handler` **must outlive** every
endpoint registered on it, client and server alike, because the endpoint's
destructor reaches into the handler to unregister. Declare the handler first, so it
is destroyed last.

`tx()` accepts payloads from the start: those queued before the connect completes
and during the handshake are held and flushed in order once the connection is up
(`tls_client_socket` declares `buffer_tx_before_connect`, see "Client tx contract"
above). The on-ready action signals there is a connection to send on.

A connect or handshake that keeps failing goes inert: the error handler fires and
the client tears down, but it does not reconnect on its own, only a consumer
`reset()` reopens it. The error callback also reports a cleared error as code `0`,
so gate consumer logic on `err != 0`.

A DNS `host_for_sni` is sent in the TLS SNI extension, an IP literal is not (RFC
6066). `tls.verify_subject_name = true` additionally pins the peer certificate to
`host_for_sni` (a DNS name via RFC-6125 SAN/CN matching, an IP literal via IP-SAN
matching), not to the TCP connect target. Pinning is only enforced when
`tls.verify_server` is also true; with an IP-literal target and no matching SAN,
leave `verify_subject_name` at its default `false`.

### Threading and signals

`tx()` wakes the loop through an internal `event_fd`. Its queue is not
synchronized, so call `tx()` only from the loop thread (the rx handler or the
on-ready action), never from another thread.

OpenSSL drives its socket BIO through `write()` without `MSG_NOSIGNAL`, so a write
to a peer-reset connection raises `SIGPIPE`. Processes using this layer must
install `signal(SIGPIPE, SIG_IGN)` or a peer reset during `tx()` aborts the
process.

Example binaries in `lib/everest/io/examples/`:
 - `test_tls_server`: `tls_listener` + `tls_server` echo demo
 - `test_tls_client`: event-loop-driven `tls_client` demo
