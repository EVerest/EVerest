# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Command definitions shared by the one-shot CLI and the interactive shell.

Each command is one argparse subparser whose `handler` default is a function
`handler(session, args) -> result`. A dict/list result is printed as JSON, a str verbatim,
None prints nothing. Handlers raise CommandError for problems that should be reported to
the user without a traceback.
"""

import argparse
import json
import threading
from pathlib import Path
from typing import Any, List, Optional, Tuple

import yaml

from . import __version__
from .configuration_api import (
    ACTIVE_SLOT_TOPIC,
    CONFIG_UPDATES_TOPIC,
    ParameterId,
    ParameterUpdate,
    configuration_command_topic,
)
from .lifecycle_api import LIFECYCLE_STATUS_TOPIC, default_broker, lifecycle_command_topic
from .mqtt_rpc import perform_rpc
from .session import Session

DESCRIPTION = "Drive EVerest's lifecycle and configuration management APIs over MQTT."

EPILOG = """\
The manager must run with --lifecycle-api[=rw] and/or --configuration-api[=rw]; without a
value both APIs start read-only. Replies are printed as JSON. Use 'shell' for an interactive
session that also shows status updates and configuration notices as they arrive.
"""

NOTICE_LABELS = {
    LIFECYCLE_STATUS_TOPIC: "status",
    ACTIVE_SLOT_TOPIC: "active_slot",
    CONFIG_UPDATES_TOPIC: "config_updates",
}

# The API clients default to 10s, generous enough for the integration tests on a loaded runner;
# an interactive user is better served by a quicker "no manager is answering".
DEFAULT_CLI_TIMEOUT_S = 4.0

COMMAND_TOPIC_FUNCTIONS = {
    "lifecycle": lifecycle_command_topic,
    "configuration": configuration_command_topic,
}


class CommandError(Exception):
    """A command could not be carried out; the message is shown to the user as is."""


def format_notice(topic: str, payload: dict) -> str:
    return f"[{NOTICE_LABELS.get(topic, topic)}] {json.dumps(payload)}"


def format_result(result: Any, compact: bool = False) -> str:
    if isinstance(result, str):
        return result
    if compact:
        return json.dumps(result, separators=(",", ":"))
    return json.dumps(result, indent=2)


def _slot_id(text: str) -> int:
    try:
        return int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"slot id must be an integer, got {text!r}")


# --- lifecycle API -------------------------------------------------------------------------


def cmd_status(session: Session, args) -> Any:
    updates = session.lifecycle.status_updates
    if not updates:
        try:
            session.lifecycle.wait_for_status(lambda _update: True, timeout_s=args.wait)
        except TimeoutError:
            raise CommandError(
                f"no lifecycle status received within {args.wait:g}s "
                "-- is a manager with --lifecycle-api running?")
        updates = session.lifecycle.status_updates
    return updates if args.all else updates[-1]


def cmd_version(session: Session, _args) -> dict:
    return session.lifecycle.get_everest_version(session.timeout_s)


def cmd_start(session: Session, _args) -> dict:
    return session.lifecycle.start_modules(session.timeout_s)


def cmd_stop(session: Session, _args) -> dict:
    return session.lifecycle.stop_modules(session.timeout_s)


def cmd_clear_retained_status(session: Session, _args) -> str:
    session.lifecycle.clear_retained_status()
    return f"cleared retained message on {LIFECYCLE_STATUS_TOPIC}"


# --- configuration API ---------------------------------------------------------------------


def cmd_slots(session: Session, _args) -> dict:
    return session.configuration.list_all_slots(session.timeout_s)


def cmd_active(session: Session, _args) -> dict:
    return session.configuration.get_active_slot(session.timeout_s)


def cmd_mark(session: Session, args) -> dict:
    return session.configuration.mark_active_slot(args.slot_id, session.timeout_s)


def cmd_delete(session: Session, args) -> dict:
    return session.configuration.delete_slot(args.slot_id, session.timeout_s)


def cmd_dup(session: Session, args) -> dict:
    return session.configuration.duplicate_slot(args.slot_id, args.description, session.timeout_s)


def cmd_desc(session: Session, args) -> dict:
    return session.configuration.set_description(args.slot_id, " ".join(args.description), session.timeout_s)


def cmd_config(session: Session, args) -> dict:
    return session.configuration.get_configuration(args.slot_id, args.force_db, session.timeout_s)


PARAMETER_FILE_HELP = """\
The file uses the layout of a module configuration, reduced to the parameters of interest:

    active_modules:
      example:                  # module id
        config_module:          # module-level parameters
          log_interval: 3
        config_implementation:
          example:              # implementation id
            current: 40
