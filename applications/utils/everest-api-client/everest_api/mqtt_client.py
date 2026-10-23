# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""MQTT client for one EVerest API module instance, built from an ApiModel."""

import collections
import os
import threading
import time
import traceback
from types import SimpleNamespace

import paho.mqtt.client as mqtt

# Messages queued per receive operation for the blocking receive commands.
RECEIVE_BUFFER_SIZE = 100


def connect_mqtt(warn):
    """Create a connected paho client with its network loop running. Broker and
    port come from the environment, as for the EVerest API modules."""
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    host = (os.getenv('MQTT_BROKER') or
            os.getenv('MQTT_SERVER_ADDRESS') or
            os.getenv('MQTT_HOST') or
            'localhost')
    try:
        port = int(os.getenv('MQTT_BROKER_PORT') or
                   os.getenv('MQTT_SERVER_PORT') or
                   os.getenv('MQTT_PORT') or
                   1883)
    except ValueError:
        warn("Invalid MQTT port value, using default port 1883")
        port = 1883
    client.connect(host, port)
    client.loop_start()
    return client


def close_mqtt(client):
    # disconnect() before loop_stop(): loop_stop() on a still-connected client
    # blocks ~0.7s waiting for its network loop to wind down
    try:
        client.disconnect()
    except Exception:  # noqa: BLE001 - closing must not fail
        pass
    client.loop_stop()


def is_valid_topic_level(text):
    """True if *text* can be used inside a topic and a subscription filter."""
    return bool(text) and not any(char in text for char in "+#\0")


class ReceiveOperation:
    """The messages of one subscription: each is handed to the callback
    (topic, payload) on arrival and queued for wait_for_message()."""

    def __init__(self, subscription):
        self.subscription = subscription
        self._callback = None
        self._messages = collections.deque(maxlen=RECEIVE_BUFFER_SIZE)
        self._condition = threading.Condition()

    def deliver(self, topic, payload):
        with self._condition:
            self._messages.append((topic, payload))
            self._condition.notify_all()
        callback = self._callback
        if callback is not None:
            callback(topic, payload)

    def register_callback(self, callback):
        self._callback = callback

    def wait_for_message(self, timeout=None, only_new=False):
        """Return (topic, payload) of the oldest queued message, waiting for one
        if the queue is empty; with only_new the queue is dropped first. None on
        timeout."""
        deadline = None if timeout is None else time.monotonic() + timeout
        with self._condition:
            if only_new:
                self._messages.clear()
            while not self._messages:
                remaining = None if deadline is None else deadline - time.monotonic()
                if remaining is not None and remaining <= 0:
                    return None
                # bounded waits keep the main thread responsive to Ctrl-C
                self._condition.wait(0.2 if remaining is None else min(remaining, 0.2))
            return self._messages.popleft()


class SendOperation:

    def __init__(self, client, topic=None):
        self._client = client
        self._topic = topic

    def publish(self, data, topic=None):
        self._client.publish(topic or self._topic, data)


class ApiClient:
    """send.<op_id>.publish() and receive.<op_id> for every operation of the
    API. Nothing is subscribed until start(), so callbacks can be registered
    first and no message (e.g. a retained one) is missed."""

    def __init__(self, connected_mqtt_client, model, module_id, on_error=None):
        self.model = model
        self._client = connected_mqtt_client
        self._mqtt_prefix = model.prefix(module_id)
        self._on_error = on_error

        self.send = SimpleNamespace()
        for op in model.send_ops:
            static_topic = None if op.is_reply or op.params else f'{self._mqtt_prefix}/{op.address}'
            setattr(self.send, op.op_id, SendOperation(self._client, static_topic))

        self.receive = SimpleNamespace()
        self._receivers = []
        for op in model.recv_ops:
            receiver = ReceiveOperation(f'{self._mqtt_prefix}/{op.subscribe_topic}')
            setattr(self.receive, op.op_id, receiver)
            self._receivers.append(receiver)

    def start(self):
        self._client.on_message = self._on_message
        for receiver in self._receivers:
            self._client.subscribe(receiver.subscription)

    def send_topic(self, op, values):
        return f'{self._mqtt_prefix}/{op.send_topic(values)}'

    def _on_message(self, client, userdata, message):
        # an exception escaping a paho callback stops the client's network loop
        try:
            for receiver in self._receivers:
                if mqtt.topic_matches_sub(receiver.subscription, message.topic):
                    receiver.deliver(message.topic, message.payload)
                    return
        except Exception:  # noqa: BLE001
            if self._on_error is not None:
                self._on_error(f"{message.topic}: {traceback.format_exc()}")
