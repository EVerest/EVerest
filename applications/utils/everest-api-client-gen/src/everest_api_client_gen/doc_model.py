# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Turn a parsed AsyncAPI spec into the model the HTML reference renders.

Unlike model.py (clients), this keeps everything a reader needs: info,
servers, every operation with its texts and request/reply pairing, every
message with payload schema and examples, and all component schemas.
Schemas are resolved into a SchemaNode tree; $refs into sibling specs are
followed through a SpecSet and keep a link to the page that defines them.
"""

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, List, Optional

from everest_api_client_gen.spec_loader import (
    SpecError, SpecSet, channel_ref_id, find_request_operation, json_compatible, resolve_channel,
    resolve_ref)

# Keywords shown as constraint chips, in display order.
_CONSTRAINTS = [
    ("format", "format"),
    ("minimum", "min"),
    ("exclusiveMinimum", "min (exclusive)"),
    ("maximum", "max"),
    ("exclusiveMaximum", "max (exclusive)"),
    ("multipleOf", "multiple of"),
    ("minLength", "min length"),
    ("maxLength", "max length"),
    ("pattern", "pattern"),
    ("minItems", "min items"),
    ("maxItems", "max items"),
    ("uniqueItems", "unique items"),
    ("minProperties", "min properties"),
    ("maxProperties", "max properties"),
    ("const", "const"),
    ("default", "default"),
]

# Recursion bound for schema trees, $ref chains and generated examples; the
# specs nest far less deeply, so hitting it means a cycle.
_MAX_DEPTH = 24

_SAMPLE_FORMATS = {
    "date-time": "2019-08-24T14:15:22Z",
    "date": "2019-08-24",
    "time": "14:15:22Z",
    "uri": "https://example.com",
    "email": "user@example.com",
}


@dataclass
class SchemaRef:
    file_name: str
    name: str
    # where the schema is defined: this page or the page of another API
    href: str
    external: bool
    # id of the rendering on this page that tree nodes copy their structure from
    anchor: str


@dataclass
class SchemaNode:
    name: Optional[str]
    type: str
    required: bool = False
    description: str = ""
    ref: Optional[SchemaRef] = None
    enum: List[Any] = field(default_factory=list)
    constraints: List[tuple] = field(default_factory=list)
    # "allowed", "forbidden", or None when unspecified
    additional_properties: Optional[str] = None
    children: List["SchemaNode"] = field(default_factory=list)
    # how the children relate: "properties", "items", "allOf", "oneOf", "anyOf"
    children_kind: Optional[str] = None
    # The referenced schema's structure is rendered once on the page (at
    # ref.anchor) and pulled in on demand instead of repeated inline.
    deferred: bool = False

    @property
    def expandable(self):
        return bool(self.children or self.enum or self.constraints or self.description
                    or self.additional_properties)


@dataclass
class Example:
    title: str
    payload_json: str
    generated: bool = False


@dataclass
class Message:
    msg_id: str
    name: str
    title: str
    summary: str
    description: str
    content_type: str
    payload: Optional[SchemaNode]
    examples: List[Example]
    used_by: List[str] = field(default_factory=list)


@dataclass
class Operation:
    op_id: str
    action: str
    title: str
    summary: str
    description: str
    address: Optional[str]
    topic: Optional[str]
    message_ids: List[str]
    # request side: where the reply goes and which operation carries it
    reply_location: Optional[str] = None
    reply_op_id: Optional[str] = None
    # reply side: the operation this one answers
    request_op_id: Optional[str] = None


@dataclass
class Server:
    name: str
    host: str
    protocol: str
    pathname: str
    description: str
    variables: List[tuple]


@dataclass
class DocModel:
    name: str
    spec_id: str
    asyncapi_version: str
    title: str
    version: str
    description: str
    license_name: str
    license_url: str
    tags: List[str]
    default_content_type: str
    servers: List[Server]
    operations: List[Operation]
    messages: List[Message]
    schemas: List[SchemaNode]
    # schemas of other APIs referenced from this one, each a (SchemaRef, SchemaNode)
    imported_schemas: List[tuple] = field(default_factory=list)

    @property
    def send_operations(self):
        return [o for o in self.operations if o.action == "send"]

    @property
    def receive_operations(self):
        return [o for o in self.operations if o.action == "receive"]

    def message(self, msg_id):
        return next(m for m in self.messages if m.msg_id == msg_id)


def page_name(file_name):
    """Directory name of a spec's page: the file name without .yaml."""
    return Path(file_name).stem


