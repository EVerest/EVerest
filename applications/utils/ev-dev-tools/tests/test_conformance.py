# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What the loader reports about everest-core's definitions as they stand.

The checks here say what the reports are, not how many: that every definition
parses, that nothing in the legacy dialect is fatal, and that each report names
the conformant spelling to write instead.  The losslessness check says the IR
does not quietly drop anything the definitions contain, which is what stops a
second backend from having to go back to the YAML the way EVerest's
documentation generation had to.
"""

from __future__ import annotations

import collections

import pytest
import yaml

from ev_cli.ir import build
from ev_cli.ir.common import CONSTRAINT_KEYWORDS
from ev_cli.schema.diagnostics import Category, Severity
from conftest import EVEREST_DIR, needs_everest

pytestmark = needs_everest


@pytest.fixture
def everest_diagnostics(everest_loader, sink):
    """Load every type unit and interface, collecting what is reported."""
    build.build_type_units(everest_loader, sink)
    build.build_interfaces(
        everest_loader, sink, everest_loader.tree.interface_names(),
        skip_unparsable=False)
    return sink


class TestDiagnostics:
    """How the legacy dialect is reported, whatever is left of it.

    ``legacy-ref`` reports one per reference; the sibling categories report one
    per keyword, so a single site carrying both ``minLength`` and ``maxLength``
    is reported twice.
    """

    def test_nothing_is_an_error(self, everest_diagnostics):
        """The legacy dialect is tolerated, not rejected."""
        assert not [
            d for d in everest_diagnostics if d.severity is Severity.ERROR]

    def test_every_interface_parses(self, everest_diagnostics):
        assert not everest_diagnostics.of(Category.INTERFACE_SKIPPED)

    def test_the_dangerous_ones_are_warnings(self, everest_diagnostics):
        """A sibling that would change meaning must not be filed as chatter."""
        dangerous = everest_diagnostics.of(Category.REF_SIBLING_CONTRADICTS)
        assert dangerous
        assert all(d.severity is Severity.WARNING for d in dangerous)

    def test_a_redundant_sibling_is_only_informational(self, everest_diagnostics):
        redundant = everest_diagnostics.of(Category.REF_SIBLING_TYPE)
        assert all(d.severity is Severity.INFO for d in redundant)

    def test_every_report_says_what_to_write_instead(self, everest_diagnostics):
        for diagnostic in everest_diagnostics:
            if diagnostic.category is Category.INTERFACE_SKIPPED:
                continue
            assert diagnostic.conformant_form, diagnostic


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
        for path in sorted((EVEREST_DIR / 'types').rglob('*.yaml')):
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
