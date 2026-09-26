#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""EVerest API command line client application"""
import uuid
import os
import re
import sys
import json
import argparse
import importlib
import cmd2
import queue
import threading
from spyclient_api.SpyClientCommandSet import SpyClientCommandSet

# One (package, class name) entry per generated API client: importing the
# command set module registers its CommandSet subclass with cmd2, which
# instantiates every discovered CommandSet on startup. Missing packages are
# skipped with a warning so a partial wheel installation still yields a
# working shell.
GENERATED_API_CLIENTS = [
    ("auth_consumer_api", "AuthConsumerAPI"),
    ("auth_token_provider_api", "AuthTokenProviderAPI"),
    ("auth_token_validator_api", "AuthTokenValidatorAPI"),
    ("dc_external_derate_consumer_api", "DcExternalDerateConsumerAPI"),
    ("display_message_api", "DisplayMessageAPI"),
    ("error_history_consumer_api", "ErrorHistoryConsumerAPI"),
    ("ev_bsp_api", "EvBspAPI"),
    ("evse_bsp_api", "EvseBspAPI"),
    ("evse_manager_consumer_api", "EvseManagerConsumerAPI"),
    ("evse_security_consumer_api", "EvseSecurityConsumerAPI"),
    ("external_energy_limits_consumer_api", "ExternalEnergyLimitsConsumerAPI"),
    ("generic_error_raiser_api", "GenericErrorRaiserAPI"),
    ("isolation_monitor_api", "IsolationMonitorAPI"),
    ("ocpp_consumer_api", "OcppConsumerAPI"),
    ("over_voltage_monitor_api", "OverVoltageMonitorAPI"),
    ("power_supply_dc_api", "PowerSupplyDCAPI"),
    ("powermeter_api", "PowermeterAPI"),
    ("session_cost_api", "SessionCostAPI"),
    ("session_cost_consumer_api", "SessionCostConsumerAPI"),
    ("slac_api", "SlacAPI"),
    ("system_api", "SystemAPI"),
]


def load_generated_command_sets():
    for snake_name, camel_name in GENERATED_API_CLIENTS:
        module_name = f"{snake_name}.{camel_name}ClientCommandSet"
        try:
            importlib.import_module(module_name)
        except ImportError as err:
            print(f"Warning: API client package '{snake_name}' is not available ({err}); "
                  "its commands will be missing", file=sys.stderr)