def schema_anchor(name):
    return f"schema-{name}"


def imported_schema_anchor(file_name, name):
    return f"ext-{page_name(file_name)}-{name}"


def _schema_ref(page_file, target_file, name):
    if target_file == page_file:
        anchor = schema_anchor(name)
        return SchemaRef(target_file, name, f"#{anchor}", False, anchor)
    return SchemaRef(target_file, name,
                     f"../{page_name(target_file)}/index.html#{schema_anchor(name)}",
                     True, imported_schema_anchor(target_file, name))


def _text(value):
    return value.strip() if isinstance(value, str) else ""


def _pretty_json(value):
    return json.dumps(json_compatible(value), indent=2, ensure_ascii=False, default=str)


class _SchemaBuilder:
    def __init__(self, specs, file_name):
        self._specs = specs
        self._file_name = file_name
        # (file, schema name) of other APIs' schemas the page refers to, in
        # order of first use
        self.imports = []

    def node(self, schema, name=None, required=False, file_name=None, defer_root=False):
        """Build the tree of *schema*. With *defer_root*, a root that is a
        named schema is deferred like any nested reference."""
        return self._node(schema, file_name or self._file_name, name, required, 0, defer_root)

    def _deref(self, schema, file_name):
        """Follow a $ref chain. Returns (merged schema, file it lives in,
        SchemaRef of the first named target or None)."""
        ref = None
        for _ in range(_MAX_DEPTH):
            siblings = {k: v for k, v in schema.items() if k != "$ref"}
            target_file, local_ref, target = self._specs.resolve(file_name, schema["$ref"])
            if not isinstance(target, dict):
                raise SpecError(f"{file_name}: $ref {schema['$ref']!r} is not a schema")
            parts = local_ref.split("/")
            if ref is None and len(parts) == 4 and parts[1:3] == ["components", "schemas"]:
                ref = _schema_ref(self._file_name, target_file, parts[3])
            if "type" in siblings and "type" in target and siblings["type"] != target["type"]:
                raise SpecError(f"{file_name}: $ref {schema['$ref']!r} is declared as "
                                f"{siblings['type']!r} but the target is {target['type']!r}")
            # sibling keywords (description, title) document the use site
            schema = {**target, **siblings}
            file_name = target_file
            if "$ref" not in schema:
                return schema, file_name, ref
        raise SpecError(f"{file_name}: $ref chain too long at {schema['$ref']!r}")

    def _node(self, schema, file_name, name, required, depth, defer_root=False):
        if not isinstance(schema, dict):
            raise SpecError(f"{file_name}: schema for {name!r} is not a mapping")
        if depth > _MAX_DEPTH:
            raise SpecError(f"{file_name}: schema nesting too deep at {name!r}")
        ref = None
        if "$ref" in schema:
            schema, file_name, ref = self._deref(schema, file_name)

        node = SchemaNode(name=name, type=_type_label(schema), required=required,
                          description=_text(schema.get("description")), ref=ref)
        node.enum = list(schema.get("enum", []))
        node.constraints = [(label, _constraint_value(schema[key]))
                            for key, label in _CONSTRAINTS if key in schema]
        if ((depth > 0 or defer_root) and ref is not None
                and any(k in schema for k in ("properties", "items", "allOf", "oneOf", "anyOf"))):
            node.deferred = True
            if ref.external and (ref.file_name, ref.name) not in self.imports:
                self.imports.append((ref.file_name, ref.name))
            return node

        add = schema.get("additionalProperties")
        if add is True:
            node.additional_properties = "allowed"
        elif add is False:
            node.additional_properties = "forbidden"

        if isinstance(schema.get("properties"), dict):
            req = set(schema.get("required", []))
            node.children = [self._node(sub, file_name, prop, prop in req, depth + 1)
                             for prop, sub in schema["properties"].items()]
            node.children_kind = "properties"
        elif isinstance(schema.get("items"), dict):
            node.children = [self._node(schema["items"], file_name, "items", False, depth + 1)]
            node.children_kind = "items"
        for combinator in ("allOf", "oneOf", "anyOf"):
            if isinstance(schema.get(combinator), list) and not node.children:
                node.children = [self._node(sub, file_name, f"#{i + 1}", False, depth + 1)
                                 for i, sub in enumerate(schema[combinator])]
                node.children_kind = combinator
        return node


