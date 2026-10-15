# spyclient-api

MQTT spy client for the EVerest API. It subscribes to the EVerest API topic
tree (default prefix `everest_api`) and logs all traffic. It is used by the
EVerest API command line client (`api_client.py`), where it registers itself
as the `SpyClient` subcommand of `client_add`.

See the EVerest documentation on the EVerest API for details:
https://everest.github.io/
