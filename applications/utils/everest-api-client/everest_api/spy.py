# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Spy client: subscribes to a whole topic tree and logs all traffic."""

import argparse
import traceback

import cmd2
from cmd2 import CommandSet, with_default_category

from everest_api.mqtt_client import close_mqtt, connect_mqtt, is_valid_topic_level

DEFAULT_SPY_PREFIX = 'everest_api'
SPY_CLIENT_ID = 'spy'


class SpyClient:

    def __init__(self, connected_mqtt_client, prefix, on_error=None):
        self._client = connected_mqtt_client
        self._mqtt_prefix = prefix or DEFAULT_SPY_PREFIX
        self._callback = None
        self._on_error = on_error

    def register_callback(self, callback):
        self._callback = callback

    def start(self):
        self._client.on_message = self._on_message
        self._client.subscribe(f'{self._mqtt_prefix}/#')

    def _on_message(self, client, userdata, message):
        # an exception escaping a paho callback stops the client's network loop
        try:
            if self._callback is not None:
                self._callback(message.topic, message.payload)
        except Exception:  # noqa: BLE001
            if self._on_error is not None:
                self._on_error(f"{message.topic}: {traceback.format_exc()}")


@with_default_category('spy')
class SpyCommandSet(CommandSet):

    def on_receive(self, topic, data):
        self._cmd.log_event(topic, data.decode('utf-8', errors='replace'))

    client_add_parser = cmd2.Cmd2ArgumentParser()
    client_add_parser.add_argument('mqtt_topic', nargs='?',
                                   help='the path/topic for the client to spy on, if empty will spy on the EVerest API; '
                                        'also the id for client_remove (default: spy)')

    @cmd2.as_subcommand_to('client_add', 'spy', client_add_parser,
                           help=f'spy on all messages below a topic (default: {DEFAULT_SPY_PREFIX})')
    def client_add_spy(self, ns: argparse.Namespace):
        client_id = ns.mqtt_topic or SPY_CLIENT_ID
        if ns.mqtt_topic is not None and not is_valid_topic_level(ns.mqtt_topic.strip("/")):
            self._cmd.perror(f"'{ns.mqtt_topic}' cannot be spied on: give a topic without the wildcards + and #")
            return
        if not self._cmd.check_new_client_id(client_id, needs_command_name=False):
            return
        mqtt_client = connect_mqtt(lambda msg: self._cmd.log_event("WARNING:", msg))
        try:
            spy_client = SpyClient(mqtt_client, (ns.mqtt_topic or "").rstrip("/"),
                                   on_error=lambda msg: self._cmd.log_event("ERROR:", msg))
            spy_client.register_callback(self.on_receive)
            spy_client.start()
        except Exception:
            close_mqtt(mqtt_client)
            raise
        self._cmd.mqtt_clients.append({"id": client_id, "mqtt_client": mqtt_client})
        self._cmd.api_clients.append({"id": client_id, "instance": spy_client})
