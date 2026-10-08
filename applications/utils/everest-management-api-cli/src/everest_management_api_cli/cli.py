# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Entry point of the everest-management-api-cli command."""

import logging
import sys
from typing import List, Optional

from .commands import CommandError, build_parser, format_result, run_command
from .session import Session
from .shell import ManagementApiShell

PROG = "everest-management-api-cli"


def main(argv: Optional[List[str]] = None) -> int:
    try:
        parser, subparsers = build_parser(prog=PROG)
    except ValueError as exc:  # unusable MQTT_SERVER_PORT in the environment
        print(f"error: {exc}", file=sys.stderr)
        return 1
    subparsers.add_parser("shell", help="Interactive shell with all of the above on one connection, plus live notices.")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.WARNING,
                        format="[%(levelname)s] %(message)s", stream=sys.stderr)

    if args.command is None:
        parser.print_help()
        return 2

    session = Session(args.host, args.port, args.timeout, verbose=args.verbose)
    try:
        session.connect()
    except ValueError as exc:  # paho rejects e.g. an empty host or a port outside 1-65535
        print(f"error: invalid MQTT broker address {args.host!r}:{args.port}: {exc}", file=sys.stderr)
        return 1
    except (OSError, TimeoutError, RuntimeError) as exc:
        print(f"error: cannot connect to the MQTT broker at {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1

    try:
        if args.command == "shell":
            ManagementApiShell(session, compact=args.compact).cmdloop()
            return 0
        result = run_command(session, args)
        if result is not None:
            print(format_result(result, args.compact))
        return 0
    except (CommandError, TimeoutError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(file=sys.stderr)
        return 130
    finally:
        session.close()


if __name__ == "__main__":
    sys.exit(main())
