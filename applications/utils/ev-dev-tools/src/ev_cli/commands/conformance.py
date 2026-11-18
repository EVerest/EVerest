# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli conformance-report``"""

from __future__ import annotations

from ..schema.conformance import collect_findings, format_report, format_summary
from .context import Context


def report(args) -> int:
    """Report where the definitions depend on EVerest's own dialect.

    This is the measure of the cleanup that is still outstanding: every entry
    is a place where the source relies on a reading of JSON Schema that only
    EVerest's own tooling provides.  The counts should only ever fall.
    """
    findings = collect_findings(Context.from_args(args).loader)
    print(format_report(findings) if args.verbose else format_summary(findings))

    if args.strict and findings:
        return 1
    return 0
