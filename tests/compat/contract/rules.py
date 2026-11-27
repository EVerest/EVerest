# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Classify the differences between two contract snapshots.

Each rule mirrors an item of docs/source/project/releases/breaking-changes.rst. A difference in a
construct that no rule knows is reported as ``unclassified``: it is neither accepted nor called
breaking, and stays a failure until a rule covers the construct.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any, Iterable, Optional

BREAKING = "breaking"
NON_BREAKING = "non-breaking"
UNCLASSIFIED = "unclassified"

ADD = "add"
REMOVE = "remove"
EDIT = "edit"


class Direction(Enum):
    """Who produces a message. The AsyncAPI specs are written from the client's point of view, so
    an operation with action ``send`` carries a message from the client to EVerest."""

    TO_CLIENT = "to_client"
    FROM_CLIENT = "from_client"
    UNKNOWN = "unknown"


@dataclass(frozen=True)
class Finding:
    pointer: str
    kind: str
    classification: str
    message: str

    def __str__(self) -> str:
        return f"[{self.classification}] {self.kind} {self.pointer}: {self.message}"


# ---------------------------------------------------------------------------------------------------
# Helpers


def _norm_type(value: Any) -> Any:
    if isinstance(value, list):
        return sorted(str(v) for v in value)
    return value


LOWER_BOUND_KEYS = ("minimum", "exclusiveMinimum", "minLength", "minItems", "minLimit")
UPPER_BOUND_KEYS = ("maximum", "exclusiveMaximum", "maxLength", "maxItems", "maxLimit")


def _bound_findings(old: dict, new: dict, pointer: str, validates_input: bool = True) -> list[Finding]:
    """A bound that moves is classified by direction. A bound that appears where none existed
    rejects values that were accepted before, which breaks whoever provides the value:
    ``validates_input`` says that this is the deployment or a client, not EVerest."""
    findings = []
    for key in LOWER_BOUND_KEYS + UPPER_BOUND_KEYS:
        old_value, new_value = old.get(key), new.get(key)
        if old_value == new_value:
            continue
        if old_value is None:
            findings.append(_constraint_added(f"{pointer}/{key}", f"{key} {new_value!r}", validates_input))
            continue
        if new_value is None:
            findings.append(Finding(f"{pointer}/{key}", REMOVE, NON_BREAKING, f"{key} {old_value!r} removed"))
            continue
        relaxed = new_value < old_value if key in LOWER_BOUND_KEYS else new_value > old_value
        findings.append(
            Finding(
                f"{pointer}/{key}",
                EDIT,
                NON_BREAKING if relaxed else BREAKING,
                f"{key} {old_value!r} -> {new_value!r} ({'widened' if relaxed else 'narrowed'})",
            )
        )
    return findings


def _constraint_added(pointer: str, constraint: str, validates_input: bool) -> Finding:
    if validates_input:
        return Finding(pointer, ADD, BREAKING, f"{constraint} added where none existed, values outside it are rejected")
    return Finding(pointer, ADD, NON_BREAKING, f"{constraint} added where none existed, to a value EVerest provides")


def _enum_findings(old: dict, new: dict, pointer: str, key: str = "enum", validates_input: bool = True) -> list[Finding]:
    old_values, new_values = old.get(key), new.get(key)
    if old_values == new_values:
        return []
    if old_values is None:
        return [_constraint_added(f"{pointer}/{key}", f"{key} {new_values!r}", validates_input)]
    if new_values is None:
        # As for an added value: every value that was valid stays valid.
        return [Finding(f"{pointer}/{key}", REMOVE, NON_BREAKING, f"{key} constraint removed")]
    findings = []
    for value in old_values:
        if value not in new_values:
            findings.append(Finding(f"{pointer}/{key}", REMOVE, BREAKING, f"{key} value {value!r} removed"))
    for value in new_values:
        if value not in old_values:
            findings.append(Finding(f"{pointer}/{key}", ADD, NON_BREAKING, f"{key} value {value!r} added"))
    return findings


# ---------------------------------------------------------------------------------------------------
# JSON schema (message payloads and RPC types)

SCHEMA_DOC_KEYS = {"description", "title", "examples", "example", "$comment", "$schema", "deprecated"}
SCHEMA_HANDLED_KEYS = SCHEMA_DOC_KEYS | {
    "type", "enum", "required", "properties", "patternProperties", "items", "oneOf", "anyOf", "allOf",
    "additionalProperties", "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum",
    "minLength", "maxLength", "minItems", "maxItems", "format", "pattern", "const", "default",
}


