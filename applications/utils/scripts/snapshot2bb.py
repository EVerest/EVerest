#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
author: kai-uwe.hermann@pionix.de
Parse snapshot.yaml or dependencies.yaml files and modify corresponding .bb recipes
"""
import argparse
import os
import re
import subprocess
import yaml
from pathlib import Path


def get_remote_rev(remote_url: str, git_tag: str) -> str:
    """Return the commit the git_tag of the remote repo points to, or an empty string."""
    refs = {}
    try:
        result = subprocess.run(["git", "ls-remote", remote_url, f"refs/tags/{git_tag}", f"refs/tags/{git_tag}^{{}}"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        for entry in result.stdout.decode("utf-8").splitlines():
            rev, _tab, ref = entry.partition("\t")
            refs[ref] = rev
    except subprocess.CalledProcessError:
        return ""

    return refs.get(f"refs/tags/{git_tag}^{{}}", refs.get(f"refs/tags/{git_tag}", ""))


def get_default_branch(remote_url: str) -> str:
    """Return the default branch of the remote repo, or an empty string."""
    try:
        result = subprocess.run(["git", "ls-remote", "--symref", remote_url, "HEAD"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        for entry in result.stdout.decode("utf-8").splitlines():
            if entry.startswith("ref: refs/heads/"):
                return entry.removeprefix("ref: refs/heads/").partition("\t")[0]
    except subprocess.CalledProcessError:
        return ""

    return ""


def main():
    parser = argparse.ArgumentParser(
        description='modify .bb files based on a snapshot')

    parser.add_argument('--input',
                        dest='in_snapshot',
                        help='Path to the snapshot.yaml or dependencies.yaml file')
    parser.add_argument('--out',
                        dest='out_dir',
                        help='Relative path to meta-everest')

    args = parser.parse_args()

    in_snapshot = os.path.realpath(os.path.expanduser(args.in_snapshot))
    out_dir = Path(os.path.realpath(os.path.expanduser(args.out_dir)))
    
    snapshot = None
    with open(in_snapshot, encoding='utf-8') as snapshot_file:
        try:
            snapshot = yaml.safe_load(snapshot_file)
        except yaml.YAMLError as e:
            print(f"Error parsing yaml of {in_snapshot}: {e}")
            return

    if not snapshot:
        print(f"snapshot empty?")
        return

    output = {}

    print(f"got snapshot: {snapshot}")

    recipes_core = out_dir / 'recipes-core'
    if not recipes_core.exists():
        print(f"cannot find recipes-core in {recipes_core}")
        return

    # TODO: provide this via a file as a command line parameter
    mapping = {'Josev': 'recipes-core/josev/python3-iso15118',
               'everest-utils': 'recipes-core/everest-devtools/evcli',
               'EVerest': 'recipes-core/everest/everest-core',
               'everest-framework': 'recipes-core/everest/everest-framework',
               'everest-sqlite': 'recipes-core/everest/everest-sqlite',
               'libcbv2g': 'recipes-core/everest/libcbv2g',
               'libevse-security': 'recipes-core/everest/libevse-security',
               'libfsm': 'recipes-core/everest/libfsm',
               'libiso15118': 'recipes-core/everest/libiso15118',
               'liblog': 'recipes-core/everest/liblog',
               'libnfc-nci': 'recipes-core/everest/libnfc-nci',
               'libocpp': 'recipes-core/everest/libocpp',
               'libslac': 'recipes-core/everest/libslac',
               'libtimer': 'recipes-core/everest/libtimer',
               'date': 'recipes-backports/date/date',
               'ftxui': 'recipes-backports/ftxui/ftxui',
               'json-rpc-cxx': 'recipes-devtools/json-rpc-cxx/json-rpc-cxx',
               'libfmt': 'recipes-backports/fmt/fmt',
               'libwebsockets': 'recipes-backports/libwebsockets/libwebsockets',
               'nlohmann_json': 'recipes-backports/nlohmann-json/nlohmann-json',
               'nlohmann_json_schema_validator': 'recipes-backports/json-schema-validator/json-schema-validator',
               'ryml': 'recipes-backports/rapidyaml/rapidyaml',
               'sdbus-cpp': 'recipes-backports/sdbus-c++/sdbus-c++',
               'sigslot': 'recipes-extended/sigslot/sigslot'}

    # dependencies.yaml has no branches, add releases here that are not on the default branch of their repo
    branches = {}

    # a branch derived from a variable like ${SRCBRANCH} follows PV and is left alone
    branch_re = re.compile(r'\bbranch=(?!\$\{)([^;|^"|\s]*)')
    pv_re = re.compile(r'PV\s*=\s*"(v?)\d')
    commit_re = re.compile(r'[0-9a-f]{40}')

    for key, entry in snapshot.items():
        print(f"key: {key}")
        if key not in mapping:
            continue
        git_tag = entry['git_tag']
        branch = entry.get('branch') or branches.get(key) or get_default_branch(entry['git'])
        git_rev = entry.get('git_rev')
        if commit_re.fullmatch(git_tag):
            git_rev = git_tag
            git_tag = 'git'
        if not git_rev:
            git_rev = get_remote_rev(entry['git'], git_tag)
        if not branch or not git_rev:
            print(f"could not determine branch or revision of {git_tag}, skipping")
            continue
        version = git_tag.removeprefix('wip-release-')
        version = version.removeprefix('v')
        version_without_rc, _vsep, _rc = version.partition('-')
        # todo: maybe even strip something like "wip-release-" from git_tag to keep versions sane?
        # or just do not modify the version if this cannot be parsed properly?
        print(f"version: {version} without-rc: {version_without_rc}")
        if key in mapping:
            file_glob = f'{mapping[key]}_*.bb'
            files = list(out_dir.glob(file_glob))
            if len(files) == 1:
                bb_path = files[0]
                
                print(f"found a matching file: {bb_path}")
                with open(bb_path) as bb_file:
                    bb_content = bb_file.read().splitlines()
                    bb_content_modified = []
                    for index, line in enumerate(bb_content):
                        if line.startswith("SRC_URI"):
                            def replace(match):
                                return f'branch={branch}'
                            line = branch_re.sub(replace, line)
                        elif line.startswith("SRCREV"):
                            line = f"SRCREV = \"{git_rev}\""
                        elif pv_match := pv_re.match(line):
                            line = f"PV = \"{pv_match.group(1)}{version_without_rc}\""

                        bb_content_modified.append(line)
                    # create new filename
                    prefix, sep, postfix = bb_path.name.partition('_')
                    new_filename = f'{prefix}_{version_without_rc}.bb'
                    new_bb_path = bb_path.parent / new_filename
                    if bb_path != new_bb_path:
                        print(f"filename is different, so remove the old one")
                        bb_path.unlink(missing_ok=True)

                    print(f"writing bb file: {new_bb_path}")
                    with open(new_bb_path, mode='w+') as new_bb_file:
                        new_bb_file.write('\n'.join(bb_content_modified))
                        new_bb_file.write('\n')


if __name__ == '__main__':
    main()
