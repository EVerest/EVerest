# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Render the generated client package from a ClientModel using Jinja2.

Jinja environment mirrors ev-cli (applications/utils/ev-dev-tools/src/ev_cli/
ev.py): StrictUndefined so a missing variable fails loudly, and
trim_blocks/lstrip_blocks/keep_trailing_newline for clean, predictable output.
"""

from pathlib import Path

import jinja2 as j2

from everest_api_client_gen.model import build_model
from everest_api_client_gen.spec_loader import load_spec

_TEMPLATES_DIR = Path(__file__).parent / "templates"

# template name -> relative output path within the package (list-of-tuples keeps
# generation order deterministic)
_OUTPUTS = [
    ("gitignore.j2", ".gitignore"),
    ("README.j2", "README"),
    ("pyproject.toml.j2", "pyproject.toml"),
    ("setup.py.j2", "setup.py"),
    ("init.py.j2", "src/{snake}/__init__.py"),
    ("client.py.j2", "src/{snake}/client.py"),
    ("command_set.py.j2", "src/{snake}/{camel}ClientCommandSet.py"),
    ("fixture.py.j2", "src/{snake}/fixture.py"),
    ("requirements.txt.j2", "src/{snake}/requirements.txt"),
]


def _make_env():
    env = j2.Environment(
        loader=j2.FileSystemLoader(str(_TEMPLATES_DIR)),
        lstrip_blocks=True,
        trim_blocks=True,
        undefined=j2.StrictUndefined,
        keep_trailing_newline=True,
    )
    return env


def template_files():
    """List the template files (used by CMake as build dependencies)."""
    return sorted(str(p) for p in _TEMPLATES_DIR.glob("*.j2"))


def generate(spec_path, out_dir, name_camel, name_snake, interface_prefix,
             server="default"):
    doc = load_spec(spec_path)
    model = build_model(doc, name_camel, name_snake, interface_prefix, server)

    env = _make_env()
    out_dir = Path(out_dir)
    context = {"model": model}

    for template_name, rel_path in _OUTPUTS:
        rel = rel_path.format(snake=name_snake, camel=name_camel)
        dest = out_dir / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        content = env.get_template(template_name).render(**context)
        dest.write_text(content)

    return model
