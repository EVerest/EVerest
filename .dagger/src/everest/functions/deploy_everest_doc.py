import os
import sys
from typing import Annotated, Optional

import dagger

DOCKER_REGISTRY = "ghcr.io"
EVEREST_CI_VERSION = "v1.3.1"
GITHUB_PAGES_DEFAULT_REPO = "everest/everest.github.io"
BUILD_ENV_IMAGE_NAME = "everest/build-env-everest"


async def _setup_ssh_agent(ctr: dagger.Container, ssh_key: str) -> dagger.Container:
    """Start an SSH agent in the container and add the key."""
    ctr = ctr.with_mounted_tmpfs("/tmp/ssh-agent")
    ctr = ctr.with_exec(["mkdir", "-p", "/root/.ssh"])
    ctr = ctr.with_new_file("/root/.ssh/github_actions", contents=ssh_key, permissions=0o600)
    ctr = ctr.with_exec(["sh", "-c", "ssh-keyscan github.com >> /root/.ssh/known_hosts"])
    ctr = ctr.with_exec(["sh", "-c", "ssh-agent -a /tmp/ssh_agent.sock > /dev/null"])
    ctr = ctr.with_env_variable("SSH_AUTH_SOCK", "/tmp/ssh_agent.sock")
    ctr = ctr.with_exec(["ssh-add", "/root/.ssh/github_actions"])
    return ctr


async def get_snapshot_file(
        ctr: dagger.Container,
        snapshot_file: str,
        github_ssh_key: Optional[dagger.Secret],
        github_pages_repo: str = GITHUB_PAGES_DEFAULT_REPO,
        use_https: Optional[bool] = None,
) -> tuple[dagger.Directory, str]:
    owner, repo, ref = snapshot_file.split("/", 2)
    ref, path = ref.split(":")

    if use_https is None:
        use_https = github_ssh_key is None

    if use_https:
        clone_url = f"https://github.com/{owner}/{repo}.git"
    else:
        ctr = await _setup_ssh_agent(ctr, await github_ssh_key.plaintext())  # type: ignore[arg-type]
        clone_url = f"git@github.com:{owner}/{repo}.git"

    # Clone and extract the snapshot yaml
    ctr = ctr.with_exec(["git", "clone", "-b", ref, clone_url, "tmp-repo"])
    ctr = ctr.with_exec(["mkdir", "-p", "everest-workspace"])
    ctr = (
        ctr.with_workdir("tmp-repo")
        .with_exec(
            ["sh", "-c", f"git archive HEAD:$(dirname {path}) $(basename {path}) | tar -C ../everest-workspace -xf -"])
        .with_workdir("/")
        .with_exec(["rm", "-rf", "tmp-repo"])
        .with_exec(["sh", "-c", f"mv everest-workspace/$(basename {path}) everest-workspace/snapshot.yaml"])
    )

    # Inject GitHub Pages repo
    pages_repo_name = github_pages_repo.split("/")[-1]
    pages_url = (f"https://github.com/{github_pages_repo}.git"
                 if use_https else f"git@github.com:{github_pages_repo}.git")

    py_script = f"""
import yaml
with open('snapshot.yaml', 'r') as f:
    snapshot = yaml.safe_load(f)
snapshot["{pages_repo_name}"] = {{
    'git': '{pages_url}',
    'git_tag': 'main',
}}
with open('snapshot.yaml', 'w') as f:
    yaml.safe_dump(snapshot, f, default_flow_style=False)
"""
    ctr = ctr.with_workdir("everest-workspace").with_exec(["python", "-c", py_script])

    if use_https:
        ctr = (
            ctr
            .with_env_variable("GIT_TERMINAL_PROMPT", "0")
            .with_exec(["git", "config", "--global", "url.https://github.com/.insteadof", "git@github.com:"])
        )

    # edm
    ctr = (
        ctr
        .with_exec(["edm", "--config", "snapshot.yaml", "--workspace", "."])
        .with_exec(["edm", "snapshot", "snapshot.yaml", "--recursive"])
    )

    snapshot_name = os.path.splitext(os.path.basename(path))[0]
    return ctr.directory("/everest-workspace"), snapshot_name


