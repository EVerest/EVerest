# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Load an AsyncAPI YAML spec and resolve the local $refs the generator needs.

Only local refs (``#/...``) are supported; the EVerest API specs are entirely
self-contained. Anything unexpected raises so a malformed spec fails the build
loudly rather than producing a silently wrong client.
"""

from pathlib import Path

import yaml


class SpecError(Exception):
    """Raised when a spec cannot be processed."""


def load_spec(path):
    """Parse the YAML spec at *path*. Mapping order is preserved (PyYAML), which
    the generator relies on to emit operations in source order."""
    text = Path(path).read_text()
    doc = yaml.safe_load(text)
    if not isinstance(doc, dict):
        raise SpecError(f"{path}: top-level YAML is not a mapping")
    return doc


def resolve_ref(doc, ref):
    """Resolve a local JSON ``$ref`` (e.g. ``#/channels/foo``) against *doc*."""
    if not isinstance(ref, str) or not ref.startswith("#/"):
        raise SpecError(f"unsupported or external $ref: {ref!r}")
    node = doc
    for part in ref[2:].split("/"):
        # JSON pointer unescaping (~1 -> /, ~0 -> ~)
        part = part.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or part not in node:
            raise SpecError(f"could not resolve $ref: {ref!r} (missing {part!r})")
        node = node[part]
    return node


def resolve_channel(doc, channel_obj):
    """Given an operation's ``channel`` object ({'$ref': ...}), return the
    channel node it points to."""
    if not isinstance(channel_obj, dict) or "$ref" not in channel_obj:
        raise SpecError(f"operation channel is not a $ref: {channel_obj!r}")
    return resolve_ref(doc, channel_obj["$ref"])


def channel_ref_id(channel_obj):
    """Return the channel key a ``{'$ref': '#/channels/<id>'}`` points to."""
    if not isinstance(channel_obj, dict) or "$ref" not in channel_obj:
        raise SpecError(f"channel is not a $ref: {channel_obj!r}")
    return channel_obj["$ref"].rsplit("/", 1)[-1]
