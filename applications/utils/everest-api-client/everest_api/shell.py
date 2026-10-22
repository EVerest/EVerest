# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""EVerest API command line client application"""
import uuid
import os
import re
import json
import argparse
import cmd2
import collections
import queue
import threading
import time
from cmd2 import rl_utils

from everest_api.commands import command_name, make_client_add_command_set
from everest_api.model import reply_topic
from everest_api.mqtt_client import close_mqtt, is_valid_topic_level
from everest_api.spy import SpyCommandSet


def cut_at_terminator(text):
    """*text* up to the first ';' outside quotes."""
    quote = None
    for index, char in enumerate(text):
        if quote:
            if char == quote:
                quote = None
        elif char in "'\"":
            quote = char
        elif char == ";":
            return text[:index]
    return text


class EVerestAPICmd(cmd2.Cmd):
    """EVerest API command line client application"""

    def __init__(self, models):
        # no output redirection: '>' and '|' are kept as typed, e.g. in payloads
        super().__init__(include_py=True, persistent_history_file=os.path.expanduser("~/.everest_api_client"),
                         auto_load_commands=False, allow_redirection=False)
        self.mqtt_clients = []
        self.api_clients = []
        self.stop_requested = False
        self.show_topic = ""
        self.hide_topic = ""
        self.quiet = True
        self.mute = False
        self.prettify = False
        self.replyToPlaceholder = "everest_api/1/{interface_type}/{module_id}/e2m/{operation_name}/{uuid}"
        self.colors = True

        self.IN_COLOR = '\033[92m'   # Green
        self.OUT_COLOR = '\033[93m'  # Yellow
        self.RESET = '\033[0m'    # Reset to default color

        self.add_settable(cmd2.Settable('show_topic', str, 'Comma separated list. If set, only messages whose topic contains one of the strings are shown (hide_topic still applies to them).', self))
        self.add_settable(cmd2.Settable('hide_topic', str, 'Comma separated list. If set, messages whose topic contains one of the strings are hidden.', self))
        self.add_settable(cmd2.Settable('quiet', bool, 'Hide heartbeat and communication_check messages', self))
        self.add_settable(cmd2.Settable('prettify', bool, 'Prettify the json outputs', self))
        self.add_settable(cmd2.Settable('colors', bool, 'Use colors for the outputs', self))
        self.add_settable(cmd2.Settable('mute', bool, 'Suppress all incoming/outgoing message logging (echo, wait_for and errors still print)', self))
        self.add_settable(cmd2.Settable('replyToPlaceholder', str, 'Text that is replaced by a fresh reply address in the payload of a sent request (see help replyto)', self))

        self.output_queue = queue.Queue()
        # Start a thread to process the queue and handle output
        self.queue_processing_thread = threading.Thread(target=self.process_output_queue)
        self.queue_processing_thread.daemon = True
        self.queue_processing_thread.start()

        # Predicates registered by an in-progress `wait_for`; each incoming
        # message is offered to them so a script can block until a specific
        # topic/value appears instead of relying on send ordering.
        self.wait_waiters = []
        self.wait_waiters_lock = threading.Lock()

        # replyTo addresses seen in incoming requests, most recent first.
        # Reply commands complete their replyTo argument from this cache, and
        # @last / @<n> tokens are substituted from it (see do_replyto).
        self.reply_to_cache = []
        self.register_postparsing_hook(self._substitute_replyto_tokens)

        # recent messages, so that wait_for also sees what arrived while the
        # previous command was running
        self.recent_messages = collections.deque(maxlen=1000)
        self.previous_command_start = self.current_command_start = time.monotonic()
        self.register_precmd_hook(self._track_command_start)
        self.register_postcmd_hook(self._stop_failed_script)
        self.script_failed = False
        self.output_lock = threading.Lock()

        self.models = models
        self.register_command_set(SpyCommandSet())
        self.register_command_set(make_client_add_command_set(models))

    def _track_command_start(self, data: cmd2.plugin.PrecommandData) -> cmd2.plugin.PrecommandData:
        self.previous_command_start, self.current_command_start = self.current_command_start, time.monotonic()
        return data

    def _stop_failed_script(self, data: cmd2.plugin.PostcommandData) -> cmd2.plugin.PostcommandData:
        # a failure in a nested script also stops the scripts that run it
        if self.script_failed:
            if self.in_script():
                raise KeyboardInterrupt("script stopped")
            self.script_failed = False
        return data

    def cache_reply_to(self, payload):
        try:
            reply_to = json.loads(payload).get("headers", {}).get("replyTo")
        except (ValueError, AttributeError):
            return
        if isinstance(reply_to, str) and reply_to:
            if reply_to in self.reply_to_cache:
                self.reply_to_cache.remove(reply_to)
            self.reply_to_cache.insert(0, reply_to)
            del self.reply_to_cache[50:]

    def complete_reply_to(self, text, module_id):
        # the client's own addresses are offered, or all of them if it has none
        candidates = [rt for rt in self.reply_to_cache if f"/{module_id}/" in rt] or self.reply_to_cache
        return [rt for rt in candidates if rt.startswith(text)]

    def check_new_client_id(self, client_id, needs_command_name):
        if any(client["id"] == client_id for client in self.api_clients):
            self.perror(f"A client with id '{client_id}' exists already")
            return False
        if needs_command_name:
            valid, errmsg = self.statement_parser.is_valid_command(command_name(client_id, "x"))
            if valid and (any(char.isspace() for char in client_id) or "/" in client_id
                          or not is_valid_topic_level(client_id)):
                valid, errmsg = False, "whitespace, '/', '+' and '#' are not allowed"
            if not valid:
                self.perror(f"'{client_id}' cannot be used as a client id: {errmsg}")
                return False
        return True

    def timed_out(self, note, optional):
        """Report a timeout of a blocking command. Unless optional, it is an
        error: the exit code becomes 1 and a running script is stopped."""
        if optional:
            self.poutput(note + " (optional, continuing)")
            return
        self.perror(note)
        self.exit_code = 1
        if self.in_script():
            self.script_failed = True
            # cmd2 stops a script, but not the shell, on KeyboardInterrupt
            raise KeyboardInterrupt("script stopped")

    def _substitute_replyto_tokens(self, data: cmd2.plugin.PostparsingData) -> cmd2.plugin.PostparsingData:
        raw = data.statement.raw

        def repl(match):
            index = 0 if match.group(1) in (None, 'last') else int(match.group(1)) - 1
            if 0 <= index < len(self.reply_to_cache):
                return self.reply_to_cache[index]
            return match.group(0)

        new_raw = re.sub(r'(?<!\S)@(last|\d+)(?!\S)', repl, raw)
        if new_raw != raw:
            data.statement = self.statement_parser.parse(new_raw)
        return data

    @cmd2.with_category('Client Management')
    def do_replyto(self, _):
        """Lists the replyTo addresses received in requests so far.

        Replying to a request from EVerest: every incoming message with a headers.replyTo is cached, newest
        first. In a <module_id>.send_reply_* command, TAB on the replyTo argument completes from the cache,
        and the word @last (or @1, @2, ...) is replaced by the most recent (or n-th) address.

        Sending a request to EVerest: if headers.replyTo in the payload of a <module_id>.send_request_*
        command is the replyToPlaceholder (see 'set'), it is replaced by a fresh reply address
        (the request topic with e2m instead of m2e, plus a unique id). TAB on the payload inserts the
        placeholder, and the reply is received with the matching <module_id>.receive_reply_* command.
        """
        if not self.reply_to_cache:
            self.poutput('No replyTo addresses received yet. Subscribe to requests (e.g. pm.receive_request_stop_transaction) or add a spy client first (client_add spy).')
        for index, reply_to in enumerate(self.reply_to_cache, 1):
            self.poutput(f"@{index}\t{reply_to}")

    def line_after_command(self, statement):
        """The rest of the line after the command word, as typed: cmd2's arguments
        stop at characters like '|' and '>' and split JSON at its spaces. An
        alias is replaced by its value, and the line ends at a ';' that ends
        the command for cmd2."""
        word, rest = re.match(r"\s*(\S*)\s?(.*)", statement.raw, re.S).groups()
        expanded = set()
        while word in self.aliases and word not in expanded:
            expanded.add(word)
            word, rest = re.match(r"\s*(\S*)\s?(.*)", self.aliases[word] + (" " + rest if rest else ""),
                                  re.S).groups()
        if statement.terminator:
            rest = cut_at_terminator(rest)
        return rest.rstrip()

    @cmd2.with_category('Client Management')
    def do_echo(self, statement: cmd2.Statement):
        """Usage: echo <text>

        Prints the text to the console as typed. Useful in scripts to mark phases; prints even while muted."""
        text = self.line_after_command(statement)
        self.poutput(self.IN_COLOR + text + self.RESET if self.colors else text)

    wait_for_parser = cmd2.Cmd2ArgumentParser()
    wait_for_parser.add_argument('topic_match', help='substring that must appear in the message topic')
    wait_for_parser.add_argument('value_match', nargs='?', default='',
                                 help='substring that must appear in the message payload (default: any)')
    wait_for_parser.add_argument('--timeout', type=float, default=30.0,
                                 help='seconds to wait before giving up (default: 30)')
    wait_for_parser.add_argument('--optional', action='store_true',
                                 help='on timeout, print a note and continue instead of failing '
                                      '(use in scripts for a message that only appears sometimes, '
                                      'e.g. a startup handshake that is absent on a re-run)')
    @cmd2.with_argparser(wait_for_parser)
    @cmd2.with_category('Client Management')
    def do_wait_for(self, args):
        """Blocks until a message whose topic contains TOPIC_MATCH and payload contains VALUE_MATCH is received.

        Requires a spy client (or a subscription that logs the topic) to be active. Useful in scripts to
        wait for a state, e.g.:  wait_for /bsp_api/e2m/enable true
                                 wait_for session_event Authorized
        Messages that arrived since the previous command started count as well, so a state published
        in reaction to the previous command is not missed.
        A timeout is an error that stops a running script and sets the exit code to 1, unless --optional.
        """
        event = threading.Event()
        waiter = (event, args.topic_match, args.value_match)
        with self.wait_waiters_lock:
            self.wait_waiters.append(waiter)
            if any(arrived >= self.previous_command_start and args.topic_match in topic and args.value_match in payload
                   for arrived, topic, payload in self.recent_messages):
                event.set()
        try:
            if event.wait(timeout=args.timeout):
                self.poutput(f"matched: topic contains '{args.topic_match}'" +
                             (f", payload contains '{args.value_match}'" if args.value_match else ""))
                return
            self.timed_out(f"wait_for timed out after {args.timeout}s waiting for topic '{args.topic_match}'" +
                           (f" / value '{args.value_match}'" if args.value_match else ""), args.optional)
        finally:
            with self.wait_waiters_lock:
                self.wait_waiters.remove(waiter)

    def complete_module_id(self, text, line, begidx, endidx):
        return [module["id"] for module in self.api_clients if module["id"].startswith(text)]

    def process_output_queue(self):
        while not self.stop_requested:
            try:
                message = self.output_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            try:
                # async_alert redraws the prompt; it only works while the prompt waits
                # for input on a terminal, otherwise the message is printed directly
                if rl_utils.vt100_support and self.use_rawinput and self.terminal_lock.acquire(blocking=False):
                    try:
                        self.async_alert(message)
                    finally:
                        self.terminal_lock.release()
                else:
                    with self.output_lock:
                        self.stdout.write(message + "\n")
                        self.stdout.flush()
            finally:
                self.output_queue.task_done()

    def check_match(self, topic, substr_list):
        for substr in substr_list:
            if substr.strip() in topic:
                return True
        return False

    def generate_replyTo(self, payload, topic):
        if not self.replyToPlaceholder:
            return payload
        return payload.replace(self.replyToPlaceholder, f"{reply_topic(topic)}/{uuid.uuid4()}")

    def _notify_waiters(self, topic, payload):
        with self.wait_waiters_lock:
            self.recent_messages.append((time.monotonic(), topic, payload))
            for event, topic_match, value_match in self.wait_waiters:
                if topic_match in topic and value_match in payload:
                    event.set()

    def log_event(self, topic, payload):
        # cache/notify happen even while muted so wait_for and @last keep working
        self.cache_reply_to(payload)
        self._notify_waiters(topic, payload)
        if topic in ("ERROR:", "WARNING:"):
            message = self.OUT_COLOR + topic + " " + payload + self.RESET if self.colors else topic + " " + payload
            self.poutput(message) if threading.current_thread() == threading.main_thread() else self.output_queue.put(message)
            return
        if self.mute:
            return
        if 'heartbeat' in topic or 'communication_check' in topic:
            showing = not self.quiet
        else:
            showing = not self.show_topic or self.check_match(topic, self.show_topic.split(","))
            if self.hide_topic and self.check_match(topic, self.hide_topic.split(",")):
                showing = False
        if showing:
            prefix = self.IN_COLOR + "In: " if self.colors else "In: "
            if "m2e" in topic:
                prefix = self.OUT_COLOR + "Out: " if self.colors else "Out: "
            message = prefix + topic + ', ' + payload + self.RESET if self.colors else prefix + topic + ', ' + payload
            self.poutput(message) if threading.current_thread() == threading.main_thread() else self.output_queue.put(message)

    client_add_parser = cmd2.Cmd2ArgumentParser()
    client_add_parser.add_subparsers(title='client_type', help='the client type to be added')
    @cmd2.with_argparser(client_add_parser)
    @cmd2.with_category('Client Management')
    def do_client_add(self, ns: argparse.Namespace):
        """Creates a client with the given module id"""
        handler = ns.cmd2_handler.get()
        if handler is not None:
            handler(ns)
        else:
            # No subcommand was provided, so call help
            self.poutput('This command does nothing without sub-parsers registered ...')
            self.do_help('client_add')

    client_remove_parser = cmd2.Cmd2ArgumentParser()
    client_remove_parser.add_argument('module_id', help='the module id for the client', completer=complete_module_id)
    @cmd2.with_argparser(client_remove_parser)
    @cmd2.with_category('Client Management')
    def do_client_remove(self, args):
        """Removes the client(s) with the given module id and their commands (a spy client's id is 'spy' or its topic)"""
        removed = [client for client in self.api_clients if client["id"] == args.module_id]
        if not removed:
            self.perror(f"No client with id '{args.module_id}'")
            return
        for client in removed:
            if client.get("command_set") is not None:
                self.unregister_command_set(client["command_set"])
        for client in self.mqtt_clients:
            if client["id"] == args.module_id:
                close_mqtt(client["mqtt_client"])
        self.mqtt_clients = [client for client in self.mqtt_clients if client["id"] != args.module_id]
        self.api_clients = [client for client in self.api_clients if client["id"] != args.module_id]

    def complete_api_name(self, text, line, begidx, endidx):
        return [model.api for model in self.models if model.api.startswith(text)]

    apis_parser = cmd2.Cmd2ArgumentParser()
    apis_parser.add_argument('api', nargs='?', help='list the operations of this API', completer=complete_api_name)
    @cmd2.with_argparser(apis_parser)
    @cmd2.with_category('Client Management')
    def do_apis(self, args):
        """Lists the EVerest APIs loaded from the AsyncAPI specs, or the operations of one API"""
        if args.api is None:
            width = max((len(model.api) for model in self.models), default=0)
            for model in self.models:
                self.poutput(f"{model.api:<{width}}  {model.pathname}")
            return
        model = next((model for model in self.models if model.api == args.api), None)
        if model is None:
            self.perror(f"Unknown API '{args.api}'")
            return
        ops = model.send_ops + model.recv_ops
        width = max((len(op.op_id) for op in ops), default=0)
        for op in ops:
            self.poutput(f"{op.op_id:<{width}}  {op.help}")

    def postloop(self):
        # signal the worker to stop first, then join with a bound so a quit is
        # not delayed by the queue's poll interval
        self.stop_requested = True
        self.queue_processing_thread.join(timeout=1)
        for mqtt_client in self.mqtt_clients:
            close_mqtt(mqtt_client["mqtt_client"])