async def build_documentation(
        ctr: dagger.Container,
        snapshot_dir: dagger.Directory,
        snapshot_name: str,
        github_pages_repo: str,
        build_html: bool,
        build_pdf: bool,
) -> tuple[Optional[dagger.Directory], Optional[dagger.Directory]]:
    """Build HTML and/or PDF documentation."""

    ctr = ctr.with_mounted_directory("/everest-workspace", snapshot_dir)

    ctr = ctr.with_exec([
        "sh", "-lc",
        "test -f /everest-workspace/everest/docs/requirements.txt "
        "&& python -m pip install -r /everest-workspace/everest/docs/requirements.txt || true"
    ])

    ctr = ctr.with_exec([
        "python", "-m", "pip", "install",
        "sphinx",
        "myst-parser",
        "sphinx-rtd-theme",
        "sphinxcontrib-contentui",
        "sphinxcontrib-jquery",
    ])

    ctr = (
        ctr.with_workdir("/everest-workspace")
        .with_exec([
            "python",
            "everest/docs/scripts/prebuild-everest-doc.py",
            "--doc-directory", "everest/docs/",
            "--core-directory", "everest-core/",
        ])
    )

    html_doc_dir = None
    if build_html:
        ctr = ctr.with_env_variable("SPHINXOPTS", f"-D version='{snapshot_name}'")
        ctr = ctr.with_workdir("/everest-workspace/everest/docs").with_exec(["make", "html"])
        html_doc_dir = ctr.directory("/everest-workspace/everest/docs/_build/html")

    pdf_doc_dir = None
    if build_pdf:
        ctr = ctr.with_env_variable("SPHINXOPTS", f"-D version='{snapshot_name}'")
        ctr = ctr.with_workdir("/everest-workspace/everest/docs").with_exec(["make", "latexpdf"])
        pdf_doc_dir = ctr.directory("/everest-workspace/everest/docs/_build/latex")

    return html_doc_dir, pdf_doc_dir


async def deploy_documentation(
        ctr: dagger.Container,
        html_doc_dir: dagger.Directory,
        snapshot_name: str,
        github_pages_repo: str,
        github_pat: dagger.Secret,
        github_ssh_key: dagger.Secret,
        is_release: bool,
        overwrite_existing: bool,
) -> None:
    """Deploy HTML documentation to the GitHub Pages repository."""
    ctr = await _setup_ssh_agent(ctr, await github_ssh_key.plaintext())
    ctr = ctr.with_exec(["git", "config", "--global", "user.email", "compiler@pionix.de"])
    ctr = ctr.with_exec(["git", "config", "--global", "user.name", "Github Service Account"])

    pages_repo_dir_name = github_pages_repo.split('/')[-1]
    ctr = ctr.with_exec(["git", "clone", f"git@github.com:{github_pages_repo}.git", pages_repo_dir_name])

    ctr = ctr.with_mounted_directory("docs-html", html_doc_dir)

    if overwrite_existing:
        ctr = ctr.with_workdir(pages_repo_dir_name).with_exec(["rm", "-rf", f"docs/{snapshot_name}"])

    ctr = (
        ctr.with_workdir(pages_repo_dir_name)
        .with_exec(["mkdir", "-p", "docs"])
        .with_exec(["touch", "docs/.nojekyll"])
        .with_exec(["mkdir", "-p", f"docs/{snapshot_name}"])
        .with_exec(["sh", "-c", f"cp -r ../docs-html/* docs/{snapshot_name}/"])
    )

    deploy_script = "everest-workspace/everest/docs/scripts/deploy-everest-doc.py"
    is_release_flag = "--is-release" if is_release else ""
    ctr = ctr.with_exec(
        [
            "python3",
            deploy_script,
            "--template-directory",
            "everest-workspace/everest/docs/templates/",
            "--html-root-directory",
            f"{pages_repo_dir_name}/docs/",
            is_release_flag,
            "--version-name",
            snapshot_name,
        ]
    )

    ctr = (
        ctr.with_workdir(pages_repo_dir_name)
        .with_exec(["git", "add", "--all"])
        .with_exec(
            ["sh", "-c", f"git diff-index --quiet HEAD || git commit -m 'Add doc build snapshot {snapshot_name}'"])
        .with_exec(["git", "push"])
    )

    await ctr.exit_code()


async def _bootstrap_public_image(client: dagger.Client) -> dagger.Container:
    """For testing."""
    return (
        client.container()
        .from_("ubuntu:24.04")
        .with_env_variable("DEBIAN_FRONTEND", "noninteractive")
        .with_exec(["apt-get", "update"])
        .with_exec([
            "apt-get", "install", "-y",
            "git", "python3", "python3-venv", "make",
            "gcc", "g++", "pkg-config", "ca-certificates",
            "openssh-client"
        ])

        .with_exec(["python3", "-m", "venv", "/opt/venv"])
        .with_env_variable("VIRTUAL_ENV", "/opt/venv")
        .with_env_variable(
            "PATH",
            "/opt/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
        )

        .with_exec(["python", "-m", "pip", "install", "--upgrade", "pip", "setuptools", "wheel"])
        .with_exec(["python", "-m", "pip", "install", "jstyleson", "jsonschema", "Jinja2", "PyYAML"])

        .with_exec([
            "python", "-m", "pip", "install",
            "git+https://github.com/EVerest/everest-dev-environment.git#subdirectory=dependency_manager"
        ])
    )


