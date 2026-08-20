# everest_io {#mainpage}

libeverest_io provides utilities for socket based communication.

Currently there are clients for
 - UDP
 - SocketCAN
 - MQTT
 - PTY
 - TCP
 - TAP

The clients are single threaded and epoll based. Utilities for file descriptor based event handling are provided and used.

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
