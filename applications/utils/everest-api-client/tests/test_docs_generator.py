# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Tests for the API reference generator, run against the real API specs."""

import html
import os
import re
import subprocess
import sys
from html.parser import HTMLParser
from pathlib import Path

import pytest
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from everest_api.doc_model import _json_value, _SchemaBuilder, make_id, op_anchor  # noqa: E402
from everest_api import docs_cli  # noqa: E402
from everest_api.docs_generator import (  # noqa: E402
    ASSET_FILES, INDEX_NAME, generate_page, generate_reference, render_description, render_rst_description,
    rst_escape)
from everest_api.spec_loader import SpecError, SpecSet, load_spec  # noqa: E402
from everest_api.specs import find_spec_dir  # noqa: E402

SPECS_DIR = find_spec_dir()
SPEC_FILES = sorted(SPECS_DIR.glob("*.yaml"))
_LABEL = re.compile(r"^\.\. _([^:]+):$", re.M)


class _PageParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.hrefs = []
        self.inline_targets = []
        self.inline_scripts = 0
        self.styles = 0
        self.asset_urls = []
        self.s_names = set()
        self.codes = set()
        self._in = None
        self._script_has_src = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if tag == "a" and "href" in attrs:
            self.hrefs.append(attrs["href"])
        if "data-inline" in attrs:
            self.inline_targets.append(attrs["data-inline"])
        if tag == "script":
            self._script_has_src = "src" in attrs
            if "src" in attrs:
                self.asset_urls.append(attrs["src"])
        if tag == "link" and attrs.get("rel") == "stylesheet":
            self.asset_urls.append(attrs["href"])
        if tag == "style":
            self.styles += 1
        if tag == "span" and attrs.get("class") == "s-name":
            self._in = "s-name"
        elif tag == "code":
            self._in = "code"

    def handle_endtag(self, tag):
        if tag in ("span", "code"):
            self._in = None

    def handle_data(self, data):
        if self.lasttag == "script" and not self._script_has_src and data.strip():
            self.inline_scripts += 1
        if self._in == "s-name":
            self.s_names.add(data.strip())
        elif self._in == "code":
            self.codes.add(data.strip())




def _parse_page(text):
    """The raw HTML fragments of a page, plus the ids of its RST labels."""
    parser = _PageParser()
    parser.feed(text)
    parser.ids |= {make_id(label) for label in _LABEL.findall(text)}
    return parser


def _operation_section(text, page, op_id):
    start = text.index(f".. _everest_api_{page}_op_{op_id}:")
    end = text.find("\n.. _", start + 1)
    return text[start:] if end < 0 else text[start:end]


@pytest.fixture(scope="module")
def site(tmp_path_factory):
    """All pages, the index and the shared assets, generated the way the docs
    build does."""
    root = tmp_path_factory.mktemp("api")
    generate_reference(SPEC_FILES, root / "reference" / "api", root / "_static" / "everest-api")
    pages = {spec.stem: _parse_page(_page_text(root, spec.stem)) for spec in SPEC_FILES}
    return root, pages


def _page_text(root, name):
    return (root / "reference" / "api" / f"{name}.rst").read_text(encoding="utf-8")


def test_specs_found():
    assert len(SPEC_FILES) > 20


def test_internal_links_resolve(site):
    _, pages = site
    for name, page in pages.items():
        for href in page.hrefs:
            if href.startswith("#"):
                assert href[1:] in page.ids, f"{name}: dangling {href}"
            elif ".html#" in href and "/" not in href:
                other, anchor = href.split(".html#")
                assert other in pages, f"{name}: link to unknown page {href}"
                assert anchor in pages[other].ids, f"{name}: dangling {href}"


def test_on_demand_definitions_exist(site):
    _, pages = site
    for name, page in pages.items():
        for target in page.inline_targets:
            assert target in page.ids, f"{name}: no definition rendered for {target}"


def test_assets_shared_not_inlined(site):
    root, pages = site
    assert sorted(p.name for p in (root / "_static" / "everest-api").iterdir()) == sorted(ASSET_FILES)
    for name, page in pages.items():
        assert page.styles == 0, f"{name}: inline <style>"
        assert page.inline_scripts == 0, f"{name}: inline <script>"
        assert page.asset_urls == [], f"{name}: links its own assets"