async def build_and_deploy_everest_docs(
        snapshot_file: Annotated[
            str,
            dagger.Doc("Snapshot file location: <owner>/<repo>/<ref>:<path-to-snapshot.yaml>"),
        ] = "everest/everest/main:snapshots/example-snapshot.yaml",
        is_release: Annotated[bool, dagger.Doc("Is this snapshot a release?")] = False,
        build_pdf: Annotated[bool, dagger.Doc("Build PDF documentation?")] = False,
        build_html: Annotated[bool, dagger.Doc("Build HTML documentation?")] = True,
        deploy_html: Annotated[bool, dagger.Doc("Deploy HTML documentation?")] = True,
        github_pages_repo: Annotated[str, dagger.Doc("GitHub Pages repo: <owner>/<repo>")] = GITHUB_PAGES_DEFAULT_REPO,
        overwrite_existing: Annotated[bool, dagger.Doc("Overwrite existing documentation?")] = False,
        runner: Annotated[str, dagger.Doc("Runner label (unused)")] = "ubuntu-24.04",
        build_only: Annotated[bool, dagger.Doc("Build docs only; skip deploy and secrets")] = False,
        export_dir: Annotated[Optional[str], dagger.Doc("If set, export built HTML here")] = None,
) -> str:
    """Build (and optionally deploy) EVerest documentation using Dagger."""

    async with dagger.Connection(dagger.Config(log_output=sys.stdout)) as client:
        build_env_image = f"{DOCKER_REGISTRY}/{BUILD_ENV_IMAGE_NAME}:{EVEREST_CI_VERSION}"
        if build_only:
            print("build-only: using public Ubuntu + venv")
            build_ctr = await _bootstrap_public_image(client)
        else:
            print(f"using prebuilt image if available: {build_env_image}")
            try:
                build_ctr = client.container().from_(build_env_image)
                # Touch to force resolution now; raises if pull fails
                await build_ctr.with_exec(["bash", "-lc", "true"]).exit_code()
                print("prebuilt image available")
            except Exception as e:
                print(f"fallback to public Ubuntu + venv (cannot pull {build_env_image}): {e}")
                build_ctr = await _bootstrap_public_image(client)

        git_cache = client.cache_volume("git-cache")
        cpm_cache = client.cache_volume("cpm-cache")
        apt_cache = client.cache_volume("apt-cache")
        build_ctr = (
            build_ctr
            .with_mounted_cache("/root/.cache/git", git_cache)
            .with_mounted_cache("/root/.cache/CPM", cpm_cache)
            .with_env_variable("CPM_SOURCE_CACHE", "/root/.cache/CPM")
            .with_mounted_cache("/var/cache/apt/archives", apt_cache)
        )

        github_pat = None
        github_ssh_key = None
        have_secrets = False
        if not build_only and deploy_html:
            pat_val = os.environ.get("SA_GITHUB_PAT")
            ssh_key_val = os.environ.get("SA_GITHUB_SSH_KEY")
            have_secrets = bool(pat_val and ssh_key_val)
            if have_secrets:
                github_pat = client.set_secret("SA_GITHUB_PAT", pat_val)  # type: ignore[arg-type]
                github_ssh_key = client.set_secret("SA_GITHUB_SSH_KEY", ssh_key_val)  # type: ignore[arg-type]
                print("deploy enabled: secrets detected")
            else:
                print("deploy enabled but no secrets found; continuing as build-only (deploy skipped)")

        print("preparing workspace via EDM")
        snapshot_dir, snapshot_name = await get_snapshot_file(
            build_ctr,
            snapshot_file,
            github_ssh_key,
            github_pages_repo,
            use_https=(build_only or not have_secrets),
        )
        print(f"workspace ready for snapshot '{snapshot_name}'")

        print("building docs (sphinx)")
        html_docs, pdf_docs = await build_documentation(
            build_ctr,
            snapshot_dir,
            snapshot_name,
            github_pages_repo,
            build_html,
            build_pdf,
        )
        print("docs build finished")

        # optional local export for testing
        if html_docs is not None and export_dir:
            print(f"exporting HTML docs to '{export_dir}'")
            await html_docs.export(export_dir)

        if build_html and deploy_html and not build_only and html_docs is not None and have_secrets:
            print("deploying to GitHub Pages")
            await deploy_documentation(
                build_ctr,
                html_docs,
                snapshot_name,
                github_pages_repo,
                github_pat,
                github_ssh_key,
                is_release,
                overwrite_existing,
            )
            print("deploy complete")
            return "EVerest documentation pipeline completed (built and deployed)."

        return "EVerest documentation pipeline completed (build-only)."
