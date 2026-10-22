# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Load an AsyncAPI YAML spec and resolve the local $refs the client needs.

Only local refs (``#/...``) are supported; the EVerest API specs are entirely
self-contained. Anything unexpected raises so a malformed spec is reported
rather than producing a silently wrong client.
"""

import re
from pathlib import Path

import yaml


class SpecError(Exception):
    """Raised when a spec cannot be processed."""


_BOOL_TAG = "tag:yaml.org,2002:bool"


class _SpecLoader(yaml.SafeLoader):
    """SafeLoader with YAML 1.2 booleans: only ``true`` and ``false`` are
    booleans, so enum members such as ``Off`` or ``No`` stay strings."""
    yaml_implicit_resolvers = {
        first: [(tag, regexp) for tag, regexp in resolvers if tag != _BOOL_TAG]
        for first, resolvers in yaml.SafeLoader.yaml_implicit_resolvers.items()
    }


_SpecLoader.add_implicit_resolver(_BOOL_TAG, re.compile(r"^(?:true|True|TRUE|false|False|FALSE)$"),
                                  list("tTfF"))


def load_spec(path):
    """Parse the YAML spec at *path*. Mapping order is preserved (PyYAML), which
    the client relies on to create the commands in source order."""
    text = Path(path).read_text(encoding="utf-8")
    doc = yaml.load(text, Loader=_SpecLoader)
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
