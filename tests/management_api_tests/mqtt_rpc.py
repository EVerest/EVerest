#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Shared RequestReply RPC helper for the manager's management APIs.

Requests use the generic envelope {"headers": {"replyTo": <topic>}, "payload": <object>}
(see lib/everest/everest_api_types/src/everest_api_types/generic/json_codec.cpp); the manager
publishes the bare result object to the replyTo topic.
"""

import json
import threading
import time
import uuid
from typing import List

import paho.mqtt.client as mqtt


def perform_rpc(mqtt_client: mqtt.Client, command_topic: str, payload: dict,
                timeout_s: float = 10.0) -> dict:
    reply_topic = f"everest_management_api_tests/{uuid.uuid4().hex}/reply"
    reply_event = threading.Event()
    replies: List[dict] = []

    def on_reply(_client, _userdata, msg):
        replies.append(json.loads(msg.payload))
        reply_event.set()

    mqtt_client.message_callback_add(reply_topic, on_reply)
    mqtt_client.subscribe(reply_topic, qos=2)
    # give the broker a moment to process the subscription
    time.sleep(0.2)
    try:
        request = {"headers": {"replyTo": reply_topic}, "payload": payload}
        mqtt_client.publish(command_topic, json.dumps(request), qos=2)
        if not reply_event.wait(timeout_s):
            # note: on an internal error the manager publishes no reply at all
            raise TimeoutError(f"Timeout waiting for reply to '{command_topic}' on {reply_topic}")
        return replies[0]
    finally:
        mqtt_client.unsubscribe(reply_topic)
        mqtt_client.message_callback_remove(reply_topic)
