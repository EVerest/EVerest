from enum import Enum


class Requirement:
    def __init__(self, module_id: str, implementation_id: str):
        self.module_id = module_id
        self.implementation_id = implementation_id


class OCPPVersion(str, Enum):
    ocpp16 = "ocpp1.6"
    ocpp201 = "ocpp2.0.1"
    ocpp21 = "ocpp2.1"


def close_mqtt_client(client) -> None:
    """Disconnect a paho client and release its sockets immediately.

    paho closes the socket pair behind loop_start() only in __del__, and the
    harness's callbacks keep each client in a reference cycle, so without this
    the descriptors stay open until a full garbage collection.
    """
    # disconnect() first: the network loop has to be running to flush the
    # DISCONNECT, or the broker sees an ungraceful drop.
    try:
        client.disconnect()
    finally:
        client.loop_stop()
        client._reset_sockets()
