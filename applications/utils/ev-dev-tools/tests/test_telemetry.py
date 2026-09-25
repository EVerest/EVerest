# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Pionix GmbH and Contributors to EVerest

import copy

import jsonschema
import pytest
import yaml

from conftest import SCHEMAS_DIR, TELEMETRY, add_module, make_manifest, run_ev_cli


@pytest.fixture(scope='module')
def manifest_validator():
    schema = yaml.safe_load((SCHEMAS_DIR / 'manifest.yaml').read_text())
    return jsonschema.Draft7Validator(schema)


def test_schema_accepts_every_kind(manifest_validator):
    manifest = make_manifest(TELEMETRY)
    manifest['telemetry']['fw_version']['x-origin'] = 'vendor extension'
    assert list(manifest_validator.iter_errors(manifest)) == []


@pytest.mark.parametrize('element', [
    {'kind': 'gauge', 'type': 'string'},
    {'kind': 'counter', 'type': 'integer', 'enum': ['A']},
    {'kind': 'gauge', 'type': 'number', '$ref': '/tel_test#/Mode'},
    {'kind': 'state', 'type': 'string', 'unit': 'V'},
    {'kind': 'state', 'type': 'boolean', 'enum': ['A']},
    {'kind': 'state', 'type': 'string', 'enum': ['A'], '$ref': '/tel_test#/Mode'},
    {'kind': 'state', 'type': 'number'},
    {'kind': 'event', 'type': 'object'},
    {'kind': 'event', 'type': 'string', '$ref': '/tel_test#/Diagnostics'},
    {'kind': 'gauge', 'type': 'number', 'unit': 'volts'},
    {'kind': 'gauge', 'type': 'number', 'unknown': 1},
    {'kind': 'state', 'type': 'string', 'enum': ['with space']},
    {'kind': 'histogram', 'type': 'number'},
], ids=lambda element: '-'.join(f'{k}={v}' for k, v in element.items()))
def test_schema_rejects_invalid_elements(manifest_validator, element):
    element = {'description': 'An element', **element}
    assert list(manifest_validator.iter_errors(make_manifest({'element': element}))) != []


@pytest.mark.parametrize('name', ['Temperature', 'with-dash', '1st', 'x' * 64])
def test_schema_rejects_invalid_names(manifest_validator, name):
    element = {'kind': 'gauge', 'type': 'number', 'description': 'An element'}
    assert list(manifest_validator.iter_errors(make_manifest({name: element}))) != []


def test_parse_maps_kinds_to_handles(parser_setup):
    from ev_cli import helpers, telemetry

    helpers.type_headers.clear()
    result = telemetry.parse_module_telemetry('TelTest', make_manifest(TELEMETRY))

    elements = {element['name']: (element['handle'], element['cpp_type']) for element in result['elements']}
    assert elements == {
        'temperature': ('Gauge', 'double'),
        'plug_ins': ('Counter', 'std::int64_t'),
        'fw_state': ('State', 'tel::types::FwState'),
        'mode': ('State', '::types::tel_test::Mode'),
        'relay_closed': ('State', 'bool'),
        'fw_version': ('State', 'std::string'),
        'diagnostics': ('Event', '::types::tel_test::Diagnostics'),
    }
    assert [element['name'] for element in result['elements']] == list(TELEMETRY)
    assert result['type_headers'] == ['generated/types/tel_test.hpp']
    assert result['inline_enums'] == [
        {'enum_type': 'FwState', 'enum': ['Idle', 'Measuring', 'Error'], 'description': 'Firmware state'}]
    assert helpers.type_headers == set()


def test_parse_without_telemetry(parser_setup):
    from ev_cli import telemetry

    assert telemetry.parse_module_telemetry('TelTest', make_manifest()) == {
        'elements': [], 'type_headers': [], 'inline_enums': []}


@pytest.mark.parametrize('name, element, message', [
    ('delete', {'kind': 'gauge', 'type': 'number'}, 'C++ keyword'),
    ('m_telemetry', {'kind': 'gauge', 'type': 'number'}, 'reserved'),
    ('state', {'kind': 'state', 'type': 'string', 'enum': ['default']}, 'C++ keyword'),
    ('missing_file', {'kind': 'state', 'type': 'string', '$ref': '/nowhere#/Mode'}, 'types/nowhere.yaml'),
    ('missing_type', {'kind': 'state', 'type': 'string', '$ref': '/tel_test#/Nothing'}, 'does not exist'),
    ('not_an_enum', {'kind': 'state', 'type': 'string', '$ref': '/tel_test#/Label'}, 'no enum values'),
    ('state_object', {'kind': 'state', 'type': 'string', '$ref': '/tel_test#/Diagnostics'}, 'should be of type'),
    ('event_enum', {'kind': 'event', 'type': 'object', '$ref': '/tel_test#/Mode'}, 'should be of type'),
])
def test_parse_reports_invalid_elements(parser_setup, name, element, message):
    from ev_cli import helpers, telemetry

    manifest = make_manifest({name: {'description': 'An element', **element}})
    with pytest.raises(helpers.EVerestParsingException) as error:
        telemetry.parse_module_telemetry('TelTest', manifest)
    assert f'TelTest/manifest.yaml: telemetry.{name}' in str(error.value)
    assert message in str(error.value)


