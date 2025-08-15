from typing import Annotated

import dagger

from .deploy_everest_doc import build_and_deploy_everest_docs


@dagger.function
async def nightly_everest_doc_build(
        github_pat: Annotated[dagger.Secret, dagger.Doc("GitHub Personal Access Token.")],
) -> str:
    """Nightly docs build."""
    return await build_and_deploy_everest_docs(
        is_release=False,
        build_pdf=False,
        build_html=True,
        deploy_html=True,
        snapshot_file="everest/everest/main:snapshots/nightly.yaml",
        github_pages_repo="everest/everest.github.io",
        overwrite_existing=True,
    )