class EVerestAPICmd(cmd2.Cmd):
    """EVerest API command line client application"""

    def __init__(self):
        super().__init__(include_py=True, persistent_history_file=os.path.expanduser("~/.everest_api_client"))
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

        self.add_settable(cmd2.Settable('show_topic', str, 'Comma separated list. If set shows ONLY the messages with topics matching the list\'s strings. If hide_topic is set too, it will be applied first, then show_topic will be applied.', self))
        self.add_settable(cmd2.Settable('hide_topic', str, 'Comma separate list. If set hides ONLY the messages with topics matching the list\'s strings. If show_topic is set too, hide_topic is applied first, then show_topic.', self))
        self.add_settable(cmd2.Settable('prettify', bool, 'Prettify the json outputs', self))
        self.add_settable(cmd2.Settable('colors', bool, 'Use colors for the outputs', self))
        self.add_settable(cmd2.Settable('mute', bool, 'Suppress all incoming/outgoing message logging (echo and wait_for still print)', self))
        self.add_settable(cmd2.Settable('replyToPlaceholder', str, 'The placeholder to search and replace in a payload', self))

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
        self._attach_replyto_completers()
        self.register_postparsing_hook(self._substitute_replyto_tokens)

    def _cache_reply_to(self, payload):
        try:
            reply_to = json.loads(payload).get("headers", {}).get("replyTo")
        except (ValueError, AttributeError):
            return
        if isinstance(reply_to, str) and reply_to:
            if reply_to in self.reply_to_cache:
                self.reply_to_cache.remove(reply_to)
            self.reply_to_cache.insert(0, reply_to)
            del self.reply_to_cache[50:]

    def _attach_replyto_completers(self):
        # The generated command sets declare their reply parsers as class
        # attributes with a plain 'replyTo' positional; give all of them a
        # completer fed from reply_to_cache. When the module id has already
        # been typed on the line, only that client's addresses are offered.
        app = self

        def completer(*args):
            text, line = args[-4], args[-3]
            candidates = app.reply_to_cache
            for token in line.split()[1:]:
                scoped = [rt for rt in candidates if f"/{token}/" in rt]
                if scoped:
                    candidates = scoped
                    break
            return [rt for rt in candidates if rt.startswith(text)]

        seen = set()
        stack = list(cmd2.CommandSet.__subclasses__()) + [type(self)]
        while stack:
            cls = stack.pop()
            if cls in seen:
                continue
            seen.add(cls)
            stack.extend(cls.__subclasses__())
            for attr in vars(cls).values():
                if isinstance(attr, argparse.ArgumentParser):
                    for action in attr._actions:
                        if action.dest == 'replyTo':
                            action.set_completer(completer)

    def _substitute_replyto_tokens(self, data: cmd2.plugin.PostparsingData) -> cmd2.plugin.PostparsingData:
        raw = data.statement.raw

        def repl(match):
            index = 0 if match.group(1) in (None, 'last') else int(match.group(1)) - 1
            if 0 <= index < len(self.reply_to_cache):
                return self.reply_to_cache[index]
            return match.group(0)

        new_raw = re.sub(r'@(last|\d+)\b', repl, raw)
        if new_raw != raw:
            data.statement = self.statement_parser.parse(new_raw)
        return data

    @cmd2.with_category('Client Management')
    def do_replyto(self, _):
        """Lists the replyTo addresses received in requests so far. Use @1 (or @last) .. @<n> in any command to substitute one."""
        if not self.reply_to_cache:
            self.poutput('No replyTo addresses received yet. Subscribe to requests (e.g. pm_receive_request_stop_transaction) or add a SpyClient first.')
        for index, reply_to in enumerate(self.reply_to_cache, 1):
            self.poutput(f"@{index}\t{reply_to}")

    echo_parser = cmd2.Cmd2ArgumentParser()
    echo_parser.add_argument('message', nargs=argparse.REMAINDER, help='text to print')
    @cmd2.with_argparser(echo_parser)
    @cmd2.with_category('Client Management')
    def do_echo(self, args):
        """Prints a message to the console. Useful in scripts to mark phases; prints even while muted."""
        text = ' '.join(args.message)
        self.poutput(self.IN_COLOR + text + self.RESET if self.colors else text)

    wait_for_parser = cmd2.Cmd2ArgumentParser()
    wait_for_parser.add_argument('topic_match', help='substring that must appear in the message topic')
    wait_for_parser.add_argument('value_match', nargs='?', default='',
                                 help='substring that must appear in the message payload (default: any)')
    wait_for_parser.add_argument('--timeout', type=float, default=30.0,
                                 help='seconds to wait before giving up (default: 30)')
    wait_for_parser.add_argument('--optional', action='store_true',
                                 help='on timeout, print a note and continue instead of raising an error '
                                      '(use in scripts for a request that only appears sometimes, '
                                      'e.g. a startup handshake that is absent on a re-run)')
    @cmd2.with_argparser(wait_for_parser)
    @cmd2.with_category('Client Management')
    def do_wait_for(self, args):
        """Blocks until a message whose topic contains TOPIC_MATCH and payload contains VALUE_MATCH is received.

        Requires a SpyClient (or a subscription that logs the topic) to be active. Useful in scripts to
        wait for a state, e.g.:  wait_for /bsp_api/e2m/enable true
                                 wait_for session_event Authorized
        Returns True on timeout so a script started with `run_script -t` can branch; with --optional a
        timeout is not treated as an error.
        """
        event = threading.Event()
        waiter = (event, args.topic_match, args.value_match)
        with self.wait_waiters_lock:
            self.wait_waiters.append(waiter)
        try:
            if event.wait(timeout=args.timeout):
                self.poutput(f"matched: topic contains '{args.topic_match}'" +
                             (f", payload contains '{args.value_match}'" if args.value_match else ""))
                return False
            note = (f"wait_for timed out after {args.timeout}s waiting for topic '{args.topic_match}'" +
                    (f" / value '{args.value_match}'" if args.value_match else ""))
            if args.optional:
                self.poutput(note + " (optional, continuing)")
                return False
            self.perror(note)
            return True
        finally:
            with self.wait_waiters_lock:
                self.wait_waiters.remove(waiter)

    def complete_module_id(self, text, line, begidx, endidx):
        return [module["id"] for module in self.api_clients if module["id"].startswith(text)]

    def get_instance_by_id(self, target_id):
        return next((client["instance"] for client in self.api_clients if client["id"] == target_id), None)

    def process_output_queue(self):
        while not self.stop_requested:
            try:
                message = self.output_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            try:
                with self.terminal_lock:
                    self.async_alert(message)
            finally:
                self.output_queue.task_done()

    def check_match(self, topic, substr_list):
        for substr in substr_list:
            if substr.strip() in topic:
                return True
        return False

    def generate_replyTo(self, payload, topic):
        return payload.replace(self.replyToPlaceholder,
                               topic.replace("m2e", "e2m") + "/" +
                               str(uuid.uuid4()))

    def _notify_waiters(self, topic, payload):
        if not self.wait_waiters:
            return
        with self.wait_waiters_lock:
            for event, topic_match, value_match in self.wait_waiters:
                if topic_match in topic and value_match in payload:
                    event.set()

    def log_event(self, topic, payload):
        # cache/notify happen even while muted so wait_for and @last keep working
        self._cache_reply_to(payload)
        self._notify_waiters(topic, payload)
        if self.mute:
            return
        showing = True
        if ('heartbeat' in topic or 'communication_check' in topic):
            if self.quiet:
                showing = False
        else:
            if self.show_topic and self.hide_topic:
                if self.check_match(topic, self.hide_topic.split(",")):
                    showing = False
                if self.check_match(topic, self.show_topic.split(",")):
                    showing = True
            else:
                if self.show_topic and not self.check_match(topic, self.show_topic.split(",")):
                    showing = False
                if self.hide_topic and self.check_match(topic, self.hide_topic.split(",")):
                    showing = False
        if showing:
            if "ERROR:" in topic:
                if threading.current_thread() == threading.main_thread():
                    self.poutput(self.OUT_COLOR + topic + " " + payload + self.RESET if self.colors else topic + " " + payload)
                else:
                    self.output_queue.put(topic + payload)
            else:
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
        """Removes a client with the given module id"""
        for index, client in enumerate(self.mqtt_clients):
            if client["id"] == args.module_id:
                try:
                    client["mqtt_client"].disconnect()
                except Exception:
                    pass
                client["mqtt_client"].loop_stop()
                del self.mqtt_clients[index]
                break
        self.api_clients = [client for client in self.api_clients if client["id"] != args.module_id]

    def complete_send_communication_check_payload(self, text, line, begidx, endidx):
        return [payload for payload in ['true'] if payload.startswith(text)]

    send_communication_check_parser = cmd2.Cmd2ArgumentParser()
    send_communication_check_parser.add_argument('module_id', help='the module id for the client', completer=complete_module_id)
    send_communication_check_parser.add_argument('payload', help='the payload as json to be send', completer=complete_send_communication_check_payload)
    @cmd2.with_argparser(send_communication_check_parser, preserve_quotes=True)
    @cmd2.with_category('Client Management')
    def do_send_communication_check(self, args):
        """Sends a 'heartbeat' like signal to the EVerest API to confirm that the external module is alive"""
        api_client = self.get_instance_by_id(args.module_id)
        if api_client:
            api_client.send.send_communication_check.publish(json.dumps(json.loads(args.payload)))
        else:
            self.log_event("ERROR:", "Invalid module id: '" + args.module_id + "' Have you created the client using the 'add_client' function?")

    receive_heartbeat_parser = cmd2.Cmd2ArgumentParser()
    receive_heartbeat_parser.add_subparsers(title='client_type', help='the client type to be added')
    @cmd2.with_argparser(receive_heartbeat_parser)
    @cmd2.with_category('Client Management')
    def do_receive_heartbeat(self, ns: argparse.Namespace):
        """Blocks until it receives the heartbeat signal from the EVerest API"""
        handler = ns.cmd2_handler.get()
        if handler is not None:
            handler(ns)
        else:
            # No subcommand was provided, so call help
            self.poutput('This command does nothing without sub-parsers registered ...')
            self.do_help('client_add')

    def postloop(self):
        # signal the worker to stop first, then join with a bound so a quit is
        # not delayed by the queue's poll interval
        self.stop_requested = True
        self.queue_processing_thread.join(timeout=1)
        # disconnect() before loop_stop(): loop_stop() on a still-connected
        # client blocks ~0.7s waiting for its network loop to wind down, which
        # adds up across clients and makes quitting feel stuck
        for mqtt_client in self.mqtt_clients:
            try:
                mqtt_client["mqtt_client"].disconnect()
            except Exception:
                pass
            mqtt_client["mqtt_client"].loop_stop()


if __name__ == '__main__':
    load_generated_command_sets()
    cmd = EVerestAPICmd()
    sys.exit(cmd.cmdloop())
