#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Shared RequestReply RPC helper for the manager's management APIs.

Requests use the generic envelope {"headers": {"replyTo": <topic>}, "payload": <object>}
(see lib/everest/everest_api_types/src/everest_api_types/generic/json_codec.cpp); the manager
publishes the bare result object to the replyTo topic.
"""

import json
import logging
import threading
import uuid
from typing import List

import paho.mqtt.client as mqtt

_SUBACK_EVENTS_ATTR = "_mqtt_rpc_suback_events"
_SUBACK_LOCK_ATTR = "_mqtt_rpc_suback_lock"


def _ensure_suback_dispatcher(mqtt_client: mqtt.Client) -> None:
    """Install a single on_subscribe dispatcher on the client that resolves
    a per-mid Event, so multiple independent callers (e.g. the lifecycle and
    configuration API clients sharing one connected_mqtt_client fixture) can
    each wait for their own SUBACK without clobbering each other's callback.

    Idempotent: safe to call repeatedly on the same client.
    """
    if getattr(mqtt_client, _SUBACK_EVENTS_ATTR, None) is not None:
        return
    setattr(mqtt_client, _SUBACK_EVENTS_ATTR, {})
    setattr(mqtt_client, _SUBACK_LOCK_ATTR, threading.Lock())
    previous_on_subscribe = mqtt_client.on_subscribe

    def _on_subscribe(client, userdata, mid, granted_qos, *args):
        if previous_on_subscribe is not None:
            previous_on_subscribe(client, userdata, mid, granted_qos, *args)
        lock = getattr(client, _SUBACK_LOCK_ATTR)
        with lock:
            event = getattr(client, _SUBACK_EVENTS_ATTR).get(mid)
        if event is not None:
            event.set()

    mqtt_client.on_subscribe = _on_subscribe


def subscribe_and_wait(mqtt_client: mqtt.Client, topic, timeout_s: float = 5.0, **kwargs) -> None:
    """Subscribe and block until the broker's SUBACK for this subscription is received.

    `topic` is whatever paho's Client.subscribe() accepts as its first argument
    (a topic string, a (topic, qos) tuple, or a list of (topic, qos) tuples).
    Fails fast with a clear message if the subscribe request itself is rejected
    locally, or if no SUBACK arrives within `timeout_s`.
    """
    _ensure_suback_dispatcher(mqtt_client)
    lock = getattr(mqtt_client, _SUBACK_LOCK_ATTR)
    events = getattr(mqtt_client, _SUBACK_EVENTS_ATTR)

    # subscribe() puts the packet in flight before it returns the mid, so registering the
    # mid afterwards races the SUBACK: if the network thread wins, _on_subscribe finds no
    # event and the wait below times out on a subscription the broker did acknowledge.
    # Under the lock the dispatcher blocks until the mid is registered; subscribe() never
    # invokes on_subscribe synchronously, so this cannot deadlock.
    event = threading.Event()
    with lock:
        result, mid = mqtt_client.subscribe(topic, **kwargs)
        if result != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"Failed to subscribe to '{topic}': paho error code {result}")
        events[mid] = event
    try:
        if not event.wait(timeout_s):
            raise TimeoutError(f"Timeout waiting for SUBACK for '{topic}' (mid={mid})")
    finally:
        with lock:
            events.pop(mid, None)


def perform_rpc(mqtt_client: mqtt.Client, command_topic: str, payload: dict,
                timeout_s: float = 10.0) -> dict:
    reply_topic = f"everest_management_api_tests/{uuid.uuid4().hex}/reply"
    reply_event = threading.Event()
    replies: List[dict] = []
    decode_errors: List[str] = []

    def on_reply(_client, _userdata, msg):
        # runs on paho's network thread: an uncaught exception here would not
        # surface as a test failure, it would just leave reply_event unset
        # and the caller waiting until the (misleading) timeout below.
        try:
            reply = json.loads(msg.payload)
        except (ValueError, TypeError) as exc:
            error = f"failed to decode reply payload on {reply_topic}: {exc}; raw={msg.payload!r}"
            logging.error(error)
            decode_errors.append(error)
            return
        replies.append(reply)
        reply_event.set()

    mqtt_client.message_callback_add(reply_topic, on_reply)
    try:
        subscribe_and_wait(mqtt_client, reply_topic, timeout_s=timeout_s, qos=2)
        request = {"headers": {"replyTo": reply_topic}, "payload": payload}
        publish_info = mqtt_client.publish(command_topic, json.dumps(request), qos=2)
        if publish_info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(
                f"Failed to publish request to '{command_topic}': paho error code {publish_info.rc}")
        if not reply_event.wait(timeout_s):
            # note: on an internal error the manager publishes no reply at all
            detail = f"; decode errors: {decode_errors}" if decode_errors else ""
            raise TimeoutError(
                f"Timeout waiting for reply to '{command_topic}' on {reply_topic}{detail}")
        return replies[0]
    finally:
        mqtt_client.unsubscribe(reply_topic)
        mqtt_client.message_callback_remove(reply_topic)