"""


def _scalar_to_value(value: Any) -> str:
    """Parameter values travel as strings; non-string YAML scalars are sent in their JSON spelling
    (true/false, 3, 40.5), which is what the manager parses them back from."""
    if isinstance(value, str):
        return value
    if value is None:
        raise CommandError("parameter values must not be empty (an empty value would be sent as the string 'null')")
    if isinstance(value, (dict, list)):
        raise CommandError(f"parameter values must be scalars, got {type(value).__name__}: {value!r}")
    return json.dumps(value)


def _mapping(value: Any, what: str) -> dict:
    """The parameter file is nested mappings all the way down; an empty level is fine, anything
    else (a list, a scalar where a parameter-name level was forgotten) is reported by location."""
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise CommandError(f"{what} must be a mapping of names to values, got {type(value).__name__}: {value!r}")
    return value


def parse_parameter_file(text: str) -> List[ParameterUpdate]:
    """Parse a parameter file (see PARAMETER_FILE_HELP) into (module_id, parameter, impl or None, value)."""
    try:
        parsed = yaml.safe_load(text)
    except yaml.YAMLError as exc:
        raise CommandError(f"not valid YAML: {exc}")
    modules = parsed.get("active_modules") if isinstance(parsed, dict) else None
    if not isinstance(modules, dict):
        raise CommandError("parameter file needs an 'active_modules' mapping")
    updates: List[ParameterUpdate] = []
    for module_id, module in modules.items():
        module = _mapping(module, f"module {module_id!r}")
        config_module = _mapping(module.get("config_module"), f"config_module of module {module_id!r}")
        for name, value in config_module.items():
            updates.append((module_id, name, None, _scalar_to_value(value)))
        implementations = _mapping(module.get("config_implementation"),
                                   f"config_implementation of module {module_id!r}")
        for implementation_id, parameters in implementations.items():
            parameters = _mapping(parameters, f"implementation {implementation_id!r} of module {module_id!r}")
            for name, value in parameters.items():
                updates.append((module_id, name, implementation_id, _scalar_to_value(value)))
    if not updates:
        raise CommandError("parameter file contains no parameters")
    return updates


def _read_text(path_text: str) -> str:
    path = Path(path_text).expanduser()
    try:
        return path.read_text()
    except OSError as exc:
        raise CommandError(f"cannot read {path}: {exc}")


def _parameter_updates(args, need_value: bool) -> List[ParameterUpdate]:
    """Resolve the parameter selection of getp/setp: either one parameter given positionally or a
    file with many. For getp (need_value=False) the value of a positional parameter is None."""
    if args.file is not None:
        if args.module_id is not None:
            raise CommandError("give either --file or module_id/parameter, not both")
        return parse_parameter_file(_read_text(args.file))
    if args.module_id is None or args.parameter is None or (need_value and args.value is None):
        raise CommandError("module_id and parameter" + (" and value" if need_value else "")
                           + " are required unless --file is given")
    return [(args.module_id, args.parameter, args.implementation_id, args.value if need_value else None)]


def cmd_getp(session: Session, args) -> dict:
    parameters: List[ParameterId] = [(m, p, i) for m, p, i, _v in _parameter_updates(args, need_value=False)]
    return session.configuration.get_config_parameters(args.slot_id, parameters, args.force_db,
                                                       session.timeout_s)


def cmd_setp(session: Session, args) -> dict:
    updates = _parameter_updates(args, need_value=True)
    return session.configuration.set_config_parameters(args.slot_id, updates, session.timeout_s)


def prepare_yaml_for_slot(text: str, as_is: bool = False) -> str:
    """A slot carries module configuration only. Given a full EVerest config file, keep just its
    active_modules subtree so the manager's own settings section is not sent along."""
    if as_is:
        return text
    try:
        parsed = yaml.safe_load(text)
    except yaml.YAMLError as exc:
        raise CommandError(f"not valid YAML (use --as-is to send it anyway): {exc}")
    if isinstance(parsed, dict) and "active_modules" in parsed:
        return yaml.safe_dump({"active_modules": parsed["active_modules"]})
    return text