def diff_schema(old: Any, new: Any, pointer: str, direction: Direction) -> list[Finding]:
    # No schema and the empty schema both accept every value.
    old, new = ({} if old is None else old), ({} if new is None else new)
    if old == new:
        return []
    if not isinstance(old, dict) or not isinstance(new, dict):
        return [Finding(pointer, EDIT, UNCLASSIFIED, f"schema changed: {old!r} -> {new!r}")]

    findings: list[Finding] = []
    client_sends = direction in (Direction.FROM_CLIENT, Direction.UNKNOWN)

    if _norm_type(old.get("type")) != _norm_type(new.get("type")):
        findings.append(Finding(f"{pointer}/type", EDIT, BREAKING, f"type {old.get('type')!r} -> {new.get('type')!r}"))

    findings += _enum_findings(old, new, pointer, validates_input=client_sends)
    findings += _bound_findings(old, new, pointer, validates_input=client_sends)

    if old.get("default") != new.get("default"):
        # The value EVerest assumes for a field the client leaves out. In a message EVerest sends
        # it only documents the field.
        findings.append(
            Finding(
                f"{pointer}/default", EDIT, BREAKING if client_sends else NON_BREAKING,
                f"default {old.get('default')!r} -> {new.get('default')!r}",
            )
        )

    for key in ("format", "pattern", "const"):
        old_value, new_value = old.get(key), new.get(key)
        if old_value == new_value:
            continue
        if old_value is None:
            findings.append(_constraint_added(f"{pointer}/{key}", f"{key} {new_value!r}", client_sends))
        elif new_value is None:
            findings.append(Finding(f"{pointer}/{key}", REMOVE, NON_BREAKING, f"{key} {old_value!r} removed"))
        else:
            findings.append(Finding(f"{pointer}/{key}", EDIT, BREAKING, f"{key} {old_value!r} -> {new_value!r}"))

    old_props, new_props = old.get("properties") or {}, new.get("properties") or {}
    old_required, new_required = set(old.get("required") or []), set(new.get("required") or [])

    for name in sorted(old_required - new_required):
        if name in new_props:
            findings.append(Finding(f"{pointer}/required/{name}", REMOVE, NON_BREAKING, "field became optional"))
    for name in sorted(new_required - old_required):
        if name in old_props:
            message = "existing field became required"
        else:
            message = "new field is required"
        findings.append(
            Finding(f"{pointer}/required/{name}", ADD, BREAKING if client_sends else NON_BREAKING, message)
        )

    for name in sorted(old_props.keys() - new_props.keys()):
        findings.append(Finding(f"{pointer}/properties/{name}", REMOVE, BREAKING, "field removed"))
    for name in sorted(new_props.keys() - old_props.keys()):
        if name in new_required - old_required:
            continue  # reported above
        findings.append(Finding(f"{pointer}/properties/{name}", ADD, NON_BREAKING, "optional field added"))
    for name in sorted(old_props.keys() & new_props.keys()):
        findings += diff_schema(old_props[name], new_props[name], f"{pointer}/properties/{name}", direction)

    # Fields whose names follow a pattern, such as one entry per module id.
    old_patterns, new_patterns = old.get("patternProperties") or {}, new.get("patternProperties") or {}
    for pattern in sorted(old_patterns.keys() - new_patterns.keys()):
        findings.append(Finding(f"{pointer}/patternProperties/{pattern}", REMOVE, BREAKING, "fields of this name pattern removed"))
    for pattern in sorted(new_patterns.keys() - old_patterns.keys()):
        findings.append(Finding(f"{pointer}/patternProperties/{pattern}", ADD, NON_BREAKING, "fields of this name pattern added"))
    for pattern in sorted(old_patterns.keys() & new_patterns.keys()):
        findings += diff_schema(
            old_patterns[pattern], new_patterns[pattern], f"{pointer}/patternProperties/{pattern}", direction
        )

    old_additional, new_additional = old.get("additionalProperties", True), new.get("additionalProperties", True)
    if old_additional != new_additional:
        if new_additional is False:
            findings.append(
                Finding(
                    f"{pointer}/additionalProperties", EDIT, BREAKING if client_sends else NON_BREAKING,
                    "additional properties no longer accepted",
                )
            )
        elif old_additional is False:
            findings.append(Finding(f"{pointer}/additionalProperties", EDIT, NON_BREAKING, "additional properties accepted"))
        else:
            findings += diff_schema(old_additional, new_additional, f"{pointer}/additionalProperties", direction)

    if "items" in old or "items" in new:
        old_items, new_items = old.get("items"), new.get("items")
        if isinstance(old_items, dict) and isinstance(new_items, dict):
            findings += diff_schema(old_items, new_items, f"{pointer}/items", direction)
        elif old_items != new_items:
            findings.append(Finding(f"{pointer}/items", EDIT, UNCLASSIFIED, "items schema changed"))

    for combinator in ("oneOf", "anyOf", "allOf"):
        old_list, new_list = old.get(combinator), new.get(combinator)
        if old_list == new_list:
            continue
        if isinstance(old_list, list) and isinstance(new_list, list) and len(old_list) == len(new_list):
            for index, (old_item, new_item) in enumerate(zip(old_list, new_list)):
                findings += diff_schema(old_item, new_item, f"{pointer}/{combinator}/{index}", direction)
        else:
            findings.append(Finding(f"{pointer}/{combinator}", EDIT, UNCLASSIFIED, f"{combinator} alternatives changed"))

    for key in sorted((old.keys() | new.keys()) - SCHEMA_HANDLED_KEYS):
        if old.get(key) != new.get(key):
            findings.append(Finding(f"{pointer}/{key}", EDIT, UNCLASSIFIED, f"{key} {old.get(key)!r} -> {new.get(key)!r}"))

    return findings


