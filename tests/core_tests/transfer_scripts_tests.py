# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

# The installed System/Linux_Systemd_Rauc scripts pass the location to curl,
# which must not parse URLs starting with '-' as options.
# Passing e.g. -K would load a config file that could circumvent the --proto allow-list.

import subprocess
from pathlib import Path

import pytest

SECRET = "local file contents"


@pytest.fixture
def modules_dir(request) -> Path:
    return Path(request.config.getoption("--everest-prefix")).resolve() / "libexec/everest/modules"


def option_like_location(tmp_path: Path, url: str) -> str:
    config = tmp_path / "curl.cfg"
    config.write_text(f'proto = "=all"\nurl = "{url}"\n')
    return f"-K{config}"


def run_script(script: Path, *args) -> list[str]:
    if not script.exists():
        pytest.skip(f"{script} is not installed")
    result = subprocess.run(
        [str(script), str(script.parent / "constants.env"), *map(str, args)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return result.stdout.splitlines()


@pytest.mark.parametrize("script", ["System/firmware_updater.sh", "System/signed_firmware_downloader.sh"])
def test_download_location_is_not_parsed_as_option(modules_dir: Path, tmp_path: Path, script: str):
    secret = tmp_path / "secret"
    secret.write_text(SECRET)
    firmware = tmp_path / "firmware"
    firmware.touch()

    output = run_script(modules_dir / script, option_like_location(tmp_path, secret.as_uri()), firmware, "", "")

    assert "DownloadFailed" in output
    assert "Downloaded" not in output
    assert SECRET not in firmware.read_text()


@pytest.mark.parametrize("script", ["System/diagnostics_uploader.sh", "Linux_Systemd_Rauc/diagnostics_uploader.sh"])
def test_upload_location_is_not_parsed_as_option(modules_dir: Path, tmp_path: Path, script: str):
    diagnostics = tmp_path / "diagnostics"
    diagnostics.write_text(SECRET)
    uploaded = tmp_path / "uploaded"

    output = run_script(modules_dir / script, option_like_location(tmp_path, uploaded.as_uri()), diagnostics.name,
                        diagnostics)

    assert "Uploading" in output
    assert "Uploaded" not in output
    assert not uploaded.exists()
