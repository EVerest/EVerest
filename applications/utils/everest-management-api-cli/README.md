# everest-management-api-cli

Command line client for the EVerest manager's **lifecycle API** and
**configuration API**: start and stop modules, inspect and edit configuration
slots and parameters, watch status updates and configuration notices, or send
hand-crafted requests to probe error handling. The interactive shell (`shell`)
keeps one connection open and prints status updates and notices as they arrive;
every command also works as a one-shot invocation for scripts.

Documentation:

- `docs/source/explanation/dev-tools/management-api-cli.rst`: installation,
  command reference, parameter files, exit codes, shell.
- `docs/source/tutorials/management_apis.rst`: walkthrough of both APIs with
  this client, including the manager flags needed to enable them.
- `docs/source/explanation/adapt-everest/management_apis.rst`: the API
  concepts, topics and message format.

## Installation

An EVerest build with testing enabled (`-DBUILD_TESTING=ON`) installs the
client into the build's Python venv, next to `everest-testing`; activate it with
`source build/venv/bin/activate` and the `everest-management-api-cli` command is
available. For any other environment:

```sh
pip install -e applications/utils/everest-management-api-cli
```

Dependencies are `paho-mqtt` (1.x or 2.x) and `pyyaml`.

## Client modules

The package also provides the API clients used by the integration tests in
`tests/management_api_tests` (`lifecycle_api.py`, `configuration_api.py`,
`mqtt_rpc.py`), so it has to be installed wherever those tests run. The CMake
configure step installs it into the build venv, CI installs its wheel into the
integration test image.

## Development

```sh
pip install -e 'applications/utils/everest-management-api-cli[test]'
pytest applications/utils/everest-management-api-cli/tests
```

The tests need no broker: they check argument parsing, request payloads and
the shell's error handling against a stubbed transport.