# ---------------------------------------------------------------------------------------------------
# AsyncAPI


def _channel_directions(spec: dict) -> dict[str, Direction]:
    directions: dict[str, Direction] = {}

    def record(channel: Optional[str], direction: Direction) -> None:
        if channel is None:
            return
        previous = directions.get(channel)
        if previous is not None and previous != direction:
            directions[channel] = Direction.UNKNOWN
        else:
            directions[channel] = direction

    for operation in (spec.get("operations") or {}).values():
        action = operation.get("action")
        if action == "send":
            direction, reply_direction = Direction.FROM_CLIENT, Direction.TO_CLIENT
        elif action == "receive":
            direction, reply_direction = Direction.TO_CLIENT, Direction.FROM_CLIENT
        else:
            continue
        record(operation.get("channel"), direction)
        record(operation.get("reply_channel"), reply_direction)
    return directions


def _diff_asyncapi_spec(name: str, old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    prefix = f"asyncapi/{name}"

    if old.get("version") != new.get("version"):
        findings.append(Finding(f"{prefix}/version", EDIT, NON_BREAKING, f"{old.get('version')} -> {new.get('version')}"))

    old_servers, new_servers = old.get("servers") or {}, new.get("servers") or {}
    for server in sorted(old_servers.keys() - new_servers.keys()):
        findings.append(Finding(f"{prefix}/servers/{server}", REMOVE, BREAKING, "server removed"))
    for server in sorted(new_servers.keys() - old_servers.keys()):
        findings.append(Finding(f"{prefix}/servers/{server}", ADD, NON_BREAKING, "server added"))
    for server in sorted(old_servers.keys() & new_servers.keys()):
        for key in ("host", "pathname", "protocol"):
            if old_servers[server].get(key) != new_servers[server].get(key):
                findings.append(
                    Finding(
                        f"{prefix}/servers/{server}/{key}", EDIT, BREAKING,
                        f"{key} {old_servers[server].get(key)!r} -> {new_servers[server].get(key)!r}",
                    )
                )

    directions = _channel_directions(new)
    for channel, direction in _channel_directions(old).items():
        directions.setdefault(channel, direction)

    old_channels, new_channels = old.get("channels") or {}, new.get("channels") or {}
    for channel in sorted(old_channels.keys() - new_channels.keys()):
        findings.append(Finding(f"{prefix}/channels/{channel}", REMOVE, BREAKING, "channel removed"))
    for channel in sorted(new_channels.keys() - old_channels.keys()):
        findings.append(Finding(f"{prefix}/channels/{channel}", ADD, NON_BREAKING, "channel added"))
    for channel in sorted(old_channels.keys() & new_channels.keys()):
        old_channel, new_channel = old_channels[channel], new_channels[channel]
        channel_pointer = f"{prefix}/channels/{channel}"
        if old_channel.get("address") != new_channel.get("address"):
            findings.append(
                Finding(
                    f"{channel_pointer}/address", EDIT, BREAKING,
                    f"address {old_channel.get('address')!r} -> {new_channel.get('address')!r}",
                )
            )
        old_messages, new_messages = old_channel.get("messages") or {}, new_channel.get("messages") or {}
        for message in sorted(old_messages.keys() - new_messages.keys()):
            findings.append(Finding(f"{channel_pointer}/messages/{message}", REMOVE, BREAKING, "message removed"))
        for message in sorted(new_messages.keys() - old_messages.keys()):
            findings.append(Finding(f"{channel_pointer}/messages/{message}", ADD, NON_BREAKING, "message added"))
        for message in sorted(old_messages.keys() & new_messages.keys()):
            findings += diff_schema(
                old_messages[message].get("payload"),
                new_messages[message].get("payload"),
                f"{channel_pointer}/messages/{message}/payload",
                directions.get(channel, Direction.UNKNOWN),
            )

    old_operations, new_operations = old.get("operations") or {}, new.get("operations") or {}
    for operation in sorted(old_operations.keys() - new_operations.keys()):
        findings.append(Finding(f"{prefix}/operations/{operation}", REMOVE, BREAKING, "operation removed"))
    for operation in sorted(new_operations.keys() - old_operations.keys()):
        findings.append(Finding(f"{prefix}/operations/{operation}", ADD, NON_BREAKING, "operation added"))
    for operation in sorted(old_operations.keys() & new_operations.keys()):
        old_operation, new_operation = old_operations[operation], new_operations[operation]
        operation_pointer = f"{prefix}/operations/{operation}"
        for key in ("action", "channel"):
            if old_operation.get(key) != new_operation.get(key):
                findings.append(
                    Finding(
                        f"{operation_pointer}/{key}", EDIT, BREAKING,
                        f"{key} {old_operation.get(key)!r} -> {new_operation.get(key)!r}",
                    )
                )
        old_reply, new_reply = old_operation.get("reply_channel"), new_operation.get("reply_channel")
        if old_reply != new_reply:
            if old_reply is None:
                findings.append(Finding(f"{operation_pointer}/reply", ADD, NON_BREAKING, f"reply channel {new_reply!r} added"))
            elif new_reply is None:
                findings.append(Finding(f"{operation_pointer}/reply", REMOVE, BREAKING, f"reply channel {old_reply!r} removed"))
            else:
                findings.append(Finding(f"{operation_pointer}/reply", EDIT, BREAKING, f"reply channel {old_reply!r} -> {new_reply!r}"))

    return findings


def diff_asyncapi(old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    for name in sorted(old.keys() - new.keys()):
        findings.append(Finding(f"asyncapi/{name}", REMOVE, BREAKING, "AsyncAPI specification removed"))
    for name in sorted(new.keys() - old.keys()):
        findings.append(Finding(f"asyncapi/{name}", ADD, NON_BREAKING, "AsyncAPI specification added"))
    for name in sorted(old.keys() & new.keys()):
        findings += _diff_asyncapi_spec(name, old[name], new[name])
    return findings


# ---------------------------------------------------------------------------------------------------
# Configuration options (module manifests and OCPP 1.6 profile schemas)

CONFIG_HANDLED_KEYS = {
    "type", "default", "required", "enum", "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum",
    "minLength", "maxLength", "minItems", "maxItems", "mutability", "readOnly",
}


def diff_config_option(old: dict, new: dict, pointer: str) -> list[Finding]:
    findings: list[Finding] = []
    if _norm_type(old.get("type")) != _norm_type(new.get("type")):
        findings.append(Finding(f"{pointer}/type", EDIT, BREAKING, f"type {old.get('type')!r} -> {new.get('type')!r}"))

    old_has_default, new_has_default = "default" in old, "default" in new
    if old_has_default and not new_has_default:
        findings.append(Finding(f"{pointer}/default", REMOVE, BREAKING, f"default {old['default']!r} removed, option is now required"))
    elif new_has_default and not old_has_default:
        findings.append(Finding(f"{pointer}/default", ADD, NON_BREAKING, f"default {new['default']!r} added, option is now optional"))
    elif old.get("default") != new.get("default"):
        findings.append(Finding(f"{pointer}/default", EDIT, BREAKING, f"default {old['default']!r} -> {new['default']!r}"))

    # A manifest option is required when it has no default, which is reported above. An OCPP 1.6
    # profile schema lists its required keys.
    old_required, new_required = bool(old.get("required")), bool(new.get("required"))
    if old_required != new_required and old_has_default == new_has_default:
        findings.append(
            Finding(
                f"{pointer}/required", EDIT, BREAKING if new_required else NON_BREAKING,
                "option became required" if new_required else "option became optional",
            )
        )

    findings += _enum_findings(old, new, pointer)
    findings += _bound_findings(old, new, pointer)

    # ReadOnly is the manifest schema default (lib/everest/framework/schemas/manifest.yaml)
    old_mutability, new_mutability = old.get("mutability", "ReadOnly"), new.get("mutability", "ReadOnly")
    if old_mutability != new_mutability:
        narrowed = old_mutability == "ReadWrite"
        findings.append(
            Finding(f"{pointer}/mutability", EDIT, BREAKING if narrowed else NON_BREAKING, f"mutability {old_mutability} -> {new_mutability}")
        )
    old_read_only, new_read_only = bool(old.get("readOnly")), bool(new.get("readOnly"))
    if old_read_only != new_read_only:
        findings.append(
            Finding(
                f"{pointer}/readOnly", EDIT, BREAKING if new_read_only else NON_BREAKING,
                f"readOnly {old.get('readOnly')!r} -> {new.get('readOnly')!r}",
            )
        )

    for key in sorted((old.keys() | new.keys()) - CONFIG_HANDLED_KEYS):
        if old.get(key) != new.get(key):
            findings.append(Finding(f"{pointer}/{key}", EDIT, UNCLASSIFIED, f"{key} {old.get(key)!r} -> {new.get(key)!r}"))
    return findings


def diff_config_set(old: dict, new: dict, pointer: str) -> list[Finding]:
    findings: list[Finding] = []
    for name in sorted(old.keys() - new.keys()):
        findings.append(Finding(f"{pointer}/{name}", REMOVE, BREAKING, "option removed"))
    for name in sorted(new.keys() - old.keys()):
        if new[name].get("required"):
            findings.append(Finding(f"{pointer}/{name}", ADD, BREAKING, "option added without a default"))
        elif "default" in new[name]:
            findings.append(Finding(f"{pointer}/{name}", ADD, NON_BREAKING, f"option added with default {new[name]['default']!r}"))
        else:
            findings.append(Finding(f"{pointer}/{name}", ADD, NON_BREAKING, "optional option added"))
    for name in sorted(old.keys() & new.keys()):
        findings += diff_config_option(old[name], new[name], f"{pointer}/{name}")
    return findings


# ---------------------------------------------------------------------------------------------------
# Modules


def _diff_module(name: str, old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    prefix = f"modules/{name}"
    findings += diff_config_set(old.get("config") or {}, new.get("config") or {}, f"{prefix}/config")

    old_provides, new_provides = old.get("provides") or {}, new.get("provides") or {}
    for impl in sorted(old_provides.keys() - new_provides.keys()):
        findings.append(Finding(f"{prefix}/provides/{impl}", REMOVE, BREAKING, "implementation removed, connections reference it by id"))
    for impl in sorted(new_provides.keys() - old_provides.keys()):
        findings.append(Finding(f"{prefix}/provides/{impl}", ADD, NON_BREAKING, "implementation added"))
    for impl in sorted(old_provides.keys() & new_provides.keys()):
        old_impl, new_impl = old_provides[impl], new_provides[impl]
        impl_pointer = f"{prefix}/provides/{impl}"
        if old_impl.get("interface") != new_impl.get("interface"):
            findings.append(
                Finding(
                    f"{impl_pointer}/interface", EDIT, BREAKING,
                    f"interface {old_impl.get('interface')!r} -> {new_impl.get('interface')!r}, "
                    "connections to requirements of the previous interface no longer match",
                )
            )
        findings += diff_config_set(old_impl.get("config") or {}, new_impl.get("config") or {}, f"{impl_pointer}/config")
        if old_impl.get("properties") != new_impl.get("properties"):
            findings.append(
                Finding(
                    f"{impl_pointer}/properties", EDIT, BREAKING,
                    "requirement-matching properties changed, existing connections may no longer match",
                )
            )

    old_requires, new_requires = old.get("requires") or {}, new.get("requires") or {}
    for requirement in sorted(old_requires.keys() - new_requires.keys()):
        findings.append(Finding(f"{prefix}/requires/{requirement}", REMOVE, BREAKING, "requirement removed, connections reference it by id"))
    for requirement in sorted(new_requires.keys() - old_requires.keys()):
        if new_requires[requirement].get("min_connections", 1) > 0:
            findings.append(Finding(f"{prefix}/requires/{requirement}", ADD, BREAKING, "mandatory requirement added, existing connections lack it"))
        else:
            findings.append(Finding(f"{prefix}/requires/{requirement}", ADD, NON_BREAKING, "optional requirement added"))
    for requirement in sorted(old_requires.keys() & new_requires.keys()):
        old_req, new_req = old_requires[requirement], new_requires[requirement]
        req_pointer = f"{prefix}/requires/{requirement}"
        if old_req.get("interface") != new_req.get("interface"):
            findings.append(
                Finding(
                    f"{req_pointer}/interface", EDIT, BREAKING,
                    f"interface {old_req.get('interface')!r} -> {new_req.get('interface')!r}, "
                    "connections to implementations of the previous interface no longer match",
                )
            )
        old_min, new_min = old_req.get("min_connections", 1), new_req.get("min_connections", 1)
        if new_min > old_min:
            findings.append(Finding(f"{req_pointer}/min_connections", EDIT, BREAKING, f"{old_min} -> {new_min}"))
        elif new_min < old_min:
            findings.append(Finding(f"{req_pointer}/min_connections", EDIT, NON_BREAKING, f"{old_min} -> {new_min}"))
        old_max, new_max = old_req.get("max_connections", 1), new_req.get("max_connections", 1)
        if new_max < old_max:
            findings.append(Finding(f"{req_pointer}/max_connections", EDIT, BREAKING, f"{old_max} -> {new_max}"))
        elif new_max > old_max:
            findings.append(Finding(f"{req_pointer}/max_connections", EDIT, NON_BREAKING, f"{old_max} -> {new_max}"))
    return findings


def diff_modules(old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    for name in sorted(old.keys() - new.keys()):
        findings.append(Finding(f"modules/{name}", REMOVE, BREAKING, "module removed, configurations reference it by name"))
    for name in sorted(new.keys() - old.keys()):
        findings.append(Finding(f"modules/{name}", ADD, NON_BREAKING, "module added"))
    for name in sorted(old.keys() & new.keys()):
        findings += _diff_module(name, old[name], new[name])
    return findings


# ---------------------------------------------------------------------------------------------------
# OCPP configuration

MUTABILITY_RANK = {"WriteOnly": 0, "ReadOnly": 0, "ReadWrite": 1}


def _values_list(characteristics: dict) -> dict:
    """``valuesList`` is a comma separated string."""
    values = characteristics.get("valuesList")
    return {"valuesList": [value.strip() for value in values.split(",")]} if isinstance(values, str) else characteristics


def _diff_component_variable(old: dict, new: dict, pointer: str) -> list[Finding]:
    findings: list[Finding] = []
    old_char, new_char = old.get("characteristics") or {}, new.get("characteristics") or {}
    if old_char.get("dataType") != new_char.get("dataType"):
        findings.append(Finding(f"{pointer}/dataType", EDIT, BREAKING, f"{old_char.get('dataType')!r} -> {new_char.get('dataType')!r}"))
    if old_char.get("unit") != new_char.get("unit"):
        findings.append(Finding(f"{pointer}/unit", EDIT, BREAKING, f"{old_char.get('unit')!r} -> {new_char.get('unit')!r}"))
    old_monitoring, new_monitoring = old_char.get("supportsMonitoring"), new_char.get("supportsMonitoring")
    if old_monitoring != new_monitoring:
        classification = BREAKING if old_monitoring and not new_monitoring else NON_BREAKING
        findings.append(Finding(f"{pointer}/supportsMonitoring", EDIT, classification, f"{old_monitoring!r} -> {new_monitoring!r}"))
    findings += _bound_findings(old_char, new_char, pointer)
    findings += _enum_findings(_values_list(old_char), _values_list(new_char), pointer, key="valuesList")

    if old.get("default") != new.get("default"):
        findings.append(Finding(f"{pointer}/default", EDIT, BREAKING, f"default {old.get('default')!r} -> {new.get('default')!r}"))

    old_attributes, new_attributes = old.get("attributes") or {}, new.get("attributes") or {}
    for attribute in sorted(old_attributes.keys() - new_attributes.keys()):
        findings.append(Finding(f"{pointer}/attributes/{attribute}", REMOVE, BREAKING, "attribute removed"))
    for attribute in sorted(new_attributes.keys() - old_attributes.keys()):
        findings.append(Finding(f"{pointer}/attributes/{attribute}", ADD, NON_BREAKING, "attribute added"))
    for attribute in sorted(old_attributes.keys() & new_attributes.keys()):
        old_attr, new_attr = old_attributes[attribute], new_attributes[attribute]
        attr_pointer = f"{pointer}/attributes/{attribute}"
        old_mut, new_mut = old_attr.get("mutability", "ReadWrite"), new_attr.get("mutability", "ReadWrite")
        if old_mut != new_mut:
            narrowed = MUTABILITY_RANK.get(new_mut, 0) < MUTABILITY_RANK.get(old_mut, 0)
            findings.append(Finding(f"{attr_pointer}/mutability", EDIT, BREAKING if narrowed else NON_BREAKING, f"{old_mut} -> {new_mut}"))
        if old_attr.get("value") != new_attr.get("value"):
            findings.append(Finding(f"{attr_pointer}/value", EDIT, BREAKING, f"value {old_attr.get('value')!r} -> {new_attr.get('value')!r}"))

    old_required, new_required = bool(old.get("required")), bool(new.get("required"))
    if old_required != new_required:
        findings.append(
            Finding(
                f"{pointer}/required", EDIT, BREAKING if new_required else NON_BREAKING,
                f"required {old.get('required')!r} -> {new.get('required')!r}",
            )
        )
    return findings


def _diff_component_config(old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    prefix = "ocpp/component_config"
    for component in sorted(old.keys() - new.keys()):
        findings.append(Finding(f"{prefix}/{component}", REMOVE, BREAKING, "component config removed"))
    for component in sorted(new.keys() - old.keys()):
        findings.append(Finding(f"{prefix}/{component}", ADD, NON_BREAKING, "component config added"))
    for component in sorted(old.keys() & new.keys()):
        old_vars, new_vars = old[component].get("variables") or {}, new[component].get("variables") or {}
        for variable in sorted(old_vars.keys() - new_vars.keys()):
            findings.append(Finding(f"{prefix}/{component}/{variable}", REMOVE, BREAKING, "variable removed"))
        for variable in sorted(new_vars.keys() - old_vars.keys()):
            findings.append(Finding(f"{prefix}/{component}/{variable}", ADD, NON_BREAKING, "variable added"))
        for variable in sorted(old_vars.keys() & new_vars.keys()):
            findings += _diff_component_variable(old_vars[variable], new_vars[variable], f"{prefix}/{component}/{variable}")
    return findings


def _diff_mapping(old: Any, new: Any, pointer: str) -> list[Finding]:
    if old == new:
        return []
    if old is None:
        return [Finding(pointer, ADD, NON_BREAKING, "added")]
    if new is None:
        return [Finding(pointer, REMOVE, BREAKING, "removed")]
    if isinstance(old, dict) and isinstance(new, dict):
        findings: list[Finding] = []
        for key in sorted(old.keys() - new.keys()):
            findings.append(Finding(f"{pointer}/{key}", REMOVE, BREAKING, "entry removed"))
        for key in sorted(new.keys() - old.keys()):
            findings.append(Finding(f"{pointer}/{key}", ADD, NON_BREAKING, "entry added"))
        for key in sorted(old.keys() & new.keys()):
            findings += _diff_mapping(old[key], new[key], f"{pointer}/{key}")
        return findings
    return [Finding(pointer, EDIT, UNCLASSIFIED, f"{old!r} -> {new!r}")]


def diff_ocpp(old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    old_profiles, new_profiles = old.get("v16_profile_schemas") or {}, new.get("v16_profile_schemas") or {}
    for profile in sorted(old_profiles.keys() - new_profiles.keys()):
        findings.append(Finding(f"ocpp/v16/{profile}", REMOVE, BREAKING, "profile schema removed"))
    for profile in sorted(new_profiles.keys() - old_profiles.keys()):
        findings.append(Finding(f"ocpp/v16/{profile}", ADD, NON_BREAKING, "profile schema added"))
    for profile in sorted(old_profiles.keys() & new_profiles.keys()):
        findings += diff_config_set(old_profiles[profile], new_profiles[profile], f"ocpp/v16/{profile}")

    findings += _diff_component_config(old.get("component_config") or {}, new.get("component_config") or {})
    findings += _diff_mapping(
        old.get("v16_custom_key_mappings"), new.get("v16_custom_key_mappings"), "ocpp/v16_custom_key_mappings"
    )
    return findings


# ---------------------------------------------------------------------------------------------------
# Migrations


def diff_migrations(old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    for name in sorted(old.keys() - new.keys()):
        findings.append(Finding(f"migrations/{name}", REMOVE, BREAKING, "migration file removed"))
    for name in sorted(new.keys() - old.keys()):
        findings.append(Finding(f"migrations/{name}", ADD, NON_BREAKING, "migration file added"))
    for name in sorted(old.keys() & new.keys()):
        if old[name] != new[name]:
            findings.append(Finding(f"migrations/{name}", EDIT, BREAKING, "migration file rewritten, deployed databases already applied it"))
    return findings


# ---------------------------------------------------------------------------------------------------
# JSON RPC API


def diff_rpc_api(old: Optional[dict], new: Optional[dict]) -> list[Finding]:
    if old is None and new is None:
        return []
    if old is None:
        return [Finding("rpc_api", ADD, NON_BREAKING, "JSON RPC API added")]
    if new is None:
        return [Finding("rpc_api", REMOVE, BREAKING, "JSON RPC API removed")]
    findings: list[Finding] = []
    if old.get("version") != new.get("version"):
        findings.append(Finding("rpc_api/version", EDIT, NON_BREAKING, f"{old.get('version')} -> {new.get('version')}"))
    old_types, new_types = old.get("types") or {}, new.get("types") or {}
    for name in sorted(old_types.keys() - new_types.keys()):
        findings.append(Finding(f"rpc_api/types/{name}", REMOVE, BREAKING, "type removed"))
    for name in sorted(new_types.keys() - old_types.keys()):
        findings.append(Finding(f"rpc_api/types/{name}", ADD, NON_BREAKING, "type added"))
    for name in sorted(old_types.keys() & new_types.keys()):
        findings += diff_schema(old_types[name], new_types[name], f"rpc_api/types/{name}", Direction.UNKNOWN)
    return findings


# ---------------------------------------------------------------------------------------------------
# Manager: configuration file, settings file and command line


def diff_framework(old: dict, new: dict) -> list[Finding]:
    """The schemas of the files a deployment writes for the manager. A baseline without a schema
    made no promise about that file."""
    findings: list[Finding] = []
    for name in sorted(old.keys() - new.keys()):
        findings.append(Finding(f"framework/{name}", REMOVE, BREAKING, "schema removed"))
    for name in sorted(old.keys() & new.keys()):
        findings += diff_schema(old[name], new[name], f"framework/{name}", Direction.FROM_CLIENT)
    return findings


def diff_manager(old: Optional[dict], new: Optional[dict]) -> list[Finding]:
    """Command line options of the manager, which start scripts and service units pass."""
    if old is None:
        return []
    if new is None:
        return [Finding("manager/options", REMOVE, BREAKING, "the declarations of the manager's options were not found")]
    findings: list[Finding] = []
    old_options, new_options = old.get("options") or {}, new.get("options") or {}
    for name in sorted(old_options.keys() - new_options.keys()):
        findings.append(Finding(f"manager/options/{name}", REMOVE, BREAKING, f"option --{name} removed"))
    for name in sorted(new_options.keys() - old_options.keys()):
        findings.append(Finding(f"manager/options/{name}", ADD, NON_BREAKING, f"option --{name} added"))
    for name in sorted(old_options.keys() & new_options.keys()):
        old_option, new_option = old_options[name], new_options[name]
        if old_option.get("takes_value") != new_option.get("takes_value"):
            findings.append(
                Finding(
                    f"manager/options/{name}/takes_value", EDIT, BREAKING,
                    f"--{name} {'takes' if new_option.get('takes_value') else 'no longer takes'} a value",
                )
            )
        if old_option.get("alias") and old_option.get("alias") != new_option.get("alias"):
            findings.append(Finding(f"manager/options/{name}/alias", EDIT, BREAKING, f"alias -{old_option['alias']} removed"))
    return findings


# ---------------------------------------------------------------------------------------------------


def diff_snapshots(old: dict, new: dict) -> list[Finding]:
    findings: list[Finding] = []
    findings += diff_asyncapi(old.get("asyncapi") or {}, new.get("asyncapi") or {})
    findings += diff_modules(old.get("modules") or {}, new.get("modules") or {})
    findings += diff_ocpp(old.get("ocpp") or {}, new.get("ocpp") or {})
    findings += diff_migrations(old.get("migrations") or {}, new.get("migrations") or {})
    findings += diff_rpc_api(old.get("rpc_api"), new.get("rpc_api"))
    findings += diff_framework(old.get("framework") or {}, new.get("framework") or {})
    findings += diff_manager(old.get("manager"), new.get("manager"))
    return findings


def _same_or_nested(pointer: str, other: str) -> bool:
    return pointer == other or pointer.startswith(other + "/") or other.startswith(pointer + "/")


def split_unreleased(findings: Iterable[Finding], since_release: Iterable[Finding]) -> tuple[list[Finding], list[Finding]]:
    """Split findings into those that change what a release ships and those that do not.

    ``since_release`` are the findings of the same tree against that release. A finding concerns
    the release when the release differs from the tree at the same place, other than by a
    compatible addition: what was added after the release is in no deployment and may change
    freely until a release ships it. An addition that breaks, such as an option without a default
    in a released module, does concern the release."""
    released_places = [
        finding.pointer for finding in since_release if not (finding.kind == ADD and finding.classification == NON_BREAKING)
    ]
    released: list[Finding] = []
    unreleased: list[Finding] = []
    for finding in findings:
        concerns_release = finding.classification == NON_BREAKING or any(
            _same_or_nested(finding.pointer, place) for place in released_places
        )
        (released if concerns_release else unreleased).append(finding)
    return released, unreleased


def by_classification(findings: Iterable[Finding]) -> dict[str, list[Finding]]:
    groups: dict[str, list[Finding]] = {BREAKING: [], UNCLASSIFIED: [], NON_BREAKING: []}
    for finding in findings:
        groups.setdefault(finding.classification, []).append(finding)
    return groups