def _type_label(schema):
    t = schema.get("type")
    if isinstance(t, list):
        label = " | ".join(str(x) for x in t)
    elif t:
        label = str(t)
    elif "properties" in schema:
        label = "object"
    elif "enum" in schema:
        label = "enum"
    elif any(k in schema for k in ("allOf", "oneOf", "anyOf")):
        label = next(k for k in ("allOf", "oneOf", "anyOf") if k in schema)
    else:
        label = "any"
    if label == "array" and isinstance(schema.get("items"), dict):
        item_type = schema["items"].get("type")
        if isinstance(item_type, str):
            label = f"array<{item_type}>"
    return label


def _constraint_value(value):
    if isinstance(value, bool):
        return "yes" if value else "no"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


class _SampleBuilder:
    """Build an illustrative payload from a schema for messages without examples."""

    def __init__(self, specs, file_name):
        self._specs = specs
        self._file_name = file_name

    def sample(self, schema):
        return self._sample(schema, self._file_name, 0)

    def _sample(self, schema, file_name, depth):
        if not isinstance(schema, dict) or depth > _MAX_DEPTH:
            return None
        while "$ref" in schema:
            target_file, _, target = self._specs.resolve(file_name, schema["$ref"])
            depth += 1
            if depth > _MAX_DEPTH:
                return None
            siblings = {k: v for k, v in schema.items() if k not in ("$ref", "description")}
            schema = dict(target)
            schema.update(siblings)
            file_name = target_file
        for key in ("const", "default"):
            if key in schema:
                return schema[key]
        if schema.get("examples"):
            return schema["examples"][0]
        if schema.get("enum"):
            return schema["enum"][0]
        for combinator in ("allOf", "oneOf", "anyOf"):
            if isinstance(schema.get(combinator), list):
                parts = [self._sample(s, file_name, depth + 1) for s in schema[combinator]]
                if combinator == "allOf" and all(isinstance(p, dict) for p in parts):
                    merged = {}
                    for part in parts:
                        merged.update(part)
                    return merged
                return parts[0] if parts else None
        t = schema.get("type")
        if isinstance(t, list):
            t = next((x for x in t if x != "null"), None)
        if t == "object" or "properties" in schema:
            # a minimal valid payload: the required properties only, unless
            # the schema marks none as required
            props = schema.get("properties", {})
            if "required" in schema:
                props = {k: v for k, v in props.items() if k in schema["required"]}
            return {prop: self._sample(sub, file_name, depth + 1)
                    for prop, sub in props.items()}
        if t == "array":
            item = self._sample(schema.get("items", {}), file_name, depth + 1)
            return [] if item is None else [item]
        if t == "string":
            return _SAMPLE_FORMATS.get(schema.get("format"), "string")
        if t == "integer":
            return schema.get("minimum", 0)
        if t == "number":
            return schema.get("minimum", 0.0)
        if t == "boolean":
            return True
        return None


def _message(doc, specs, file_name, schemas, msg_id, msg):
    # an empty schema ({}) documents a message without payload
    payload = msg.get("payload") or None
    examples = []
    for i, ex in enumerate(msg.get("examples") or []):
        if not isinstance(ex, dict) or "payload" not in ex:
            raise SpecError(f"message '{msg_id}' example {i + 1} has no payload")
        title = _text(ex.get("summary")) or _text(ex.get("name")) or f"Example {i + 1}"
        examples.append(Example(title, _pretty_json(ex["payload"])))
    if not examples and payload is not None:
        sample = _SampleBuilder(specs, file_name).sample(payload)
        examples.append(Example("Minimal payload generated from the schema",
                                _pretty_json(sample), True))
    return Message(
        msg_id=msg_id,
        name=_text(msg.get("name")) or msg_id,
        title=_text(msg.get("title")),
        summary=_text(msg.get("summary")),
        description=_text(msg.get("description")),
        content_type=_text(msg.get("contentType")) or _text(doc.get("defaultContentType")),
        payload=schemas.node(payload, "payload", defer_root=True) if payload is not None else None,
        examples=examples,
    )


