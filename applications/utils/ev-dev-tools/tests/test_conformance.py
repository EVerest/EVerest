# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What is found in everest-core's definitions as they stand.

The checks here say what the findings are, not how many: that every definition
builds, that nothing in the legacy dialect is fatal, and that each finding names
the conformant spelling to write instead.  The losslessness check says the IR
does not quietly drop anything the definitions contain, which is what spares a
second backend from going back to the YAML.
"""

from __future__ import annotations

import collections

import pytest
import yaml

from ev_cli.ir import build
from ev_cli.ir.common import CONSTRAINT_KEYWORDS
from ev_cli.schema.conformance import Category, collect_findings
from conftest import EVEREST_DIR, needs_everest

pytestmark = needs_everest


@pytest.fixture
def everest_findings(everest_loader):
    return collect_findings(everest_loader)


def test_every_type_unit_and_interface_builds(everest_loader):
    """Generating everything stops at the first definition that does not build."""
    build.build_type_units(everest_loader)
    build.build_interfaces(everest_loader, everest_loader.tree.interface_names())


def of(findings, category):
    return [finding for finding in findings if finding.category is category]


class TestFindings:
    """How the legacy dialect is reported, whatever is left of it.

    ``legacy-ref`` reports one per reference; the sibling categories report one
    per keyword, so a single site carrying both ``minLength`` and ``maxLength``
    is reported twice.
    """

    def test_the_legacy_dialect_is_reported_not_rejected(self, everest_findings):
        assert of(everest_findings, Category.LEGACY_REF)

    def test_a_contradicting_sibling_says_it_would_be_unsatisfiable(self, everest_findings):
        """A sibling that would change meaning must not read like a redundant one."""
        dangerous = of(everest_findings, Category.REF_SIBLING_CONTRADICTS)
        assert dangerous
        assert all('no value can satisfy' in finding.message for finding in dangerous)

    def test_every_kind_of_document_is_covered(self, everest_findings):
        undeclared = {finding.source.file.parent.name
                      for finding in of(everest_findings, Category.UNDECLARED_DIALECT)}
        assert {'types', 'interfaces', 'errors'} <= undeclared
        assert any(finding.source.file.name == 'manifest.yaml'
                   for finding in of(everest_findings, Category.UNDECLARED_DIALECT))

    def test_every_finding_says_what_to_write_instead(self, everest_findings):
        for finding in everest_findings:
            assert finding.conformant_form, finding


class TestLosslessness:
    """Nothing in the sources is silently discarded.

    The IR carries constraints the C++ backend has no use for, precisely so
    that a documentation or OpenAPI backend does not have to re-read the YAML.
    This test fails when a definition starts using a keyword the IR would drop.
    """

    #: Keywords the IR represents at a schema position.
    SCHEMA_KEYWORDS = frozenset({
        'type', 'description', '$ref', 'properties', 'required', 'items',
    }) | frozenset(CONSTRAINT_KEYWORDS) | {'default'}

    #: Keys the IR deliberately does not carry, and why.
    INTENTIONALLY_DROPPED = {
        # Seven config entries in the EEBUS module write "required: <bool>".
        # The manifest meta-schema permits it because it allows additional
        # properties, but nothing reads it: no generator, and not the framework.
        # Recorded rather than represented, because representing it would imply
        # it means something.
        'manifest-config': frozenset({'required'}),
    }

    def schema_keys(self, schema, keys):
        if not isinstance(schema, dict):
            return
        keys.update(schema)
        for key in ('items', 'additionalProperties'):
            if isinstance(schema.get(key), dict):
                self.schema_keys(schema[key], keys)
        if isinstance(schema.get('properties'), dict):
            for sub in schema['properties'].values():
                self.schema_keys(sub, keys)

    def test_type_files_use_no_keyword_the_ir_drops(self):
        keys: set[str] = set()
        for path in sorted((EVEREST_DIR / 'types').glob('*.yaml')):
            document = yaml.safe_load(path.read_text()) or {}
            assert set(document) <= {'description', 'types'}
            for schema in (document.get('types') or {}).values():
                self.schema_keys(schema, keys)
        assert keys <= self.SCHEMA_KEYWORDS, keys - self.SCHEMA_KEYWORDS

    def test_interfaces_use_no_keyword_the_ir_drops(self):
        document_keys: set[str] = set()
        payload_keys: set[str] = set()
        command_keys: set[str] = set()

        for path in sorted((EVEREST_DIR / 'interfaces').glob('*.yaml')):
            document = yaml.safe_load(path.read_text()) or {}
            document_keys.update(document)
            for schema in (document.get('vars') or {}).values():
                self.schema_keys(schema, payload_keys)
            for command in (document.get('cmds') or {}).values():
                command_keys.update(command)
                for schema in (command.get('arguments') or {}).values():
                    self.schema_keys(schema, payload_keys)
                if 'result' in command:
                    self.schema_keys(command['result'], payload_keys)

        assert document_keys <= {'description', 'vars', 'cmds', 'errors'}
        assert command_keys <= {'description', 'arguments', 'result'}
        # A var may additionally carry the mqtt quality of service.
        assert payload_keys <= self.SCHEMA_KEYWORDS | {'qos'}, (
            payload_keys - self.SCHEMA_KEYWORDS - {'qos'})

    def test_manifests_use_no_keyword_the_ir_drops(self):
        document_keys: set[str] = set()
        config_keys: set[str] = set()
        requires_keys: set[str] = set()

        for path in sorted((EVEREST_DIR / 'modules').rglob('manifest.yaml')):
            document = yaml.safe_load(path.read_text()) or {}
            document_keys.update(document)
            for entry in (document.get('config') or {}).values():
                config_keys.update(entry)
            for provided in (document.get('provides') or {}).values():
                for entry in (provided.get('config') or {}).values():
                    config_keys.update(entry)
            for required in (document.get('requires') or {}).values():
                requires_keys.update(required)

        assert document_keys <= {
            'description', 'config', 'provides', 'requires', 'metadata',
            'enable_external_mqtt', 'enable_telemetry', 'enable_global_errors',
            'capabilities',
        }
        assert requires_keys <= {
            'interface', 'min_connections', 'max_connections', 'ignore'}

        represented = {'type', 'description', 'mutability'} | frozenset(
            CONSTRAINT_KEYWORDS) | {'default'}
        unexpected = config_keys - represented - self.INTENTIONALLY_DROPPED['manifest-config']
        assert not unexpected, unexpected

    def test_the_dropped_manifest_key_really_is_unused(self):
        """If something started reading it, dropping it would be a bug."""
        offenders = collections.Counter()
        for path in sorted((EVEREST_DIR / 'modules').rglob('manifest.yaml')):
            document = yaml.safe_load(path.read_text()) or {}
            for name, entry in (document.get('config') or {}).items():
                if 'required' in entry:
                    offenders[path.parent.name] += 1
        # All of them are in one module; if that changes, revisit the decision.
        assert set(offenders) <= {'EEBUS'}, dict(offenders)
