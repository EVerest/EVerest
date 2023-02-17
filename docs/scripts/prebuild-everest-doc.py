#!/usr/bin/env -S python3 -tt
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright 2020 - 2022 Pionix GmbH and Contributors to EVerest
#

import argparse
from typing import List, NamedTuple
import jinja2
import yaml
from pathlib import Path
from shutil import copytree, copyfile

class YAMLItem(NamedTuple):
    name: str
    path: Path

YAMLItemList = List[YAMLItem]

# Jinja filters


def rst_indent(input):
    lines = input.splitlines()
    lines = [f"| { line }\r\n" for line in lines]
    return "".join(lines)


def make_rst_ref(input):
    output = input.replace("/", "")
    output = output.replace("#", "-")
    return output


def process_index_file(
    template: jinja2.Template,
    file_list: YAMLItemList,
    entry_ref_prefix: str,
    refname: str,
    headline: str,
    out_path: Path,
):

    prefix_list = [f"{ entry_ref_prefix }/{ item.name }" for item in file_list]
    output = template.render(
        file_list=prefix_list,
        refname=refname,
        headline=headline,
    )
    out_path.write_text(output)


def process_file_list(
    template: jinja2.Template,
    file_list: YAMLItemList,
    out_dir: Path,
    handwritten_modules: list
):
    if not out_dir.is_dir():
        out_dir.mkdir(parents=True)

    for item in file_list:
        include_handwritten = False
        if item.name in handwritten_modules:
            include_handwritten = True
        print(f"Process { item.path }")
        output = template.render(
            name=item.name,
            data=yaml.safe_load(item.path.read_text()),
            include_handwritten=include_handwritten,
        )

        (out_dir / item.name).with_suffix(".rst").write_text(output)


def main():
    parser = argparse.ArgumentParser(description='Process YAML files and jinja2 templates to generate documentation.')
    parser.add_argument(
        '--doc-directory',
        '-d',
        type=Path,
        dest='doc_dir',
        action='store',
        default=Path(),
        help="Output directory for generated files"
    )
    parser.add_argument(
        '--core-directory',
        '-c',
        type=Path,
        dest='core_dir',
        action='store',
        required=True,
        help="EVerest core directory"
    )
    parser.add_argument(
        '--snapshot-file',
        '-s',
        type=Path,
        dest='snapshot_file',
        action='store',
        required=True,
        help="Path to the snapshot file"
    )

    args = parser.parse_args()

    # check for the doc dir and create include and generated folders
    if not args.doc_dir.is_dir():
        args.doc_dir.mkdir()

    generated_dir: Path = (args.doc_dir / "generated")
    included_dir: Path = (args.doc_dir / "included/modules")
    template_dir: Path = (args.doc_dir / "templates")

    # Place the snapshot file in doc directory
    if not args.snapshot_file.is_file():
        raise FileNotFoundError(f"Snapshot file: '{ args.snapshot_file }' doesn't exist")
    copyfile(args.snapshot_file, args.doc_dir / "appendix/snapshot.yaml")

    # setup templates
    if not template_dir.is_dir():
        raise FileNotFoundError(f"Template path: '{ template_dir }' doesn't exist")

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(template_dir),
        trim_blocks=True,
        lstrip_blocks=True
    )
    env.filters['rst_indent'] = rst_indent
    env.filters['make_rst_ref'] = make_rst_ref



    # check if everest core directory is valid
    if not args.core_dir.is_dir():
        raise FileNotFoundError(f"Everest core path: '{ args.core_dir }' doesn't exist")

    # check for hand written modules
    handwritten_modules: List[str] = []
    custom_doc_path: Path = args.core_dir / "docs/modules"
    if custom_doc_path.is_dir():
        handwritten_modules.extend(
            item.stem
            for item in custom_doc_path.iterdir()
            if item.suffix == ".rst"
        )
        included_dir.mkdir(exist_ok=True, parents=True)
        copytree(custom_doc_path, included_dir, dirs_exist_ok=True)

    # start generating from YAMLs
    # modules
    file_list: YAMLItemList = []
    for module_path in (args.core_dir / "modules").iterdir():
        manifest_path = module_path / "manifest.yaml"
        if not manifest_path.is_file():
            continue
        file_list.append(YAMLItem(module_path.stem, manifest_path))

    process_file_list(
        env.get_template("module.rst.jinja"),
        file_list,
        generated_dir / "modules",
        handwritten_modules
    )
    process_index_file(
        env.get_template("file_list_ref.rst.jinja"),
        file_list,
        "modules",
        "everest_modules",
        "EVerest Modules",
        generated_dir / "everest_modules.rst",
    )

    # interfaces
    file_list = [YAMLItem(item.stem, item)
                 for item in (args.core_dir / "interfaces").iterdir() if item.suffix == ".yaml"]
    process_file_list(
        env.get_template("interface.rst.jinja"),
        file_list,
        generated_dir / "interfaces",
        handwritten_modules
    )
    process_index_file(
        env.get_template("file_list_ref.rst.jinja"),
        file_list,
        "interfaces",
        "everest_interfaces",
        "EVerest Interfaces",
        generated_dir / "everest_interfaces.rst",
    )

    # types
    file_list = [YAMLItem(item.stem, item) for item in (args.core_dir / "types").iterdir() if item.suffix == ".yaml"]
    process_file_list(
        env.get_template("types.rst.jinja"),
        file_list,
        generated_dir / "types",
        handwritten_modules
    )
    process_index_file(
        env.get_template("file_list_ref.rst.jinja"),
        file_list,
        "types",
        "everest_types",
        "EVerest Types",
        generated_dir / "everest_types.rst",
    )


if __name__ == "__main__":
    main()
