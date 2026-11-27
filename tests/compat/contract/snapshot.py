# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Reduce an EVerest source tree to a JSON snapshot of its public API contract.

No build is needed: everything is read from YAML, JSON and SQL files, so a snapshot of any git ref
can be produced from ``git archive``.
"""

from __future__ import annotations

import hashlib
import io
import json
import re
import subprocess
import tarfile
import tempfile
from pathlib import Path
from typing import Any, Optional

import yaml

from . import registry, release

FORMAT_VERSION = 1

ASYNCAPI_DIR = "docs/source/reference/EVerest_API"
MODULES_DIR = "modules"
OCPP_CONFIG_DIR = "lib/everest/ocpp/config"
OCPP16_KEY_MAPPINGS = f"{OCPP_CONFIG_DIR}/v16/ocpp16-custom-key-mappings.yaml"
RPC_API_TYPES = "modules/API/RpcApi/types/json_rpc_api.yaml"
FRAMEWORK_MIGRATIONS_DIR = "lib/everest/framework/schemas/migrations"
# Schemas of the files a deployment writes for the manager: the EVerest configuration and the settings.
FRAMEWORK_SCHEMAS = {
    "config": "lib/everest/framework/schemas/config.yaml",
    "settings": "lib/everest/framework/schemas/settings.yaml",
}
MANAGER_SOURCE = "lib/everest/framework/src/manager.cpp"

# States the release the tree is developed towards.
PROJECT_FILE = "CMakeLists.txt"

# Paths extracted from a git ref. Missing paths are tolerated so that old refs still snapshot.
CONTRACT_PATHS = (
    ASYNCAPI_DIR, MODULES_DIR, OCPP_CONFIG_DIR, FRAMEWORK_MIGRATIONS_DIR, *FRAMEWORK_SCHEMAS.values(), MANAGER_SOURCE,
    *registry.REGISTRY_FILES.values(), PROJECT_FILE,
)

# Migration directories are keyed by the store they belong to, not by their path, so that moving a
# directory between releases does not read as a rewritten schema history. A directory that is not
# listed is keyed by its path: the store is covered, and a move of it reads as a removal until it
# is listed here.
MIGRATION_FAMILIES = {
    "lib/everest/framework/schemas/migrations": "framework",
    "v16/core_migrations": "ocpp_v16_core",
    "v2/core_migrations": "ocpp_v2_core",
    "device_model_migrations": "ocpp_device_model",
    "PersistentSessionStorage/migrations": "persistent_session_storage",
}
MIGRATION_SEARCH_ROOTS = ("lib/everest", "modules")

# Dummy and Simulation modules are excluded from the public API
# (docs/source/project/releases/releases-and-versioning.rst).
EXEMPT_MODULE_CATEGORIES = {"Simulation"}
EXEMPT_MODULE_PREFIXES = ("Dummy",)

# Manifest keys of a config option that carry no contract (they are documentation).
CONFIG_OPTION_IGNORED_KEYS = {"description", "deprecated"}

RPC_API_VERSION_RE = re.compile(r"version\s+(\d+\.\d+\.\d+)\s+of the JSON RPC API")


def _load_yaml(path: Path) -> Any:
    return yaml.safe_load(path.read_text())


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text())


def _is_test_path(path: Path) -> bool:
    return any(part in ("tests", "test") for part in path.parts)


# ---------------------------------------------------------------------------------------------------
# AsyncAPI


def _pointer_get(doc: Any, pointer: str) -> Any:
    node = doc
    for raw in pointer.split("/")[1:]:
        key = raw.replace("~1", "/").replace("~0", "~")
        if isinstance(node, list):
            node = node[int(key)]
        else:
            node = node[key]
    return node


def _ref_name(ref: Any) -> Optional[str]:
    if isinstance(ref, dict):
        ref = ref.get("$ref")
    if not isinstance(ref, str):
        return None
    return ref.rsplit("/", 1)[-1]


def resolve_refs(node: Any, doc: Any, stack: tuple = ()) -> Any:
    """Inline local ``$ref`` pointers. A reference that is already being resolved is left as a
    ``{"$ref": ...}`` marker so that recursive schemas terminate."""
    if isinstance(node, dict):
        ref = node.get("$ref")
        if isinstance(ref, str) and ref.startswith("#/"):
            if ref in stack:
                return {"$ref": ref}
            return resolve_refs(_pointer_get(doc, ref[1:]), doc, stack + (ref,))
        return {key: resolve_refs(value, doc, stack) for key, value in node.items()}
    if isinstance(node, list):
        return [resolve_refs(item, doc, stack) for item in node]
    return node


def collect_asyncapi(root: Path) -> dict:
    specs = {}
    for path in sorted((root / ASYNCAPI_DIR).glob("*.yaml")):
        doc = _load_yaml(path)
        if not isinstance(doc, dict) or "asyncapi" not in doc:
            continue
        channels = {}
        for channel_name, channel in (doc.get("channels") or {}).items():
            messages = {}
            for message_name, message in (channel.get("messages") or {}).items():
                message = resolve_refs(message, doc)
                messages[message_name] = {"payload": message.get("payload")}
            channels[channel_name] = {"address": channel.get("address"), "messages": messages}
        operations = {}
        for operation_name, operation in (doc.get("operations") or {}).items():
            reply = operation.get("reply") or {}
            operations[operation_name] = {
                "action": operation.get("action"),
                "channel": _ref_name(operation.get("channel")),
                "reply_channel": _ref_name(reply.get("channel")),
            }
        servers = {}
        for server_name, server in (doc.get("servers") or {}).items():
            servers[server_name] = {key: server.get(key) for key in ("host", "pathname", "protocol")}
        specs[path.stem] = {
            "version": (doc.get("info") or {}).get("version"),
            "servers": servers,
            "channels": channels,
            "operations": operations,
        }
    return specs


# ---------------------------------------------------------------------------------------------------
# Modules


def _config_set(config: Optional[dict]) -> dict:
    options = {}
    for name, option in (config or {}).items():
        if not isinstance(option, dict):
            continue
        entry = {key: value for key, value in option.items() if key not in CONFIG_OPTION_IGNORED_KEYS}
        entry["required"] = "default" not in option
        options[name] = entry
    return options


def _module_category(manifest: Path, root: Path) -> Optional[str]:
    parts = manifest.relative_to(root).parts
    return parts[1] if len(parts) > 3 else None


def collect_modules(root: Path) -> dict:
    modules = {}
    for manifest in sorted((root / MODULES_DIR).rglob("manifest.yaml")):
        if _is_test_path(manifest.relative_to(root)):
            continue
        name = manifest.parent.name
        if _module_category(manifest, root) in EXEMPT_MODULE_CATEGORIES or name.startswith(EXEMPT_MODULE_PREFIXES):
            continue
        doc = _load_yaml(manifest)
        if not isinstance(doc, dict):
            continue
        provides = {}
        for impl_id, impl in (doc.get("provides") or {}).items():
            provides[impl_id] = {
                "interface": impl.get("interface"),
                "config": _config_set(impl.get("config")),
                "properties": {
                    key: value
                    for key, value in impl.items()
                    if key not in ("interface", "config", "description", "cmds", "vars")
                },
            }
        requires = {}
        for requirement_id, requirement in (doc.get("requires") or {}).items():
            requires[requirement_id] = {
                "interface": requirement.get("interface"),
                "min_connections": requirement.get("min_connections", 1),
                "max_connections": requirement.get("max_connections", 1),
            }
        modules[name] = {
            "path": manifest.parent.relative_to(root).as_posix(),
            "config": _config_set(doc.get("config")),
            "provides": provides,
            "requires": requires,
        }
    return modules


# ---------------------------------------------------------------------------------------------------
# OCPP configuration files


def _component_config(doc: dict) -> dict:
    required = set(doc.get("required") or [])
    variables = {}
    for variable_name, spec in (doc.get("properties") or {}).items():
        if not isinstance(spec, dict):
            continue
        attributes = {}
        for attribute in spec.get("attributes") or []:
            attribute_type = attribute.get("type", "Actual")
            attributes[attribute_type] = {
                key: attribute[key] for key in ("mutability", "value") if key in attribute
            }
        variables[variable_name] = {
            "characteristics": spec.get("characteristics") or {},
            "attributes": attributes,
            "default": spec.get("default"),
            "required": variable_name in required,
        }
    return {"name": doc.get("name"), "variables": variables}


def _profile_schema(doc: dict) -> dict:
    required = set(doc.get("required") or [])
    keys = {}
    for key, spec in (doc.get("properties") or {}).items():
        if not isinstance(spec, dict):
            continue
        entry = {k: v for k, v in spec.items() if k not in CONFIG_OPTION_IGNORED_KEYS}
        entry["required"] = key in required
        keys[key] = entry
    return keys


def collect_ocpp(root: Path) -> dict:
    config_dir = root / OCPP_CONFIG_DIR
    profiles = {}
    for path in sorted(config_dir.glob("v16/profile_schemas/*.json")):
        profiles[path.stem] = _profile_schema(_load_json(path))
    components = {}
    for path in sorted(config_dir.glob("**/component_config/*/*.json")):
        if _is_test_path(path.relative_to(root)):
            continue
        components[f"{path.parent.name}/{path.stem}"] = _component_config(_load_json(path))
    mappings = None
    mappings_path = root / OCPP16_KEY_MAPPINGS
    if mappings_path.exists():
        mappings = _load_yaml(mappings_path)
    return {
        "v16_profile_schemas": profiles,
        "component_config": components,
        "v16_custom_key_mappings": mappings,
    }


# ---------------------------------------------------------------------------------------------------
# SQL migrations


def _migration_family(directory: Path, root: Path) -> Optional[str]:
    relative = directory.relative_to(root).as_posix()
    for suffix, family in MIGRATION_FAMILIES.items():
        if relative.endswith(suffix):
            return family
    return None


def collect_migrations(root: Path) -> dict:
    migrations = {}
    for search_root in MIGRATION_SEARCH_ROOTS:
        base = root / search_root
        if not base.exists():
            continue
        for directory in sorted(p for p in base.rglob("*migrations") if p.is_dir()):
            if _is_test_path(directory.relative_to(root)):
                continue
            family = _migration_family(directory, root) or directory.relative_to(root).as_posix()
            for migration in sorted(directory.glob("*.sql")):
                digest = hashlib.sha256(migration.read_bytes()).hexdigest()
                migrations[f"{family}/{migration.name}"] = digest
    return migrations


# ---------------------------------------------------------------------------------------------------
# JSON RPC API


def collect_rpc_api(root: Path) -> Optional[dict]:
    path = root / RPC_API_TYPES
    if not path.exists():
        return None
    doc = _load_yaml(path)
    match = RPC_API_VERSION_RE.search(doc.get("description") or "")
    return {
        "version": match.group(1) if match else None,
        "types": doc.get("types") or {},
    }


# ---------------------------------------------------------------------------------------------------
# Manager: configuration file, settings file and command line

# desc.add_options()("name,n", po::value<...>(), "help"): the name, an optional one-letter alias,
# and whether the option takes a value.
MANAGER_OPTION_RE = re.compile(r'add_options\(\)\(\s*"([A-Za-z0-9_-]+)(?:,([A-Za-z]))?"\s*,\s*(po::value)?')
MANAGER_EXPERIMENTAL_RE = re.compile(r"EXPERIMENTAL_OPTIONS\s*\{([^}]*)\}")


def collect_framework(root: Path) -> dict:
    schemas = {}
    for name, relative in FRAMEWORK_SCHEMAS.items():
        path = root / relative
        if not path.exists():
            continue
        doc = _load_yaml(path)
        schema = resolve_refs(doc, doc)
        schema.pop("$defs", None)
        schemas[name] = schema
    return schemas


def collect_manager(root: Path) -> Optional[dict]:
    """The command line options the manager declares, None for a tree without its source."""
    path = root / MANAGER_SOURCE
    if not path.exists():
        return None
    source = path.read_text()
    options = {
        name: {"alias": alias or None, "takes_value": bool(value)}
        for name, alias, value in MANAGER_OPTION_RE.findall(source)
    }
    experimental = MANAGER_EXPERIMENTAL_RE.search(source)
    return {
        "options": options,
        # What the manager warns about at startup; a consistency test compares it with the registry.
        "experimental_options": sorted(re.findall(r'"([A-Za-z0-9_-]+)"', experimental.group(1))) if experimental else [],
    }


# ---------------------------------------------------------------------------------------------------
# Entry points


def snapshot_tree(root: Path, label: Optional[str] = None) -> dict:
    root = Path(root)
    return {
        "format": FORMAT_VERSION,
        "label": label or str(root),
        "asyncapi": collect_asyncapi(root),
        "modules": collect_modules(root),
        "ocpp": collect_ocpp(root),
        "migrations": collect_migrations(root),
        "rpc_api": collect_rpc_api(root),
        "framework": collect_framework(root),
        "manager": collect_manager(root),
        # Not part of the contract: the exceptions this tree documents, for a diff that has it as baseline.
        "registry": registry.collect(root),
        "version": release.project_version(root),
    }


def snapshot_ref(repo: Path, ref: str) -> dict:
    """Snapshot a git ref by extracting its contract paths into a temporary directory."""
    repo = Path(repo)
    resolved = subprocess.run(
        ["git", "-C", str(repo), "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}"], capture_output=True
    )
    if resolved.returncode != 0:
        raise ValueError(f"{ref!r} is not a commit of {repo}; a shallow clone lacks tags and the merge base")
    with tempfile.TemporaryDirectory(prefix="everest-contract-") as tmp:
        for path in CONTRACT_PATHS:
            result = subprocess.run(
                ["git", "-C", str(repo), "archive", "--format=tar", ref, "--", path],
                capture_output=True,
            )
            if result.returncode != 0:
                # A path that does not exist at this ref is not an error: older trees lack some.
                continue
            with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
                archive.extractall(tmp, filter="data")
        return snapshot_tree(Path(tmp), label=ref)


def write_snapshot(snapshot: dict, path: Path) -> None:
    Path(path).write_text(json.dumps(snapshot, indent=1, sort_keys=True) + "\n")


def read_snapshot(path: Path) -> dict:
    snapshot = json.loads(Path(path).read_text())
    if snapshot.get("format") != FORMAT_VERSION:
        raise ValueError(f"{path}: snapshot format {snapshot.get('format')!r}, this version reads {FORMAT_VERSION}")
    return snapshot
