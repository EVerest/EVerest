# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Command line entry point for the EVerest API client generator."""

import argparse
import sys

from everest_api_client_gen import __version__
from everest_api_client_gen.generator import generate, template_files
from everest_api_client_gen.spec_loader import SpecError


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="everest-api-client-gen",
        description="Generate a Python MQTT client package from an EVerest "
                    "AsyncAPI spec (npm-free replacement for asyncapi-cli + "
                    "python-mqtt-template).")
    parser.add_argument("--version", action="version",
                        version=f"%(prog)s {__version__}")
    parser.add_argument("--get-templates", action="store_true",
                        help="print the generator's template files (one per "
                             "line) and exit; used by the build for dependency "
                             "tracking")
    parser.add_argument("--spec", help="path to the AsyncAPI YAML spec")
    parser.add_argument("--out", help="output directory for the generated package")
    parser.add_argument("--name-camel-case", dest="name_camel",
                        help="CamelCase client name, e.g. PowermeterAPI")
    parser.add_argument("--name-snake-case", dest="name_snake",
                        help="snake_case package name, e.g. powermeter_api")
    parser.add_argument("--interface-prefix", dest="interface_prefix",
                        help="short command prefix, e.g. pm")
    parser.add_argument("--server", default="default",
                        help="server name in the spec (default: default)")

    args = parser.parse_args(argv)

    if args.get_templates:
        print("\n".join(template_files()))
        return 0

    missing = [name for name, val in (
        ("--spec", args.spec), ("--out", args.out),
        ("--name-camel-case", args.name_camel),
        ("--name-snake-case", args.name_snake),
        ("--interface-prefix", args.interface_prefix)) if not val]
    if missing:
        parser.error("the following arguments are required: " + ", ".join(missing))

    try:
        generate(args.spec, args.out, args.name_camel, args.name_snake,
                 args.interface_prefix, args.server)
    except SpecError as err:
        print(f"error: {args.spec}: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
