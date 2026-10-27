# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Render the static HTML reference of the EVerest API specs.

Each spec becomes ``<name>/index.html``. The stylesheet and script are shared
by all pages and written once to ``_assets``; pages only link to them. An
RST index page lists every API for the Sphinx toctree.
"""

import re
import shutil
from pathlib import Path

import jinja2 as j2
from markupsafe import Markup, escape

from everest_api.doc_model import build_doc_model, schema_anchor
from everest_api.spec_loader import SpecError, SpecSet, validate_spec

_TEMPLATES_DIR = Path(__file__).parent / "templates" / "html"
_ASSETS_DIR = _TEMPLATES_DIR / "assets"
ASSET_FILES = ("everest-api.css", "everest-api.js")
# written by index.html.j2; marks the pages this generator may delete again
GENERATOR_MARK = '<meta name="generator" content="everest_api.docs_cli">'

_LIST_ITEM = re.compile(r"^\s*[-*]\s+(.*)$")
_CODE = re.compile(r"`([^`]+)`")
_BOLD = re.compile(r"\*\*([^*]+)\*\*")


def _inline(text):
    html = str(escape(text))
    html = _CODE.sub(r"<code>\1</code>", html)
    return _BOLD.sub(r"<strong>\1</strong>", html)


def render_description(text):
    """Render the markdown subset the specs use: paragraphs, ``- `` lists,
    inline code and bold. Everything else is escaped text."""
    if not text:
        return Markup("")
    out = []
    for block in re.split(r"\n\s*\n", text.strip()):
        paragraph, items = [], []

        def flush_paragraph():
            if paragraph:
                out.append("<p>" + _inline(" ".join(paragraph)) + "</p>")
                paragraph.clear()

        def flush_items():
            if items:
                out.append("<ul>" + "".join(f"<li>{_inline(i)}</li>" for i in items) + "</ul>")
                items.clear()

        for line in block.splitlines():
            match = _LIST_ITEM.match(line)
            if match:
                flush_paragraph()
                items.append(match.group(1))
            elif items and line.startswith((" ", "\t")) and line.strip():
                items[-1] += " " + line.strip()
            else:
                flush_items()
                if line.strip():
                    paragraph.append(line.strip())
        flush_paragraph()
        flush_items()
    return Markup("".join(out))


def _word_breaks(name):
    """Allow long identifiers to wrap after underscores, not mid-word."""
    return Markup("_<wbr>").join(str(name).split("_"))


def _make_env():
    env = j2.Environment(
        loader=j2.FileSystemLoader(str(_TEMPLATES_DIR)),
        autoescape=j2.select_autoescape(enabled_extensions=("html.j2",), default=False),
        lstrip_blocks=True,
        trim_blocks=True,
        undefined=j2.StrictUndefined,
        keep_trailing_newline=True,
    )
    env.filters["md"] = render_description
    env.filters["wordbreaks"] = _word_breaks
    env.globals["schema_anchor"] = schema_anchor
    return env


def generate_html(spec_path, out_dir, specs=None, env=None):
    """Render one page; pages link the shared assets as ``../_assets``."""
    model = build_doc_model(spec_path, specs)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    content = (env or _make_env()).get_template("index.html.j2").render(
        model=model, assets_href="../_assets")
    (out_dir / "index.html").write_text(content, encoding="utf-8")
    return model


def write_assets(out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in ASSET_FILES:
        shutil.copyfile(_ASSETS_DIR / name, out_dir / name)


def _is_generated_page(directory):
    page = directory / "index.html"
    try:
        return GENERATOR_MARK in page.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return False


def generate_reference(spec_files, html_dir, index_rst):
    """Render the whole reference: ``<html_dir>/<name>/index.html`` per spec,
    the shared assets in ``<html_dir>/_assets`` and the RST index page at
    *index_rst*. Every spec is validated against the AsyncAPI 3.0 schema
    before anything is rendered. Pages of specs that are no longer listed are
    deleted; other content of *html_dir* is left alone. The specs must share
    one directory, since they refer to each other by file name."""
    spec_files = [Path(p) for p in spec_files]
    directories = {p.resolve().parent for p in spec_files}
    if len(directories) != 1:
        raise SpecError("the specs of one reference must be in one directory")
    html_dir = Path(html_dir)
    specs = SpecSet(directories.pop())
    for spec in spec_files:
        validate_spec(specs.get(spec.name), spec.name)
    env = _make_env()
    models = []
    for spec in spec_files:
        try:
            models.append(generate_html(spec, html_dir / spec.stem, specs=specs, env=env))
        except SpecError as err:
            raise SpecError(f"{spec.name}: {err}") from None
    write_assets(html_dir / "_assets")
    expected = {spec.stem for spec in spec_files} | {"_assets"}
    for entry in html_dir.iterdir():
        if entry.name not in expected and entry.is_dir() and _is_generated_page(entry):
            shutil.rmtree(entry)
    index_rst = Path(index_rst)
    index_rst.parent.mkdir(parents=True, exist_ok=True)
    index_rst.write_text(env.get_template("api_index.rst.j2").render(models=models),
                         encoding="utf-8")
    return models
