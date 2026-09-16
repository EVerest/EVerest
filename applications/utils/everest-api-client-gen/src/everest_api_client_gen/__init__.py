# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Self-contained generator for the EVerest API Python MQTT clients.

Replaces the npm asyncapi-cli + python-mqtt-template toolchain with a small
Jinja2-based generator that reads only the handful of AsyncAPI fields the
clients need. See model.py for the exact spec subset consumed.
"""

__version__ = "0.1.0"
