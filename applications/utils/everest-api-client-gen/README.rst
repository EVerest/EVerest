everest-api-client-gen
======================

Generates the per-API Python MQTT client packages for the EVerest API client
(``applications/utils/everest-api-client``) from the AsyncAPI specs in
``docs/source/reference/EVerest_API``.

This is a small, self-contained replacement for the npm ``asyncapi-cli`` +
``python-mqtt-template`` toolchain. It uses only Jinja2 and PyYAML (already
build dependencies via ``ev-cli``), so it adds no npm supply-chain surface.

Usage
-----

.. code-block:: sh

    everest-api-client-gen \
        --spec docs/source/reference/EVerest_API/powermeter_API.yaml \
        --out build/generated/async_python_client_powermeter_api \
        --name-camel-case PowermeterAPI \
        --name-snake-case powermeter_api \
        --interface-prefix pm \
        --server default

The generator reads only the small AsyncAPI subset the clients need (server
pathname, operation actions, channel addresses, reply cross-references and one
message example per operation) and fails loudly on anything unexpected.