def cmd_load(session: Session, args) -> dict:
    raw_yaml = prepare_yaml_for_slot(_read_text(args.file), args.as_is)
    return session.configuration.load_from_yaml(raw_yaml, args.description, args.slot, session.timeout_s)


# --- generic -------------------------------------------------------------------------------


def parse_raw_payload(parts) -> dict:
    text = " ".join(parts).strip() if parts else ""
    if not text:
        return {}
    try:
        payload = json.loads(text)
    except ValueError as exc:
        raise CommandError(f"payload is not valid JSON: {exc}")
    return payload


def cmd_raw(session: Session, args) -> dict:
    topic = COMMAND_TOPIC_FUNCTIONS[args.api](args.command_name)
    return perform_rpc(session.mqtt, topic, parse_raw_payload(args.payload), session.timeout_s)


def cmd_watch(session: Session, args) -> None:
    def printer(topic: str, payload: dict) -> None:
        print(format_notice(topic, payload), flush=True)

    latest = session.lifecycle.latest_status
    if latest is not None:
        printer(LIFECYCLE_STATUS_TOPIC, latest)
    previous = session.on_notice
    session.on_notice = printer
    try:
        # Notices arrive on paho's network thread; the main thread only has to block. Nothing
        # sets this event: wait() returns when --duration passes, or raises KeyboardInterrupt
        # (a lock wait on the main thread is interruptible by SIGINT on POSIX).
        threading.Event().wait(args.duration)
    finally:
        session.on_notice = previous


# --- parser --------------------------------------------------------------------------------