@pytest.mark.parametrize("spec", SPEC_FILES, ids=lambda p: p.stem)
def test_nothing_from_the_spec_is_dropped(site, spec):
    """Every operation, message, schema, property name and enum value of the
    spec appears on its page."""
    _, pages = site
    page = pages[spec.stem]
    doc = load_spec(spec)
    for op_id in doc["operations"]:
        assert op_anchor(spec.stem, op_id) in page.ids
    for msg_id in doc["components"].get("messages", {}):
        assert f"msg-{msg_id}" in page.ids
    for schema_name, schema in doc["components"].get("schemas", {}).items():
        assert f"schema-{schema_name}" in page.ids
        for prop in _property_names(schema):
            assert prop in page.s_names, f"property {prop} of {schema_name} missing"
        for value in _enum_values(schema):
            assert html.unescape(_json_value(value)) in page.codes, f"enum value {value} of {schema_name} missing"


def _walk(schema):
    if isinstance(schema, dict):
        yield schema
        for value in schema.values():
            yield from _walk(value)
    elif isinstance(schema, list):
        for value in schema:
            yield from _walk(value)


def _property_names(schema):
    for node in _walk(schema):
        if isinstance(node.get("properties"), dict):
            yield from node["properties"]


def _enum_values(schema):
    for node in _walk(schema):
        if isinstance(node.get("enum"), list):
            yield from node["enum"]


def test_reply_operations_link_to_their_request(tmp_path):
    page = "auth_token_validator_API"
    generate_page(SPECS_DIR / f"{page}.yaml", tmp_path)
    text = (tmp_path / f"{page}.rst").read_text(encoding="utf-8")
    request = _operation_section(text, page, "receive_request_validate_token")
    assert '$message.header#/replyTo' in request
    assert f'href="#{op_anchor(page, "send_reply_validate_token")}"' in request
    reply = _operation_section(text, page, "send_reply_validate_token")
    assert f'href="#{op_anchor(page, "receive_request_validate_token")}"' in reply


def test_operations_are_headings_with_labels(site):
    root, _ = site
    text = _page_text(root, "lifecycle_API")
    assert "\n.. _everest_api_lifecycle_API_op_send_request_stop_modules:\n\nsend\\_request\\_stop\\_modules\n~~~~" in text


def test_topics_join_prefix_and_address_with_one_slash(site):
    """Some specs write the server pathname with a trailing slash."""
    root, _ = site
    for spec in SPEC_FILES:
        topics = re.findall(r'data-copy="([^"]*)"', _page_text(root, spec.stem))
        assert topics and all("//" not in t for t in topics), f"{spec.stem}: {topics}"
    assert 'data-copy="everest_api/1/lifecycle/m2e/stop_modules"' in _page_text(root, "lifecycle_API")


def test_empty_payload_schema_means_no_payload(site):
    root, _ = site
    section = _operation_section(_page_text(root, "isolation_monitor_API"), "isolation_monitor_API", "receive_start")
    assert "No payload." in section
    assert "generated" not in section


def test_ref_with_conflicting_sibling_type_fails(tmp_path):
    (tmp_path / "x_API.yaml").write_text(
        "components:\n"
        "  schemas:\n"
        "    Status: {type: string, enum: [Running, Idle]}\n"
        "    Session:\n"
        "      type: object\n"
        "      properties:\n"
        "        status: {type: object, $ref: '#/components/schemas/Status'}\n",
        encoding="utf-8")
    specs = SpecSet(tmp_path)
    builder = _SchemaBuilder(specs, "x_API.yaml")
    with pytest.raises(SpecError, match="declared as 'object' but the target is 'string'"):
        builder.node(specs.get("x_API.yaml")["components"]["schemas"]["Session"], "Session")


def test_payload_ref_to_a_non_schema_fails(tmp_path):
    (tmp_path / "x_API.yaml").write_text(
        "info: {title: X}\n"
        "servers: {default: {pathname: x}}\n"
        "channels: {c: {address: a, messages: {m: {$ref: '#/components/messages/m'}}}}\n"
        "operations: {op: {action: send, channel: {$ref: '#/channels/c'}}}\n"
        "components:\n"
        "  messages:\n"
        "    m: {payload: {$ref: '#/info/title'}}\n",
        encoding="utf-8")
    with pytest.raises(SpecError, match="'#/info/title' is not a schema"):
        generate_page(tmp_path / "x_API.yaml", tmp_path / "out")


