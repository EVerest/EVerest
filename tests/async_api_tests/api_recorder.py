# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Recorder of every MQTT message of one EVerest instance under test.

External topics come back exactly as the API specifications name them
(everest_api/..., everest_external/...), internal ones as
modules/<id>/impl/<impl>/{var,cmd,error}/... with the instance prefixes removed.
"""
import json
import os
import threading
import time
from typing import Optional

import paho.mqtt.client as mqtt
from everest.testing.core_utils.everest_core import EverestCore



class Record:
    __slots__ = ('t', 'topic', 'payload')

    def __init__(self, t: float, topic: str, payload: str):
        self.t, self.topic, self.payload = t, topic, payload

    def json(self):
        try:
            return json.loads(self.payload)
        except ValueError:
            return None

    def value(self):
        """Strip the framework envelope of internal var, cmd and error messages."""
        data = self.json()
        if isinstance(data, dict) and isinstance(data.get('data'), dict):
            inner = data['data']
            if 'data' in inner:
                return inner['data']
            if 'args' in inner:
                return inner['args']
            return inner
        return data

    def __repr__(self):
        return f'{self.t:8.3f} {self.topic} {self.payload[:120]}'


class Recorder:
    """Records every message of one EVerest instance, with the instance prefixes removed.

    External topics come back exactly as the API spec names them (everest_api/...,
    everest_external/...), internal ones as modules/<id>/impl/<impl>/{var,cmd,error}/...
    """

    def __init__(self, everest_core: EverestCore):
        self.internal_prefix = f'everest_{everest_core.everest_uuid}/'
        self.external_prefix = everest_core.mqtt_external_prefix
        self.records = []
        self.listeners = []
        self.cv = threading.Condition()
        self.t0 = time.monotonic()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f'lab-{everest_core.everest_uuid}')
        self.client.on_message = self._on_message
        self.client.on_connect = self._on_connect
        self._subscribed = threading.Event()

    def start(self):
        self.client.connect(os.environ.get('MQTT_SERVER_ADDRESS', '127.0.0.1'),
                            int(os.environ.get('MQTT_SERVER_PORT', '1883')))
        self.client.loop_start()
        assert self._subscribed.wait(5.0), 'recorder did not subscribe within 5 s'
        return self

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()

    def now(self) -> float:
        return time.monotonic() - self.t0

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        # the external prefix has no trailing slash, so '#' alone is not a valid filter after it
        client.subscribe([(self.internal_prefix + '#', 0)] +
                         [(self.external_prefix + root + '/#', 0) for root in ('everest_api', 'everest_external', 'test')])
        self._subscribed.set()

    def _on_message(self, client, userdata, msg):
        topic = msg.topic
        if topic.startswith(self.internal_prefix):
            topic = topic[len(self.internal_prefix):]
        elif topic.startswith(self.external_prefix):
            topic = topic[len(self.external_prefix):]
        record = Record(self.now(), topic, msg.payload.decode('utf-8', 'replace'))
        with self.cv:
            self.records.append(record)
            self.cv.notify_all()
        for listener in list(self.listeners):
            listener(record)

    def publish(self, topic: str, payload):
        """Publish on an external topic; non-string payloads are JSON encoded."""
        if not isinstance(payload, str):
            payload = json.dumps(payload)
        self.client.publish(self.external_prefix + topic, payload)

    def _matches(self, r: Record, topic, prefix, pred, since):
        return (r.t >= since and (topic is None or r.topic == topic)
                and (prefix is None or r.topic.startswith(prefix)) and (pred is None or pred(r)))

    def find(self, topic=None, since=0.0, pred=None, prefix=None):
        with self.cv:
            return [r for r in self.records if self._matches(r, topic, prefix, pred, since)]

    def wait_for(self, topic=None, timeout=10.0, since=None, pred=None, prefix=None) -> Optional[Record]:
        """First matching record at or after `since` (default: now), or None after `timeout`."""
        if since is None:
            since = self.now()
        deadline = time.monotonic() + timeout
        with self.cv:
            while True:
                for r in self.records:
                    if self._matches(r, topic, prefix, pred, since):
                        return r
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.cv.wait(remaining)

    def absent(self, topic=None, timeout=3.0, since=None, pred=None, prefix=None) -> bool:
        return self.wait_for(topic, timeout, since, pred, prefix) is None
