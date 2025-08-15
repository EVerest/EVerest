import sys
from typing import Annotated, Optional

import dagger
from dagger import function, object_type

from .functions.deploy_everest_doc import (
    build_and_deploy_everest_docs as build_and_deploy_everest_docs,
)
from .functions.nightly_everest_doc_build import (
    nightly_everest_doc_build as nightly_everest_doc_build,
)

DEFAULT_SNAPSHOT_FILE = "everest/everest/main:snapshots/some-snapshot.yaml"
DEFAULT_GITHUB_PAGES_REPO = "everest/everest.github.io"


@object_type
class Everest:
    """CI entrypoints for documentation and weekly lint.

    Exposes:
    - `deploy_everest_docs`: build and deploy docs
    - `deploy_everest_docs_nightly`: nightly docs build and deploy
    - `weekly_lint_repository`: repo lint
    """

    @function
    async def deploy_everest_docs(
            self,
            snapshot_file: Annotated[
                str,
                dagger.Doc("Snapshot file: <owner>/<repo>/<ref>:<path-to-snapshot.yaml>"),
            ] = DEFAULT_SNAPSHOT_FILE,
            is_release: Annotated[bool, dagger.Doc("Mark this snapshot as a release?")] = False,
            build_pdf: Annotated[bool, dagger.Doc("Build PDF docs?")] = False,
            build_html: Annotated[bool, dagger.Doc("Build HTML docs?")] = True,
            deploy_html: Annotated[bool, dagger.Doc("Deploy HTML docs to GitHub Pages?")] = True,
            github_pages_repo: Annotated[
                str, dagger.Doc("GitHub Pages repo: <owner>/<repo>")
            ] = DEFAULT_GITHUB_PAGES_REPO,
            overwrite_existing: Annotated[
                bool, dagger.Doc("Overwrite existing docs for this version?")
            ] = False,
            build_only: Annotated[
                bool, dagger.Doc("Build docs only; skip deploy and secrets")
            ] = False,
            export_dir: Annotated[
                Optional[str], dagger.Doc("Optional local export directory for HTML build")
            ] = None,
    ) -> str:
        """Build the documentation (optionally deploy)."""
        return await build_and_deploy_everest_docs(
            snapshot_file=snapshot_file,
            is_release=is_release,
            build_pdf=build_pdf,
            build_html=build_html,
            deploy_html=deploy_html,
            github_pages_repo=github_pages_repo,
            overwrite_existing=overwrite_existing,
            build_only=build_only,
            export_dir=export_dir,
        )

    @function
    async def deploy_everest_docs_nightly(
            self,
            github_pat: Annotated[
                dagger.Secret, dagger.Doc("GitHub Personal Access Token (secret).")
            ],
    ) -> str:
        """Nightly docs build."""
        return await nightly_everest_doc_build(github_pat=github_pat)

    @function
    async def weekly_lint_repository(self) -> None:
        """Run weekly lint repository."""
        async with dagger.Connection(dagger.Config(log_output=sys.stdout)) as client:
            src = client.host().directory(".")
            ctr = (
                client.container()
                .from_("node:20-bullseye")
                .with_exec(["apt-get", "update"])
                .with_exec(
                    [
                        "apt-get",
                        "install",
                        "-y",
                        "git",
                        "ca-certificates",
                        "bash",
                    ]
                )
                .with_mounted_directory("/repo", src)
                .with_workdir("/repo")
                .with_exec(
                    [
                        "git",
                        "config",
                        "--global",
                        "--add",
                        "safe.directory",
                        "/repo",
                    ]
                )
            )
            cmd = (
                "npx --yes repolinter@latest "
                "lint --dryRun -g https://github.com/everest/everest ; "
                "echo '__REPOLINTER_RC__:'$?"
            )
            txt = await ctr.with_exec(["bash", "-lc", cmd]).stdout()
            print(txt)