def test_null_operation_fails(tmp_path):
    (tmp_path / "x_API.yaml").write_text("operations: {op: }\n", encoding="utf-8")
    with pytest.raises(SpecError, match="operation 'op' is not a mapping"):
        generate_page(tmp_path / "x_API.yaml", tmp_path / "out")


def test_invalid_spec_fails_the_reference(tmp_path):
    spec = tmp_path / "x_API.yaml"
    spec.write_text((SPECS_DIR / "powermeter_API.yaml").read_text(encoding="utf-8")
                    .replace("asyncapi:", "bogus: 1\nasyncapi:", 1), encoding="utf-8")
    with pytest.raises(SpecError, match="x_API.yaml is not a valid AsyncAPI 3.0 document"):
        generate_reference([spec], tmp_path / "rst", tmp_path / "assets")
    assert not (tmp_path / "rst").exists()
    assert not (tmp_path / "assets").exists()


def _tree(node):
    return [(child.name, child.type, child.children_kind, _tree(child)) for child in node.children]


def test_combinators_next_to_properties_are_kept(tmp_path):
    (tmp_path / "x_API.yaml").write_text(
        "components:\n"
        "  schemas:\n"
        "    Mixed:\n"
        "      type: object\n"
        "      properties: {a: {type: string}}\n"
        "      allOf: [{properties: {b: {type: integer}}}]\n"
        "      oneOf: [{type: object}, {type: array}]\n",
        encoding="utf-8")
    specs = SpecSet(tmp_path)
    node = _SchemaBuilder(specs, "x_API.yaml").node(specs.get("x_API.yaml")["components"]["schemas"]["Mixed"])
    assert node.children_kind == "properties"
    assert _tree(node) == [
        ("a", "string", None, []),
        (None, "allOf", "allOf", [("#1", "object", "properties", [("b", "integer", None, [])])]),
        (None, "oneOf", "oneOf", [("#1", "object", None, []), ("#2", "array", None, [])]),
    ]


def test_values_are_shown_as_json(tmp_path):
    (tmp_path / "x_API.yaml").write_text(
        "components:\n"
        "  schemas:\n"
        "    Flag: {type: boolean, default: false, enum: [true, false, null]}\n"
        "    Mode: {type: string, default: fast, enum: [fast, slow]}\n",
        encoding="utf-8")
    specs = SpecSet(tmp_path)
    builder = _SchemaBuilder(specs, "x_API.yaml")
    schemas = specs.get("x_API.yaml")["components"]["schemas"]
    flag, mode = builder.node(schemas["Flag"]), builder.node(schemas["Mode"])
    assert (flag.enum, flag.constraints) == (["true", "false", "null"], [("default", "false")])
    assert (mode.enum, mode.constraints) == (["fast", "slow"], [("default", "fast")])


def test_render_description_escapes_and_formats():
    out = str(render_description("Use `a<b>` **now**\n\n- one\n- two <script>"))
    assert "<code>a&lt;b&gt;</code>" in out
    assert "<strong>now</strong>" in out
    assert "<ul><li>one</li><li>two &lt;script&gt;</li></ul>" in out
    assert "<script>" not in out
    assert str(render_description("**a `b` c** `**d**`")) == "<p><strong>a <code>b</code> c</strong> <code>**d**</code></p>"


def test_render_rst_description_escapes_and_formats():
    out = render_rst_description("Use `a*b`s **now** for x_ and *y*\n\n- one\n  continued\n- `two`\n\n1. not a list")
    assert out == ("Use ``a*b``\\ s **now** for x\\_ and \\*y\\*\n\n"
                   "- one continued\n- ``two``\n\n"
                   "\\1. not a list\n")


def test_rst_escape_keeps_text_literal():
    assert rst_escape("send_reply") == "send\\_reply"
    assert rst_escape("|a| `b`") == "\\|a\\| \\`b\\`"
    assert rst_escape("- item\n  more") == "\\- item more"


def _experimental_specs():
    return [p for p in SPEC_FILES if (load_spec(p).get("info") or {}).get("x-experimental")]


