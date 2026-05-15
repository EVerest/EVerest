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

```cpp
using namespace everest::lib::io;

tls::tls_listener::Config lcfg;
auto& chain = lcfg.tls.chains.emplace_back();
chain.certificate_chain_file = "server_chain.pem";
chain.private_key_file       = "server_priv.pem";
chain.trust_anchor_file      = "server_root_cert.pem";
lcfg.bind_addr = "127.0.0.1";
lcfg.bind_port = 8443;

event::fd_event_handler ev;
tls::tls_listener listener(std::move(lcfg));   // socket + bind + listen + tls::Server::init

std::deque<std::shared_ptr<tls::tls_server_connection>> connections;

listener.set_accept_callback(
    [&](std::unique_ptr<tls::tls_server_connection> conn, std::string ip, uint16_t port) {
        std::shared_ptr<tls::tls_server_connection> sp(std::move(conn));
        sp->set_rx_handler([](auto const& payload, auto& self) { self.tx(payload); }); // echo
        ev.register_event_handler(sp.get());
        connections.push_back(std::move(sp));
    });

ev.register_event_handler(&listener);
std::atomic_bool running{true};
ev.run(running);
```

### Client side (event-loop driven)

```cpp
tls::tls_client_socket::Config cfg;
cfg.tls.verify_locations_file = "server_root_cert.pem";
cfg.tls.io_timeout_ms = 5000;
cfg.host_for_sni = "localhost";

tls::tls_client client(cfg, "127.0.0.1", std::uint16_t{8443}, 5000);
client.set_rx_handler([](auto const& payload, auto& dev) { /* ... */ });
client.set_on_ready_action([&client]() { client.tx({/* hello */}); });

event::fd_event_handler ev;
ev.register_event_handler(&client);
std::atomic_bool running{true};
ev.run(running);
```

### Client side (synchronous)

```cpp
tls::tls_client_socket sock;
tls::tls_client_socket::Config cfg;
cfg.tls.verify_locations_file = "server_root_cert.pem";
cfg.host_for_sni = "localhost";
sock.open(std::move(cfg), "127.0.0.1", 8443); // TCP connect + TLS handshake

tls::tls_client_socket::PayloadT msg = {'h', 'i'};
sock.tx(msg);
```

Example binaries live in `lib/everest/io/examples/`:
 - `test_tls_server` — listener + connection event-loop echo demo
 - `test_tls_client` — event-loop-driven `tls_client` demo
