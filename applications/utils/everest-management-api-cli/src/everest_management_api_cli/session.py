# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""One MQTT connection shared by the lifecycle and configuration API clients."""

import uuid
from typing import Optional

from .configuration_api import ConfigurationApiClient, NoticeListener
from .lifecycle_api import LifecycleApiClient, make_mqtt_client


class Session:
    """Connects to the broker and subscribes to every topic the management APIs publish on.

    Notifications (lifecycle status, active_slot, config_updates) are forwarded to `on_notice`,
    which may be swapped at any time; they are also recorded by the two clients. Usable as a
    context manager.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 1883, timeout_s: float = 10.0,
                 client_id: Optional[str] = None, verbose: bool = False):
        self.host = host
        self.port = port
        self.timeout_s = timeout_s
        self.client_id = client_id or f"everest_management_api_cli_{uuid.uuid4().hex[:8]}"
        self.verbose = verbose
        self.on_notice: Optional[NoticeListener] = None
        self.mqtt = None
        self.lifecycle: Optional[LifecycleApiClient] = None
        self.configuration: Optional[ConfigurationApiClient] = None

    def _dispatch(self, topic: str, payload: dict) -> None:
        listener = self.on_notice
        if listener is not None:
            listener(topic, payload)

    def connect(self) -> "Session":
        self.mqtt = make_mqtt_client(self.client_id)
        if self.verbose:
            self.mqtt.enable_logger()
        self.mqtt.connect(self.host, self.port)
        self.mqtt.loop_start()
        try:
            self.lifecycle = LifecycleApiClient(self.mqtt, self._dispatch)
            self.configuration = ConfigurationApiClient(self.mqtt, self._dispatch)
            self.lifecycle.subscribe_status()
            self.configuration.subscribe_notices()
        except Exception:
            self.close()
            raise
        return self

    def close(self) -> None:
        if self.mqtt is not None:
            self.mqtt.loop_stop()
            self.mqtt.disconnect()
            self.mqtt = None

    def __enter__(self) -> "Session":
        return self.connect()

    def __exit__(self, *_exc) -> None:
        self.close()