def _message_ids(doc, channel, op):
    """Message ids of an operation: its own messages list, else its channel's."""
    refs = op.get("messages") or list((channel.get("messages") or {}).values())
    ids = []
    for ref in refs:
        if not isinstance(ref, dict) or "$ref" not in ref:
            raise SpecError(f"message reference is not a $ref: {ref!r}")
        target = ref["$ref"]
        if target.startswith("#/channels/"):
            target = resolve_ref(doc, target).get("$ref", "")
        if not target.startswith("#/components/messages/"):
            raise SpecError(f"unsupported message $ref: {ref['$ref']!r}")
        ids.append(target.rsplit("/", 1)[-1])
    return ids


def build_doc_model(spec_path, specs=None):
    spec_path = Path(spec_path)
    specs = specs or SpecSet(spec_path.parent)
    file_name = spec_path.name
    doc = specs.get(file_name)
    info = doc.get("info") or {}
    license_info = info.get("license") or {}

    servers = []
    for name, srv in (doc.get("servers") or {}).items():
        variables = [(var, _text((data or {}).get("description")))
                     for var, data in (srv.get("variables") or {}).items()]
        # some specs write the topic prefix with a trailing slash
        servers.append(Server(name, _text(srv.get("host")), _text(srv.get("protocol")),
                              _text(srv.get("pathname")).rstrip("/"), _text(srv.get("description")),
                              variables))
    pathname = servers[0].pathname if servers else ""

    operations_doc = doc.get("operations")
    if not isinstance(operations_doc, dict):
        raise SpecError("spec has no operations mapping")
    components = doc.get("components") or {}
    messages_doc = components.get("messages") or {}

    operations = []
    for op_id, op in operations_doc.items():
        action = op.get("action")
        if action not in ("send", "receive"):
            raise SpecError(f"operation '{op_id}' has invalid action {action!r}")
        channel = resolve_channel(doc, op.get("channel"))
        address = channel.get("address")
        entry = Operation(
            op_id=op_id,
            action=action,
            title=_text(op.get("title")) or _text(channel.get("title")),
            summary=_text(op.get("summary")),
            description=_text(op.get("description")) or _text(channel.get("description")),
            address=address,
            topic=f"{pathname}/{address}" if address and pathname else address,
            message_ids=_message_ids(doc, channel, op),
        )
        reply = op.get("reply")
        if isinstance(reply, dict):
            entry.reply_location = _text((reply.get("address") or {}).get("location"))
            if isinstance(reply.get("channel"), dict):
                reply_channel_id = channel_ref_id(reply["channel"])
                entry.reply_op_id = next(
                    (other_id for other_id, other in operations_doc.items()
                     if isinstance(other.get("channel"), dict)
                     and channel_ref_id(other["channel"]) == reply_channel_id), None)
        if address is None:
            entry.request_op_id, _ = find_request_operation(operations_doc, channel_ref_id(op["channel"]))
        operations.append(entry)

    builder = _SchemaBuilder(specs, file_name)
    messages = [_message(doc, specs, file_name, builder, msg_id, msg)
                for msg_id, msg in messages_doc.items()]
    by_id = {m.msg_id: m for m in messages}
    for op in operations:
        for msg_id in op.message_ids:
            if msg_id not in by_id:
                raise SpecError(f"operation '{op.op_id}' references unknown message '{msg_id}'")
            by_id[msg_id].used_by.append(op.op_id)

    schemas = [builder.node(schema, name)
               for name, schema in (components.get("schemas") or {}).items()]

    # builder.imports grows while imported schemas are built: they may refer
    # to further schemas of other APIs
    imported = []
    i = 0
    while i < len(builder.imports):
        target_file, name = builder.imports[i]
        target = resolve_ref(specs.get(target_file), f"#/components/schemas/{name}")
        imported.append((_schema_ref(file_name, target_file, name),
                         builder.node(target, name, file_name=target_file)))
        i += 1
    imported.sort(key=lambda item: (item[0].file_name, item[0].name))

    return DocModel(
        name=page_name(file_name),
        spec_id=_text(doc.get("id")),
        asyncapi_version=str(doc.get("asyncapi", "")),
        title=_text(info.get("title")) or page_name(file_name),
        version=str(info.get("version", "")),
        description=_text(info.get("description")),
        license_name=_text(license_info.get("name")),
        license_url=_text(license_info.get("url")),
        tags=[_text(t.get("name")) for t in info.get("tags") or [] if isinstance(t, dict)],
        default_content_type=_text(doc.get("defaultContentType")),
        servers=servers,
        operations=operations,
        messages=messages,
        schemas=schemas,
        imported_schemas=imported,
    )
