# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Interactive shell: the same commands as the CLI, on one long-lived connection, with live notices."""

import cmd
import shlex

from .commands import CommandError, build_parser, format_notice, format_result, run_command
from .configuration_api import ACTIVE_SLOT_TOPIC, CONFIG_UPDATES_TOPIC
from .lifecycle_api import LIFECYCLE_STATUS_TOPIC
from .session import Session

SHELL_ONLY_COMMANDS = """\
shell commands:
  help [COMMAND]        show this overview or one command's usage
  timeout [SECONDS]     show or set the reply timeout
  quit, exit, Ctrl-D    leave the shell
"""


class ManagementApiShell(cmd.Cmd):
    prompt = "everest-api> "

    def __init__(self, session: Session, compact: bool = False):
        super().__init__()
        self.session = session
        self.compact = compact
        self.parser, self.subparsers = build_parser(prog="", include_connection_options=False)
        # Per-command usage lines read "usage: mark [-h] slot_id"; an unknown command gets a
        # one-line hint instead of argparse's full usage for the (nameless) top-level parser.
        for name, subparser in self.subparsers.choices.items():
            subparser.prog = name
        self.parser.usage = "COMMAND [ARGS ...]"
        self.parser.epilog = ("Replies are printed as JSON. Status updates and configuration notices "
                              "are printed as they arrive.")
        self.parser.error = self._unknown_command
        self.intro = (
            "EVerest management API shell. Type 'help' for commands, 'quit' to leave.\n"
            f"Connected to {session.host}:{session.port}; watching {LIFECYCLE_STATUS_TOPIC},\n"
            f"{ACTIVE_SLOT_TOPIC} and {CONFIG_UPDATES_TOPIC}.\n"
            "Status updates and notices are printed as they arrive.")
        self._at_prompt = True
        session.on_notice = self._print_notice

    # --- live notices ----------------------------------------------------------------------

    def _print_notice(self, topic: str, payload: dict) -> None:
        # Runs on paho's network thread. While the user is at the prompt, redraw it after the
        # notice so the line stays readable; while a command runs, just print the notice.
        if self._at_prompt:
            self.stdout.write(f"\r{format_notice(topic, payload)}\n{self.prompt}")
        else:
            self.stdout.write(f"{format_notice(topic, payload)}\n")
        self.stdout.flush()

    def precmd(self, line: str) -> str:
        self._at_prompt = False
        return line

    def postcmd(self, stop: bool, line: str) -> bool:
        self._at_prompt = True
        return stop

    # --- dispatch --------------------------------------------------------------------------

    def cmdloop(self, intro=None) -> None:
        first = True
        while True:
            try:
                super().cmdloop(intro if first else "")
                return
            except KeyboardInterrupt:
                self.stdout.write("^C\n")
                first = False

    def emptyline(self) -> None:
        pass

    def default(self, line: str) -> None:
        try:
            argv = shlex.split(line)
        except ValueError as exc:
            print(f"error: {exc}")
            return
        try:
            args = self.parser.parse_args(argv)
        except SystemExit:
            # argparse already printed the usage error or the requested help
            return
        if args.command is None:
            self.parser.print_help()
            return
        try:
            result = run_command(self.session, args)
        except KeyboardInterrupt:
            print()
            return
        except CommandError as exc:
            print(f"error: {exc}")
            return
        except (TimeoutError, RuntimeError) as exc:
            print(f"error: {type(exc).__name__}: {exc}")
            return
        if result is not None:
            print(format_result(result, self.compact))

    def _unknown_command(self, message: str) -> None:
        # Replaces ArgumentParser.error on the top-level shell parser (SystemExit is caught in default()).
        print(f"error: {message}\ntype 'help' for the list of commands")
        raise SystemExit(2)

    def completenames(self, text: str, *_ignored):
        names = list(self.subparsers.choices) + ["help", "timeout", "quit", "exit"]
        return [name for name in names if name.startswith(text)]

    # --- shell-only commands ---------------------------------------------------------------

    def do_help(self, arg: str) -> None:
        """help [COMMAND] -- show the command overview or one command's usage"""
        arg = arg.strip()
        if not arg:
            self.parser.print_help()
            print()
            print(SHELL_ONLY_COMMANDS, end="")
        elif arg in self.subparsers.choices:
            self.subparsers.choices[arg].print_help()
        else:
            super().do_help(arg)

    def do_timeout(self, arg: str) -> None:
        """timeout [SECONDS] -- show or set the reply timeout"""
        arg = arg.strip()
        if arg:
            try:
                self.session.timeout_s = float(arg)
            except ValueError:
                print(f"error: not a number: {arg!r}")
                return
        print(f"reply timeout: {self.session.timeout_s:g}s")

    def do_quit(self, _arg: str) -> bool:
        """quit -- leave the shell"""
        return True

    def do_exit(self, _arg: str) -> bool:
        """exit -- leave the shell"""
        return True

    def do_EOF(self, _arg: str) -> bool:
        print()
        return True