def add_commands(subparsers) -> None:
    """Register every API command on `subparsers` (from ArgumentParser.add_subparsers)."""

    def add(name: str, handler, help_text: str, description: Optional[str] = None) -> argparse.ArgumentParser:
        """`help_text` is the one-liner in the command overview; `description` (default: the same
        text) heads the command's own --help and may carry more detail."""
        parser = subparsers.add_parser(name, help=help_text, description=description or help_text,
                                       formatter_class=argparse.RawDescriptionHelpFormatter)
        parser.set_defaults(handler=handler)
        return parser

    def slot_arg(parser: argparse.ArgumentParser) -> None:
        parser.add_argument("slot_id", type=_slot_id, help="configuration slot id")

    def param_args(parser: argparse.ArgumentParser, with_value: bool) -> None:
        """One parameter given positionally, or many from a file (see _parameter_updates)."""
        parser.add_argument("module_id", nargs="?")
        parser.add_argument("parameter", nargs="?", help="parameter name")
        if with_value:
            parser.add_argument("value", nargs="?", help="new value, as a string")
        parser.add_argument("--impl", dest="implementation_id", metavar="IMPLEMENTATION_ID",
                            help="address an implementation-level parameter instead of a module-level one")
        parser.add_argument("-f", "--file", metavar="FILE",
                            help="YAML file with the parameters" + (" and values to set" if with_value else " to query"))

    def db_arg(parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--db", dest="force_db", action="store_true",
                            help="force_read_from_db: read the persisted value instead of the in-memory one")

    # lifecycle API
    parser = add("status", cmd_status, "Lifecycle API: show the latest status update.",
                 "Lifecycle API: show the latest status update.\n\n"
                 "The status topic is retained on the broker, so this works whenever a manager has published "
                 "one, and a killed manager's last-will status stays visible until cleared "
                 "(see clear-retained-status).")
    parser.add_argument("--all", action="store_true",
                        help="print every status update received in this session, not only the latest")
    parser.add_argument("--wait", type=float, default=2.0, metavar="SECONDS",
                        help="how long to wait for a first status update (default: %(default)s)")
    add("version", cmd_version, "Lifecycle API: get_everest_version.")
    add("start", cmd_start, "Lifecycle API: start_modules.")
    add("stop", cmd_stop, "Lifecycle API: stop_modules.")
    add("clear-retained-status", cmd_clear_retained_status,
        "Clear the retained lifecycle status left behind by a killed manager.",
        "Clear the retained lifecycle status left behind by a killed manager.\n\n"
        "When the manager dies without disconnecting cleanly, the broker publishes its last-will status "
        "and keeps it retained, so 'status' would report it until a new manager overwrites it.")

    # configuration API
    add("slots", cmd_slots, "Configuration API: list_all_slots.")
    add("active", cmd_active, "Configuration API: get_active_slot.")
    slot_arg(add("mark", cmd_mark, "Configuration API: mark_active_slot."))
    slot_arg(add("delete", cmd_delete, "Configuration API: delete_slot."))
    parser = add("dup", cmd_dup, "Configuration API: duplicate_slot.")
    slot_arg(parser)
    parser.add_argument("-d", "--description", help="description for the new slot")
    parser = add("desc", cmd_desc, "Configuration API: set_description.")
    slot_arg(parser)
    parser.add_argument("description", nargs="+", help="new description (several words are joined)")
    parser = add("config", cmd_config, "Configuration API: get_configuration.")
    slot_arg(parser)
    db_arg(parser)
    parser = add("getp", cmd_getp, "Configuration API: get_config_parameters, one or from a file.",
                 "Configuration API: get_config_parameters.\n\nQuery one parameter given as module_id and "
                 "parameter (plus --impl for an implementation-level one), or every parameter listed in a "
                 "file given with --file; the file's values are ignored.\n\n" + PARAMETER_FILE_HELP)
    slot_arg(parser)
    param_args(parser, with_value=False)
    db_arg(parser)
    parser = add("setp", cmd_setp, "Configuration API: set_config_parameters, one or from a file.",
                 "Configuration API: set_config_parameters.\n\nSet one parameter given as module_id, "
                 "parameter and value (plus --impl for an implementation-level one), or every parameter "
                 "listed in a file given with --file.\n\n" + PARAMETER_FILE_HELP)
    slot_arg(parser)
    param_args(parser, with_value=True)
    parser = add("load", cmd_load, "Configuration API: load_from_yaml with the contents of a file.",
                 "Configuration API: load_from_yaml with the contents of a file.\n\n"
                 "A full EVerest config file (one with an active_modules key) is reduced to its "
                 "active_modules subtree first, since a slot holds no manager settings.")
    parser.add_argument("file", help="YAML file to load")
    parser.add_argument("-d", "--description", help="description for the slot")
    parser.add_argument("--slot", type=_slot_id, metavar="SLOT_ID",
                        help="load into this existing slot instead of creating a new one")
    parser.add_argument("--as-is", action="store_true", help="send the file contents unchanged")

    # generic
    parser = add("raw", cmd_raw, "Send an arbitrary request to either API, e.g. for malformed-payload tests.",
                 "Send an arbitrary request to either API, e.g. for malformed-payload tests.\n\n"
                 "example: raw configuration mark_active_slot '{\"slot_id\": 999}'")
    parser.add_argument("api", choices=sorted(COMMAND_TOPIC_FUNCTIONS))
    parser.add_argument("command_name", metavar="command", help="command name, i.e. the last topic segment")
    parser.add_argument("payload", nargs="*", help="JSON object (default: {}); quote it as one argument")
    parser = add("watch", cmd_watch,
                 "Print lifecycle status updates and configuration notices as they arrive, until Ctrl-C.")
    parser.add_argument("--duration", type=float, metavar="SECONDS",
                        help="stop after this many seconds instead of running until Ctrl-C")


def build_parser(prog: Optional[str] = None,
                 include_connection_options: bool = True) -> Tuple[argparse.ArgumentParser, Any]:
    """Build the command parser. Returns (parser, subparsers action) so callers can add commands."""
    parser = argparse.ArgumentParser(prog=prog, description=DESCRIPTION, epilog=EPILOG,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    if include_connection_options:
        host, port = default_broker()
        parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
        parser.add_argument("--host", default=host,
                            help="MQTT broker host (default: $MQTT_SERVER_ADDRESS or %(default)s)")
        parser.add_argument("--port", type=int, default=port,
                            help="MQTT broker port (default: $MQTT_SERVER_PORT or %(default)s)")
        parser.add_argument("--timeout", type=float, default=DEFAULT_CLI_TIMEOUT_S, metavar="SECONDS",
                            help="how long to wait for a reply (default: %(default)s)")
        parser.add_argument("--compact", action="store_true",
                            help="print replies as single-line JSON, e.g. for piping into jq")
        parser.add_argument("-v", "--verbose", action="store_true", help="log MQTT client details")
    subparsers = parser.add_subparsers(dest="command", metavar="COMMAND")
    add_commands(subparsers)
    return parser, subparsers


def run_command(session: Session, args) -> Any:
    return args.handler(session, args)
