# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Load AsyncAPI YAML specs and resolve the $refs the generators need.

Local refs (``#/...``) resolve within one spec. Refs into a sibling spec
(``other_API.yaml#/...``) resolve through a SpecSet, which loads the specs of
one directory on demand. Anything unexpected raises so a malformed spec fails
the build loudly rather than producing silently wrong output.
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
    the generators rely on to emit operations in source order."""
    text = Path(path).read_text(encoding="utf-8")
    doc = yaml.load(text, Loader=_SpecLoader)
    if not isinstance(doc, dict):
        raise SpecError(f"{path}: top-level YAML is not a mapping")
    return doc


def json_compatible(value):
    """Convert a parsed YAML value so json.dumps prints it as JavaScript would:
    integral floats such as 230.0 become 230. bool is checked first since it
    is a subclass of int."""
    if isinstance(value, bool):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, dict):
        return {k: json_compatible(v) for k, v in value.items()}
    if isinstance(value, list):
        return [json_compatible(v) for v in value]
    return value


def split_ref(ref):
    """Split a ``$ref`` into ``(file name or None, JSON pointer)``."""
    if not isinstance(ref, str) or "#/" not in ref:
        raise SpecError(f"unsupported $ref: {ref!r}")
    file_part, pointer = ref.split("#", 1)
    if file_part and ("/" in file_part or "\\" in file_part):
        raise SpecError(f"$ref must name a sibling spec file: {ref!r}")
    return (file_part or None), pointer


def resolve_ref(doc, ref):
    """Resolve a local JSON ``$ref`` (e.g. ``#/channels/foo``) against *doc*."""
    file_part, pointer = split_ref(ref)
    if file_part is not None:
        raise SpecError(f"external $ref needs a SpecSet: {ref!r}")
    node = doc
    for part in pointer[1:].split("/"):
        # JSON pointer unescaping (~1 -> /, ~0 -> ~)
        part = part.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or part not in node:
            raise SpecError(f"could not resolve $ref: {ref!r} (missing {part!r})")
        node = node[part]
    return node


class SpecSet:
    """The specs of one directory, loaded on first use, keyed by file name."""

    def __init__(self, directory):
        self._directory = Path(directory)
        self._docs = {}

    def get(self, file_name):
        if file_name not in self._docs:
            path = self._directory / file_name
            if not path.is_file():
                raise SpecError(f"referenced spec file not found: {path}")
            self._docs[file_name] = load_spec(path)
        return self._docs[file_name]

    def resolve(self, file_name, ref):
        """Resolve *ref* as written in spec *file_name*. Returns
        ``(file name holding the target, local pointer ref, target node)``."""
        target_file, pointer = split_ref(ref)
        target_file = target_file or file_name
        local_ref = "#" + pointer
        try:
            node = resolve_ref(self.get(target_file), local_ref)
        except SpecError as err:
            raise SpecError(f"{file_name}: {err} (as {ref!r})") from None
        return target_file, local_ref, node


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


def find_request_operation(operations, reply_channel_id):
    """Return ``(op_id, op)`` of the operation whose ``reply.channel`` points
    at *reply_channel_id*, or ``(None, None)``."""
    for op_id, op in operations.items():
        reply = op.get("reply")
        if not isinstance(reply, dict):
            continue
        reply_channel = reply.get("channel")
        if not isinstance(reply_channel, dict) or "$ref" not in reply_channel:
            continue
        if channel_ref_id(reply_channel) == reply_channel_id:
            return op_id, op
    return None, None
