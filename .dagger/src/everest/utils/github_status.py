from enum import Enum
from typing import Optional

import requests


class CIStep(Enum):
    BUILD_KIT = "build-kit"
    LINT = "lint"
    CONFIGURE_CMAKE_GCC = "configure-cmake-gcc"
    BUILD_CMAKE_GCC = "build-cmake-gcc"
    UNIT_TESTS = "unit-tests"
    INSTALL_CMAKE_GCC = "install-cmake-gcc"
    INTEGRATION_TESTS = "integration-tests"
    OCPP_TESTS = "ocpp-tests"


class GithubStatusState(Enum):
    ERROR = "error"
    FAILURE = "failure"
    PENDING = "pending"
    SUCCESS = "success"


GITHUB_API_BASE_URL = "https://api.github.com/repos"


def initialize_status(
        auth_token: str,
        org_name: str,
        repo_name: str,
        sha: str,
        step: CIStep,
        description: Optional[str] = None,
) -> None:
    """Initialize a GitHub commit status for the given step."""
    api_url = f"{GITHUB_API_BASE_URL}/{org_name}/{repo_name}/statuses/{sha}"
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {auth_token}",
        "GitHub-Api-Version": "2022-11-28",
    }
    body = {
        "state": GithubStatusState.PENDING.value,
        "target_url": None,
        "description": description,
        "context": f"CI: {step.value}",
    }
    res = requests.post(api_url, json=body, headers=headers, timeout=15)
    if res.status_code != 201:
        raise RuntimeError(
            f"Failed to initialize GitHub status: {res.status_code} - {res.text}"
        )


def update_status(
        auth_token: str,
        org_name: str,
        repo_name: str,
        sha: str,
        step: CIStep,
        state: GithubStatusState,
        target_url: str,
        description: Optional[str] = None,
) -> None:
    """Update a GitHub commit status for the given step."""
    api_url = f"{GITHUB_API_BASE_URL}/{org_name}/{repo_name}/statuses/{sha}"
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {auth_token}",
        "GitHub-Api-Version": "2022-11-28",
    }
    body = {
        "state": state.value,
        "target_url": target_url,
        "description": description,
        "context": f"CI: {step.value}",
    }
    res = requests.post(api_url, json=body, headers=headers, timeout=15)
    if res.status_code != 201:
        raise RuntimeError(
            f"Failed to update GitHub status: {res.status_code} - {res.text}"
        )
