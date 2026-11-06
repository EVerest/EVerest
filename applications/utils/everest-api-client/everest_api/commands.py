# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""cmd2 commands built from the ApiModels.

  - `client_add <api> <module_id>` for every API (one CommandSet for all APIs)
  - per added client, a CommandSet registered at runtime with one command per operation:
      `<module_id>.<op_id> [replyTo] [<channel params>...] <payload>` per send operation
      `<module_id>.<op_id> [--next] [--timeout S] [--optional]` per receive operation,
      returning the oldest message no receive has returned yet, waiting for one if needed
"""

import argparse
import json
import re
import textwrap

import cmd2
from cmd2 import CommandSet, with_default_category
from cmd2.utils import strip_quotes

from everest_api.model import compact_json
from everest_api.mqtt_client import ApiClient, close_mqtt, connect_mqtt

COMMAND_SEPARATOR = "."


class ClientCommandSet(CommandSet):
    """Base of the per-client command sets. The argument completers live here so
    cmd2 resolves them to the command set instance of the command."""

    module_id = None
    api_client = None
    send_ops = {}

    def complete_reply_to(self, text, line, begidx, endidx):
        return self._cmd.complete_reply_to(text, self.module_id)

    def complete_payload(self, text, line, begidx, endidx):
        op = self.send_ops.get(line.split(maxsplit=1)[0][len(self.module_id) + len(COMMAND_SEPARATOR):])
        payload = example_payload(op, self._cmd.replyToPlaceholder) if op is not None else ""
        return [payload] if payload and payload.startswith(text) else []

    def on_message(self, topic, data):
        text = data.decode('utf-8', errors='replace') if data else '""'
        try:
            text = json.dumps(json.loads(text), indent=1 if self._cmd.prettify else None)
        except ValueError:
            pass
        self._cmd.log_event(topic, text)

    def register_callbacks(self):
        for receiver in vars(self.api_client.receive).values():
            receiver.register_callback(self.on_message)

    def blocking_receive(self, op_id, args):
        receiver = getattr(self.api_client.receive, op_id)
        message = receiver.wait_for_message(args.timeout, only_new=args.next)
        if message is None:
            self._cmd.timed_out(f"{command_name(self.module_id, op_id)} timed out after {args.timeout}s",
                                args.optional)
            return
        topic, data = message
        text = data.decode('utf-8', errors='replace')
        # @last refers to the request just received, even if newer ones are queued
        self._cmd.cache_reply_to(text)
        if not self._cmd.mute:
            self._cmd.poutput(f"received: {topic}")

    def send(self, op, args, arguments_text):
        """Publish *op*. *arguments_text* is the text after the command word; the
        payload is what follows the replyTo and topic parameter arguments."""
        payload_text = text_after_words(arguments_text, int(op.is_reply) + len(op.params))
        if not payload_text:
            self._cmd.perror("the payload is missing")
            return
        try:
            payload = json.dumps(parse_payload(payload_text))
        except ValueError as err:
            self._cmd.perror(f"invalid JSON payload: {err}")
            return
        send = getattr(self.api_client.send, op.op_id)
        if op.is_reply:
            send.publish(payload, topic=strip_quotes(args.replyTo))
            return
        topic = send._topic
        if op.params:
            topic = self.api_client.send_topic(op, {param: strip_quotes(getattr(args, param)) for param in op.params})
        if op.reply_op_id is not None:
            payload = self._cmd.generate_replyTo(payload, topic)
        send.publish(payload, topic=topic)


def text_after_words(text, count):
    """*text* without its first *count* whitespace-separated words."""
    return re.sub(r"^\s*(?:\S+\s+){%d}" % count, "", text, count=1).strip() if count else text.strip()


def parse_payload(text):
    """Parse a JSON payload. Quotes are kept on the command line, so a payload
    quoted as a whole ('{...}') is unquoted once if it does not parse as is."""
    try:
        return json.loads(text)
    except ValueError:
        if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
            return json.loads(text[1:-1])
        raise


def example_payload(op, reply_to_placeholder):
    """The spec's example payload of *op*; for a request, with the placeholder
    as its replyTo so that sending it as is gets a fresh reply address."""
    if not op.payload_json or op.reply_op_id is None:
        return op.payload_json
    payload = json.loads(op.payload_json)
    headers = payload.get("headers") if isinstance(payload, dict) else None
    if not isinstance(headers, dict) or "replyTo" not in headers:
        return op.payload_json
    headers["replyTo"] = reply_to_placeholder
    return compact_json(payload)


def _command(name, parser, func, help_text):
    func.__name__ = func.__qualname__ = f"do_{name}"
    func.__doc__ = help_text or None
    return cmd2.with_argparser(parser, preserve_quotes=True)(func)


def _send_command(op, module_id):
    epilog = None
    if op.reply_op_id is not None:
        epilog = ("This is a request. If headers.replyTo in the payload is the replyToPlaceholder (see 'set'), "
                  "it is replaced by a fresh reply address; TAB on the payload inserts it. The reply is received "
                  f"with '{command_name(module_id, op.reply_op_id)}'.")
    elif op.is_reply:
        epilog = ("This replies to a request received from EVerest: replyTo is the address from the request's "
                  "headers. TAB completes it from the requests received so far, and @last (or @1, @2, ...) "
                  "stands for the most recent ones; 'replyto' lists them.")
    parser = cmd2.Cmd2ArgumentParser(description=op.help or None,
                                     epilog=textwrap.fill(epilog, width=100) if epilog else None)
    if op.is_reply:
        parser.add_argument('replyTo', help='the replyTo address of the request (TAB, @last)',
                            completer=ClientCommandSet.complete_reply_to)
    for param in op.params:
        parser.add_argument(param, help=f'value of the {param} topic parameter')
    parser.add_argument('payload', nargs=argparse.REMAINDER,
                        help='the payload as JSON, the rest of the line (TAB inserts the example from the spec)',
                        completer=ClientCommandSet.complete_payload)

    def do_send(self, args):
        self.send(op, args, self._cmd.line_after_command(args.cmd2_statement.get()))

    return parser, do_send


def _receive_command(op):
    parser = cmd2.Cmd2ArgumentParser(description=op.help or None)
    parser.add_argument('--next', action='store_true',
                        help='ignore the messages received so far and wait for a new one')
    parser.add_argument('--timeout', type=float, default=None,
                        help='seconds to wait before giving up (default: wait until a message arrives)')
    parser.add_argument('--optional', action='store_true',
                        help='on timeout, print a note and continue instead of failing')

    def do_receive(self, args):
        self.blocking_receive(op.op_id, args)

    return parser, do_receive


def command_name(module_id, op_id):
    return f"{module_id}{COMMAND_SEPARATOR}{op_id}"


def make_client_command_set(model, module_id, api_client):
    """Create the CommandSet instance with the commands of one added client."""
    attrs = {"module_id": module_id, "api_client": api_client, "send_ops": {op.op_id: op for op in model.send_ops}}

    for op in model.send_ops:
        parser, func = _send_command(op, module_id)
        attrs[f"do_{command_name(module_id, op.op_id)}"] = _command(
            command_name(module_id, op.op_id), parser, func, op.help)

    for op in model.recv_ops:
        parser, func = _receive_command(op)
        attrs[f"do_{command_name(module_id, op.op_id)}"] = _command(
            command_name(module_id, op.op_id), parser, func, op.help)

    cls = type(f"ClientCommandSet[{module_id}]", (ClientCommandSet,), attrs)
    return with_default_category(f"{module_id} ({model.api})")(cls)()


def _client_add_subcommand(model):
    parser = cmd2.Cmd2ArgumentParser()
    if model.needs_module_id:
        parser.add_argument('module_id', help='the module id for the client')
    else:
        parser.add_argument('module_id', nargs='?', default=model.api,
                            help=f'the id for the client (default: {model.api})')

    def client_add(self, ns: argparse.Namespace):
        self.add_client(model, ns.module_id)

    return cmd2.as_subcommand_to('client_add', model.api, parser, help=model.pathname)(client_add)


class ClientAddCommandSet(CommandSet):
    """Holds the `client_add <api>` subcommands of all APIs."""

    def add_client(self, model, module_id):
        if not self._cmd.check_new_client_id(module_id, needs_command_name=True):
            return
        mqtt_client = connect_mqtt(lambda msg: self._cmd.log_event("WARNING:", msg))
        registered = False
        try:
            api_client = ApiClient(mqtt_client, model, module_id,
                                   on_error=lambda msg: self._cmd.log_event("ERROR:", msg))
            command_set = make_client_command_set(model, module_id, api_client)
            self._cmd.register_command_set(command_set)
            registered = True
            command_set.register_callbacks()
            api_client.start()
        except Exception:
            if registered:
                self._cmd.unregister_command_set(command_set)
            close_mqtt(mqtt_client)
            raise
        self._cmd.mqtt_clients.append({"id": module_id, "mqtt_client": mqtt_client})
        self._cmd.api_clients.append({"id": module_id, "instance": api_client, "command_set": command_set})


def make_client_add_command_set(models):
    attrs = {f"client_add_{model.api}": _client_add_subcommand(model) for model in models}
    return type("ClientAddCommandSet", (ClientAddCommandSet,), attrs)()
