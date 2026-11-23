# Modbus Server / Client library

## Build and test

This library is built and tested as part of the build process of EVerest.

## Run examples

### Modbus basic server

Note: you need an modbus tcp client for this example

```bash
cd build/lib/everest/modbus
./examples/dummy_basic_server
```

### SSL Modbus server + client

#### Setup SSL certificates

```bash
cd certs
./generate.sh
```

#### Run server

```bash
cd build
./examples/dummy_basic_ssl_server
```

#### Run client

```bash
cd build
./examples/dummy_basic_ssl_client
```