def generate_loader(everest_dir, module):
    output_dir = everest_dir / 'generated'
    result = run_ev_cli(everest_dir, 'module', 'generate-loader', '--disable-clang-format',
                        '--schemas-dir', str(SCHEMAS_DIR), '--output-dir', str(output_dir),
                        '--everest-dir', str(everest_dir), '--', module)
    assert result.returncode == 0, result.stderr
    name = module.split('/')[-1]
    return ((output_dir / name / 'ld-ev.hpp').read_text(), (output_dir / name / 'ld-ev.cpp').read_text())


def test_loader_declares_telemetry_elements(everest_dir):
    module = add_module(everest_dir, 'TelTest', make_manifest(TELEMETRY))
    hpp, cpp = generate_loader(everest_dir, module)

    assert '#include <framework/telemetry.hpp>' in hpp
    assert '#include <generated/types/tel_test.hpp>' in hpp
    assert 'namespace tel {' in hpp
    assert 'enum class FwState {' in hpp
    assert 'inline void to_json(nlohmann::json& j, const FwState& e) {' in hpp
    assert 'struct Elements {' in hpp
    for declaration in [
        'everest::telemetry::Gauge<double> temperature;',
        'everest::telemetry::Counter<std::int64_t> plug_ins;',
        'everest::telemetry::State<tel::types::FwState> fw_state;',
        'everest::telemetry::State<::types::tel_test::Mode> mode;',
        'everest::telemetry::State<bool> relay_closed;',
        'everest::telemetry::State<std::string> fw_version;',
        'everest::telemetry::Event<::types::tel_test::Diagnostics> diagnostics;',
    ]:
        assert declaration in hpp

    assert 'everest::telemetry::ModuleTelemetry m_telemetry;' in hpp
    assert hpp.index('ModuleTelemetry m_telemetry;') < hpp.index('Gauge<double> temperature;')

    assert 'tel::Elements::Elements(everest::telemetry::ModuleTelemetry telemetry) :' in cpp
    assert 'm_telemetry(std::move(telemetry)),' in cpp
    assert 'temperature(m_telemetry, "temperature")' in cpp
    assert 'static tel::Elements tel_elements(adapter.make_telemetry ? adapter.make_telemetry() : everest::telemetry::ModuleTelemetry{});' in cpp
    assert 'tel_elements, ' in cpp


def test_loader_without_telemetry_has_no_telemetry_code(everest_dir):
    module = add_module(everest_dir, 'Plain', make_manifest())
    hpp, cpp = generate_loader(everest_dir, module)

    assert 'telemetry.hpp' not in hpp
    assert 'tel::' not in hpp + cpp
    assert 'namespace tel' not in hpp


def test_loader_reports_invalid_telemetry(everest_dir):
    telemetry = copy.deepcopy(TELEMETRY)
    telemetry['mode']['$ref'] = '/tel_test#/Label'
    module = add_module(everest_dir, 'Broken', make_manifest(telemetry))
    result = run_ev_cli(everest_dir, 'module', 'generate-loader', '--disable-clang-format',
                        '--schemas-dir', str(SCHEMAS_DIR), '--output-dir', str(everest_dir / 'generated'),
                        '--everest-dir', str(everest_dir), '--', module)
    assert result.returncode != 0
    assert 'Broken/manifest.yaml: telemetry.mode' in result.stderr


def test_module_skeleton_holds_telemetry_elements(everest_dir):
    module = add_module(everest_dir, 'TelTest', make_manifest(TELEMETRY))
    result = run_ev_cli(everest_dir, 'module', 'create', '--disable-clang-format',
                        '--schemas-dir', str(SCHEMAS_DIR), '--everest-dir', str(everest_dir),
                        '--build-dir', str(everest_dir / 'build'), '--', module)
    assert result.returncode == 0, result.stderr

    header = (everest_dir / 'modules' / 'Test' / 'TelTest' / 'TelTest.hpp').read_text()
    assert 'tel::Elements& tel_elements,' in header
    assert 'tel(tel_elements),' in header
    assert 'tel::Elements& tel;' in header


def test_module_skeleton_without_telemetry_is_unchanged(everest_dir):
    module = add_module(everest_dir, 'Plain', make_manifest())
    result = run_ev_cli(everest_dir, 'module', 'create', '--disable-clang-format',
                        '--schemas-dir', str(SCHEMAS_DIR), '--everest-dir', str(everest_dir),
                        '--build-dir', str(everest_dir / 'build'), '--', module)
    assert result.returncode == 0, result.stderr

    header = (everest_dir / 'modules' / 'Test' / 'Plain' / 'Plain.hpp').read_text()
    assert 'tel' not in header.replace('telemetry', '')


def test_loader_of_telemetry_receiver_exposes_the_catalog(everest_dir):
    manifest = make_manifest()
    manifest['telemetry_receiver'] = True
    hpp, cpp = generate_loader(everest_dir, add_module(everest_dir, 'Receiver', manifest))
    assert 'everest::telemetry::TelemetryCatalog get_telemetry_catalog();' in hpp
    assert 'return adapter.get_telemetry_catalog();' in cpp

    hpp, cpp = generate_loader(everest_dir, add_module(everest_dir, 'Producer', make_manifest(TELEMETRY)))
    assert 'get_telemetry_catalog' not in hpp + cpp
