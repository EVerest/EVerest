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
import logging as log
from enum import Enum

# Paths for modules, modules doc, interfaces, types: doc_dir/<path>
class OutputPath:
    MODULES = Path("_generated/modules")
    MODULES_DOC = Path("_included/modules_doc")
    INTERFACES = Path("_generated/interfaces")
    TYPES = Path("_generated/types")

class FileType(Enum):
    MODULE = "module"
    INTERFACE = "interface"
    TYPE = "type"

class YAMLItem(NamedTuple):
    name: str
    path: Path
    type: FileType
    out_path: Path
    template: jinja2.Template
    doc_path: Path = None
    target_doc_path: Path = None

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


def process_file_list(
    file_list: YAMLItemList,
):
    for item in file_list:
        if not item.out_path.parent.is_dir():
            item.out_path.parent.mkdir(parents=True)
        print(f"Process { item.path }")
        output = item.template.render(
            name=item.name,
            data=yaml.safe_load(item.path.read_text()),
            doc_path=item.doc_path,
        )
        item.out_path.write_text(output)

        if (
            item.doc_path is not None
        ):
            if item.type != FileType.MODULE:
                raise Exception(f"Doc path is only allowed for modules, not for { item.type }")
            if not item.target_doc_path.parent.is_dir():
                item.target_doc_path.parent.mkdir(parents=True)
            if item.doc_path.is_dir():
                copytree(item.doc_path, item.target_doc_path)
            else:
                copyfile(item.doc_path, item.target_doc_path)


def generate_module_list(args, templates):
    file_list: YAMLItemList = []
    for manifest_path in [
        *args.generate_module_list,
        *(args.core_dir / "modules").rglob("manifest.yaml"),
    ]:
        module_dir = manifest_path.parent
        if not manifest_path.is_file():
            log.error(f"Module: '{ module_dir }' doesn't have a manifest file")
            continue
        if (
            module_dir.name in args.ignore_module_list
            and module_dir not in args.generate_module_list
        ):
            log.info(f"Ignoring module: '{ module_dir }'")
            continue
        doc_path = None
        if (
            (module_dir / "doc.rst").is_file()
            and (module_dir / "docs").is_dir()
        ):
            log.error(f"Found multiple handwritten module documentation for: { module_dir.name }")
            doc_path = (module_dir / "docs")
        elif (module_dir / "doc.rst").is_file():
            doc_path = (module_dir / "doc.rst")
        elif (module_dir / "docs").is_dir():
            doc_path = (module_dir / "docs")
        out_path = args.doc_dir / OutputPath.MODULES / f"{ module_dir.name }.rst"
        target_doc_path = args.doc_dir / OutputPath.MODULES_DOC / f"{ module_dir.name }.rst"
        file_list.append(YAMLItem(
            name = module_dir.name,
            path = manifest_path,
            type = FileType.MODULE,
            out_path = out_path,
            template = templates["module.rst"],
            doc_path = doc_path,
            target_doc_path = target_doc_path,
        ))
    return file_list


def generate_interface_list(args, templates):
    file_list = []
    for item in [
        *args.generate_interface_list,
        *(args.core_dir / "interfaces").iterdir(),
    ]:
        if item.suffix != ".yaml":
            continue
        if (
            item.stem in args.ignore_interface_list
            and item not in args.generate_interface_list
        ):
            log.info(f"Ignoring interface: '{ item }'")
            continue
        out_path = args.doc_dir / OutputPath.INTERFACES / f"{ item.stem }.rst"
        file_list.append(YAMLItem(
            name = item.stem,
            path = item,
            type = FileType.INTERFACE,
            out_path = out_path,
            template = templates["interface.rst"],
        ))
        log.info(f"Adding interface: '{ item }' to the list of interfaces to generate")
    return file_list

def generate_type_list(args, templates):
    file_list = []
    for item in [
        *args.generate_type_list,
        *(args.core_dir / "types").iterdir(),
    ]:
        if item.suffix != ".yaml":
            continue
        if (
            item.stem in args.ignore_type_list
            and item not in args.generate_type_list
        ):
            log.info(f"Ignoring type: '{ item }'")
            continue
        out_path = args.doc_dir / OutputPath.TYPES / f"{ item.stem }.rst"       
        file_list.append(YAMLItem(
            name = item.stem,
            path = item,
            type = FileType.TYPE,
            out_path = out_path,
            template = templates["types.rst"],
        ))
        log.info(f"Adding type: '{ item }' to the list of types to generate")
    return file_list

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
    parser.add_argument(
        '--do-not-generate-module',
        type=str,
        dest='ignore_module_list',
        action='append',
        required=False,
        default=[],
        help="Add module to List of modules to ignore"
    )
    parser.add_argument(
        '--do-not-generate-interface',
        type=str,
        dest='ignore_interface_list',
        action='append',
        required=False,
        default=[],
        help="Add interface to List of interfaces to ignore"
    )
    parser.add_argument(
        '--do-not-generate-type',
        type=str,
        dest='ignore_type_list',
        action='append',
        required=False,
        default=[],
        help="Add type to List of types to ignore"
    )
    parser.add_argument(
        '--generate-module',
        type=Path,
        dest='generate_module_list',
        action='append',
        required=False,
        default=[],
        help="Add module to List of modules to generate additionaly to the default ones"
    )
    parser.add_argument(
        '--generate-interface',
        type=Path,
        dest='generate_interface_list',
        action='append',
        required=False,
        default=[],
        help="Add interface to List of interfaces to generate additionaly to the default ones"
    )
    parser.add_argument(
        '--generate-type',
        type=Path,
        dest='generate_type_list',
        action='append',
        required=False,
        default=[],
        help="Add type to List of types to generate additionaly to the default ones"
    )

    args = parser.parse_args()

    # check for the doc dir and create include and generated folders
    if not args.doc_dir.is_dir():
        args.doc_dir.mkdir()

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
    templates = {
        "module.rst": env.get_template("module.rst.jinja"),
        "interface.rst": env.get_template("interface.rst.jinja"),
        "types.rst": env.get_template("types.rst.jinja"),
    }

    # check if everest core directory is valid
    if not args.core_dir.is_dir():
        raise FileNotFoundError(f"Everest core path: '{ args.core_dir }' doesn't exist")

    file_list: YAMLItemList = generate_module_list(args, templates)
    file_list += generate_interface_list(args, templates)
    file_list += generate_type_list(args, templates)
    process_file_list(
        file_list,
    )

if __name__ == "__main__":
    main()
