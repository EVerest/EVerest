#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
author: andreas.heinrich@rwth-aachen.de
Fix author and signoff of commits to match DCO requirements.
"""

import argparse
from pathlib import Path
import subprocess


def get_git_author_string(args: argparse.Namespace) -> str:
    try:
        name = subprocess.run(
            ['git', '-C', str(args.git_root), 'config', 'user.name'],
            capture_output=True,
            text=True,
            check=True
        ).stdout.strip()
        
        email = subprocess.run(
            ['git', '-C', str(args.git_root), 'config', 'user.email'],
            capture_output=True,
            text=True,
            check=True
        ).stdout.strip()

    except subprocess.CalledProcessError as e:
        print(f'Git config failed with error: {e.stderr}')
        exit(1)

    result = f"{name} <{email}>"
    return result


def remove_signoffs(args: argparse.Namespace) -> str:
    try:
        msg_result = subprocess.run(
            ['git', '-C', str(args.git_root), 'log', '-1', '--pretty=%B'],
            capture_output=True,
            text=True,
            check=True
        )
    except subprocess.CalledProcessError as e:
        print(f'Git log failed with error: {e.stderr}')
        exit(1)

    current_msg = msg_result.stdout
    lines = current_msg.splitlines()
    filtered_lines = [
        line for line in lines 
        if not line.strip().startswith('Signed-off-by:')
    ]
    clean_msg = "\n".join(filtered_lines).strip()
    return clean_msg


def main():
    parser = argparse.ArgumentParser(
        description='Fix author and signoff of latest commit to match DCO requirements.')

    parser.add_argument(
        '--author', '-a',
        type=str,
        help='Author name and email in the format "Name <email>"',
        default=None
    )
    parser.add_argument(
        '--git-root', '-g',
        type=str,
        help='Git root directory (default: .)',
        default=str(Path.cwd())
    )
    parser.add_argument(
        '--remove-existing-signoffs', '--rm',
        action='store_true',
        help='Remove existing Signed-off-by trailers before adding the new one',
        default=False
    )

    args = parser.parse_args()

    args.git_root = Path(args.git_root).expanduser().resolve()
    
    if args.author is None:
        print(
            "\033[93m"
            "Warning:\n"
            "   You are overriding the author information with yourself as author.\n"
            "   Please make sure that you are the original author of the commit\n"
            "   or give credits to the original author in commit message by for example\n"
            "   adding a 'Co-authored-by: Original Author <email>' trailer to the commit message.\n"
            "\033[0m"
        )
        args.author = get_git_author_string(args)
    else:
        print(
            "\033[93m"
            "Warning:\n"
            "   You are overriding the author information with a custom value.\n"
            "   You are not allowed to signoff a commit for another person,\n"
            "   except if you are the original author and have the right to do so\n"
            "   or if you are a maintainer fixing the commit for the original author.\n"
            "   For example in case of mismatching user.name and user.email in\n"
            "   case of using github's web based git operations"
            "\033[0m"
        )

    if args.remove_existing_signoffs:
        remove_signoffs(args)

    subprocess_args = [
        'git', '-C', str(args.git_root),
        'commit', '--amend', '--no-edit',
        '--author', args.author,
        '--trailer', 'Signed-off-by: ' + args.author
    ]
    if args.remove_existing_signoffs:
        clean_msg = remove_signoffs(args)
        subprocess_args.extend(['-m', clean_msg])
    try:
        subprocess.run(
            subprocess_args,
            capture_output=True,
            text=True,
            check=True
        )
    except subprocess.CalledProcessError as e:
        print(f'Git amend failed with error:: {e.stderr}')
        exit(1)


if __name__ == '__main__':
    main()
