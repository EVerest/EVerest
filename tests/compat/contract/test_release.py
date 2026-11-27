# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import pytest

from contract import release
from contract.release import ReleaseBaseline

TAGS = ["2026.02.0", "2026.02.0-rc4", "2026.02.1", "2026.02.2", "2026.10.0", "2026.10.0-rc3", "v2025.9.0", "nightly"]


def test_release_line():
    assert release.release_line("2026.10.0") == "2026.10"
    assert release.release_line("2026.10") == "2026.10"


def test_final_releases_are_sorted_and_exclude_candidates():
    assert release.final_releases(TAGS) == ["2026.02.0", "2026.02.1", "2026.02.2", "2026.10.0"]
    assert release.final_releases(["2026.02.10", "2026.02.9"]) == ["2026.02.9", "2026.02.10"]


def test_development_branch_follows_the_latest_release():
    assert release.release_baseline(TAGS) == ReleaseBaseline("2026.10.0", patch_release=False)
    assert release.release_baseline(TAGS, target_branch="main") == ReleaseBaseline("2026.10.0", patch_release=False)


def test_stable_branch_follows_the_latest_release_of_its_line():
    assert release.release_baseline(TAGS, target_branch="stable/2026.02") == ReleaseBaseline("2026.02.2", True)
    assert release.release_baseline(TAGS, target_branch="stable/2026.10") == ReleaseBaseline("2026.10.0", True)


def test_stable_branch_before_its_first_release_follows_the_lines_before():
    assert release.release_baseline(TAGS, target_branch="stable/2027.04") == ReleaseBaseline("2026.10.0", False)
    assert release.release_baseline(TAGS + ["2027.10.0"], target_branch="stable/2027.04") == ReleaseBaseline(
        "2026.10.0", False
    )


def test_patch_release_follows_the_previous_release_of_its_line():
    assert release.release_baseline(TAGS, new_tag="2026.02.2") == ReleaseBaseline("2026.02.1", True)
    assert release.release_baseline(TAGS, new_tag="2026.10.1") == ReleaseBaseline("2026.10.0", True)
    assert release.release_baseline(TAGS, new_tag="2026.10.1-rc1") == ReleaseBaseline("2026.10.0", True)


def test_first_release_of_a_line_follows_the_latest_release_before():
    assert release.release_baseline(TAGS, new_tag="2026.10.0") == ReleaseBaseline("2026.02.2", False)
    assert release.release_baseline(TAGS, new_tag="2027.04.0-rc1") == ReleaseBaseline("2026.10.0", False)


def test_no_baseline_without_an_earlier_release():
    assert release.release_baseline([]) is None
    assert release.release_baseline(TAGS, new_tag="2026.02.0") is None
    with pytest.raises(ValueError):
        release.release_baseline(TAGS, new_tag="nightly")


def test_project_version(tmp_path):
    assert release.project_version(tmp_path) is None
    (tmp_path / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.16)\n\nproject(everest-core\n    VERSION 2027.04.0\n)\n"
    )
    assert release.project_version(tmp_path) == "2027.04.0"