def test_experimental_apis_show_a_warning(site):
    root, _ = site
    experimental = {p.stem for p in _experimental_specs()}
    assert "evse_security_consumer_API" in experimental
    for spec in SPEC_FILES:
        text = _page_text(root, spec.stem)
        assert (".. warning::\n\n   **Experimental API**" in text) == (spec.stem in experimental), spec.stem


def test_apis_in_the_experimental_index_are_marked():
    """Every AsyncAPI spec the experimental feature index names carries the
    x-experimental marker, so its page shows the warning."""
    index = (SPECS_DIR.parents[1] / "project" / "releases" / "experimental-index.rst").read_text(encoding="utf-8")
    listed = sorted(set(re.findall(r"``([a-z_]+_API)``", index)))
    assert listed
    unmarked = [name for name in listed
                if not (load_spec(SPECS_DIR / f"{name}.yaml").get("info") or {}).get("x-experimental")]
    assert unmarked == [], f"in the experimental index but not marked x-experimental: {unmarked}"


def test_experimental_apis_are_in_the_experimental_index():
    """The deprecation policy requires every experimental component to be
    listed in the experimental feature index."""
    index = (SPECS_DIR.parents[1] / "project" / "releases" / "experimental-index.rst").read_text(encoding="utf-8")
    for spec in _experimental_specs():
        assert spec.stem in index, f"{spec.stem} is experimental but missing from experimental-index.rst"


@pytest.mark.parametrize("marker", ["yes", {"since": "1", "unknown": 2}, 3])
def test_malformed_experimental_marker_fails(tmp_path, marker):
    doc = load_spec(SPECS_DIR / "auth_token_provider_API.yaml")
    doc["info"]["x-experimental"] = marker
    (tmp_path / "x_API.yaml").write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")
    (tmp_path / "auth_consumer_API.yaml").write_text(
        (SPECS_DIR / "auth_consumer_API.yaml").read_text(encoding="utf-8"), encoding="utf-8")
    (tmp_path / "display_message_API.yaml").write_text(
        (SPECS_DIR / "display_message_API.yaml").read_text(encoding="utf-8"), encoding="utf-8")
    with pytest.raises(SpecError, match="x-experimental"):
        generate_page(tmp_path / "x_API.yaml", tmp_path / "out")


def test_api_index_lists_every_api_and_marks_experimental_ones(site):
    root, _ = site
    text = _page_text(root, INDEX_NAME)
    assert text.startswith(".. _everest_api:\n")
    assert "\nEVerest API Specification\n" in text
    assert ".. warning::" in text
    listing, toctree = text.split(".. toctree::", 1)
    assert ":hidden:" in toctree
    toctree_entries = {e.strip() for e in toctree.splitlines()}
    experimental = {p.stem for p in _experimental_specs()}
    for spec in SPEC_FILES:
        assert spec.stem in toctree_entries
        entry = next(e for e in listing.splitlines() if e.startswith(f"- :doc:`{spec.stem}`"))
        assert ("(experimental" in entry) == (spec.stem in experimental), entry


def test_cli_renders_pages_assets_and_index(tmp_path):
    specs = [SPECS_DIR / "auth_consumer_API.yaml", SPECS_DIR / "display_message_API.yaml"]
    rst, assets = tmp_path / "rst", tmp_path / "assets"
    assert docs_cli.main(["--spec", *map(str, specs), "--rst-out", str(rst), "--assets-out", str(assets)]) == 0
    assert sorted(p.name for p in rst.iterdir()) == [
        "auth_consumer_API.rst", f"{INDEX_NAME}.rst", "display_message_API.rst"]
    assert sorted(p.name for p in assets.iterdir()) == sorted(ASSET_FILES)


def test_other_files_in_the_output_directory_are_kept(tmp_path):
    specs = [SPECS_DIR / "auth_consumer_API.yaml", SPECS_DIR / "display_message_API.yaml"]
    rst = tmp_path / "rst"
    rst.mkdir()
    (rst / "notes.rst").write_text("keep", encoding="utf-8")
    generate_reference(specs, rst, tmp_path / "assets")
    assert (rst / "notes.rst").read_text(encoding="utf-8") == "keep"


