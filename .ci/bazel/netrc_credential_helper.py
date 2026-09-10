#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Bazel credential helper serving HTTP Basic auth from a netrc file.

Bazel applies netrc itself only to the URL a repository rule names. GitHub
answers those URLs with a redirect to another host, for example
github.com/<owner>/<repo>/archive/<ref>.tar.gz to codeload.github.com, and
Bazel drops the Authorization header across a cross-host redirect without
re-reading netrc for the new host. The request that transfers the archive is
therefore anonymous and shares the per-IP rate limit that answers 429.

Bazel does consult a credential helper again for the redirect target, so
serving netrc through this helper keeps every hop authenticated. It is wired up
per host in .bazelrc; see the --credential_helper lines there.

Protocol: https://github.com/EngFlow/credential-helper-spec
"""

import base64
import json
import os
import sys
from netrc import netrc
from urllib.parse import urlparse

NO_CREDENTIALS = {"headers": {}}


def lookup(uri):
    host = urlparse(uri).hostname
    path = os.environ.get("NETRC") or os.path.join(os.path.expanduser("~"), ".netrc")
    if not host or not os.path.exists(path):
        return NO_CREDENTIALS
    entry = netrc(path).authenticators(host)
    if entry is None:
        return NO_CREDENTIALS
    login, _, password = entry
    basic = base64.b64encode(f"{login}:{password}".encode()).decode()
    return {"headers": {"Authorization": [f"Basic {basic}"]}}


def main(argv):
    if len(argv) != 2 or argv[1] != "get":
        print(f"usage: {os.path.basename(argv[0])} get", file=sys.stderr)
        return 2
    request = json.load(sys.stdin)
    json.dump(lookup(request.get("uri", "")), sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