def test_unchanged_files_are_not_rewritten(tmp_path):
    specs = [SPECS_DIR / "auth_consumer_API.yaml", SPECS_DIR / "display_message_API.yaml"]
    rst, assets = tmp_path / "rst", tmp_path / "assets"
    generate_reference(specs, rst, assets)
    files = sorted(p for p in tmp_path.rglob("*") if p.is_file())
    for path in files:
        os.utime(path, ns=(0, 0))
    generate_reference(specs, rst, assets)
    assert [p.stat().st_mtime_ns for p in files] == [0] * len(files)


def test_specs_from_two_directories_are_rejected(tmp_path):
    (tmp_path / "x_API.yaml").write_text("{}", encoding="utf-8")
    with pytest.raises(SpecError, match="one directory"):
        generate_reference([SPECS_DIR / "auth_consumer_API.yaml", tmp_path / "x_API.yaml"], tmp_path, tmp_path)


_CONF_PY_IN = SPECS_DIR.parents[2] / "conf.py.in"
_API_HOOKS_START = "# The EVerest API reference pages"
_SPHINX_CONF = """
import posixpath
import re

from docutils import nodes

project = "EVerest API reference test"
html_static_path = ["_static"]

"""


def _sphinx_conf():
    """A minimal conf.py with the API reference hooks of the manual's."""
    text = _CONF_PY_IN.read_text(encoding="utf-8")
    return _SPHINX_CONF + text[text.index(_API_HOOKS_START):]


_SPHINX_ROOT = """
Root
====

.. _project-experimental-components:

Experimental components
-----------------------

.. _project-experimental-index:

Experimental index
------------------

.. toctree::

   reference/api/{index}
"""


class _BuiltPage(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.hrefs = []
        self.assets = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if tag == "a" and "href" in attrs:
            self.hrefs.append(attrs["href"])
        url = attrs.get("src") or (attrs.get("href") if tag == "link" else None)
        if url and "everest-api/" in url:
            self.assets.append(url)


_BUILT_PAGES = {
    "html": lambda out: {p.stem: p for p in (out / "reference" / "api").glob("*.html")},
    "dirhtml": lambda out: {p.parent.name: p for p in (out / "reference" / "api").glob("*/index.html")},
}


def _link_target(builder, href, page):
    """(page name, fragment) a link points to; page name None when the link
    leaves the API reference."""
    target, _, fragment = html.unescape(href).partition("#")
    if "://" in href:
        return None, fragment
    if builder == "html":
        return (target[:-len(".html")] if target.endswith(".html") and "/" not in target
                else None if target else page), fragment
    if not target:
        return page, fragment
    match = re.fullmatch(r"\.\./([^/]+)/", target)
    return (match.group(1) if match else None), fragment


@pytest.mark.parametrize("builder", sorted(_BUILT_PAGES))
def test_sphinx_builds_the_reference_without_warnings(tmp_path, builder):
    """The pages build with Sphinx without a warning, every link with a
    fragment finds its target in the built site, links to other API pages
    follow the builder's URLs, and only the API pages load the assets."""
    pytest.importorskip("sphinx")
    src, out = tmp_path / "src", tmp_path / "out"
    generate_reference(SPEC_FILES, src / "reference" / "api", src / "_static" / "everest-api")
    (src / "conf.py").write_text(_sphinx_conf(), encoding="utf-8")
    (src / "index.rst").write_text(_SPHINX_ROOT.replace("{index}", INDEX_NAME), encoding="utf-8")
    result = subprocess.run([sys.executable, "-m", "sphinx", "-W", "--keep-going", "-q", "-b", builder,
                             str(src), str(out)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr

    built = {}
    for name, path in _BUILT_PAGES[builder](out).items():
        parser = _BuiltPage()
        parser.feed(path.read_text(encoding="utf-8"))
        built[name] = parser
    assert len(built) == len(SPEC_FILES) + 1
    cross_page_links = 0
    for name, page in built.items():
        for href in page.hrefs:
            target, fragment = _link_target(builder, href, name)
            if not fragment or target is None:
                continue
            cross_page_links += target != name
            assert target in built, f"{name}: link to unknown page {href}"
            assert fragment in built[target].ids, f"{name}: dangling {href}"
        is_api_page = name != INDEX_NAME
        assert bool(page.assets) == is_api_page, f"{name}: {page.assets}"
    assert cross_page_links > 0
