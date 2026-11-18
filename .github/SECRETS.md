# CI Secrets

Every credential this repository's CI uses, what it grants, and where it is
configured. Keep this file in sync when adding or removing a secret.

## Principles

1. Same-repository operations use the automatic `GITHUB_TOKEN`. Every job
   declares an explicit `permissions:` block; none rely on the default scope.
2. Cross-repository writes use short-lived GitHub App tokens, minted per run
   by `actions/create-github-app-token@v3`. No long-lived deploy tokens or
   personal access tokens.
3. Tokens are requested with the narrowest permission subset that works, via
   the action's `permission-*` inputs. The documentation build receives a
   read-only token even though the same App can write.
4. Each container receives only the credential its step needs. The
   documentation build container gets a read-only token; only the separate
   deployment container gets a write token. A write credential does enter that
   container, which is the one place to revisit if builds move to remote
   builders.
5. Secrets are named for the access they confer, not for a service account or
   a mechanism, and are passed explicitly. `secrets: inherit` is not used.
   Apps are identified by their client ID; the deprecated App ID is not used.
6. Values reach `run:` scripts only through `env:` and are referenced as
   `"$VAR"`, never interpolated with `${{ }}` into the script text. This keeps
   inputs from being parsed as shell code and keeps tokens out of the
   generated script.
7. Checkouts set `persist-credentials: false` unless the job pushes with the
   checkout's credentials, so no token lands in a `.git/config` that build
   containers can read.

`.github/workflows/job_zizmor.yaml` audits every pull request for violations of
principles 1, 3, 6 and 7, configured in `.github/zizmor.yml`.

## Inventory

| Secret | Type | Configured in | Grants | Consumed by |
| --- | --- | --- | --- | --- |
| `GITHUB_TOKEN` | automatic | n/a | this repository only, narrowed per job | all workflows |
| `DOCS_DEPLOY_APP_CLIENT_ID` | GitHub App client ID | organisation | identifies the docs App | `job_build-docs.yaml`, `on_workflow_dispatch_delete.yaml` |
| `DOCS_DEPLOY_APP_PRIVATE_KEY` | GitHub App private key | organisation | `contents: read` or `contents: write` on `<owner>/everest.github.io`, per run | as above |
| `COVERAGE_DEPLOY_APP_CLIENT_ID` | GitHub App client ID | organisation | identifies the coverage App | `job_create-coverage-badge.yml` |
| `COVERAGE_DEPLOY_APP_PRIVATE_KEY` | GitHub App private key | organisation | `contents: write` on `<owner>/everest.github.io` | as above |
| `PRIVATE_DEPS_SSH_KEY` | SSH private key, optional | organisation or repository | read access to private dependency repositories | `job_build-cmake-gcc.yml`, `job_build-docs.yaml` |
| `BACKPORT_APP_CLIENT_ID` | GitHub App client ID | organisation | identifies the backport App | `on_label.yaml` |
| `BACKPORT_APP_PRIVATE_KEY` | GitHub App private key | organisation | `contents: write`, `pull-requests: write`, `metadata: read` on this repository | `on_label.yaml` |
| `HIMALAYAS_ACCESSOR_CLIENT_ID` | GitHub App client ID | organisation | identifies the Himalayas App | `on_schedule_move-pointers.yaml` |
| `HIMALAYAS_ACCESSOR_APP_PRIVATE_KEY` | GitHub App private key | organisation | `contents: write` on `EVerest/Himalayas`, used only for `repository_dispatch` | as above |

## Notes per secret

### `DOCS_DEPLOY_APP_*`

A GitHub App installed only on `<owner>/everest.github.io` with `contents:
write`. `job_build-docs.yaml` mints two tokens from it per run: a
`permission-contents: read` token passed into the docs build container for
trailbook's clone, and a `permission-contents: write` token passed into the
separate deployment container for the push. The build and deploy steps are
distinct `docker run` invocations, so the build container never holds a
credential that can write.

Rotation: regenerate the App's private key in the App settings and update the
secret. Nothing on the target repository changes.

Absence: documentation is built standalone. Runs with `deploy_docs: true` fail
early with an explicit error.

### `COVERAGE_DEPLOY_APP_*`

A separate App with the same installation and permission, kept distinct so
revoking coverage deployment does not affect documentation deployment. The
coverage jobs are currently disabled; the wiring is in place for when they are
re-enabled.

### `PRIVATE_DEPS_SSH_KEY`

Loaded into an ssh-agent by `.github/actions/setup-ssh-agent`, whose socket is
bind-mounted into the build containers. Empty upstream: every dependency in
`dependencies.yaml` is public HTTPS. It exists so private downstream forks can
build private dependencies without patching workflows.

The agent starts even when the key is empty, because `docker run` fails if the
socket it is asked to mount does not exist.

### `HIMALAYAS_ACCESSOR_*`

A GitHub App installed on `EVerest/Himalayas`. `on_schedule_move-pointers.yaml`
mints a `permission-contents: write` token scoped to that repository, the
minimum for `repository_dispatch`, to record each pointer move. The pointer
push itself uses `GITHUB_TOKEN`, so this token never reaches this repository.

Absence: the pointer still moves, but minting the token fails, leaving the
move unrecorded until the secret is set and the run is repeated.

## Forks

Fork pull requests receive no secrets. Consequently:

- Builds fetch dependencies over public HTTPS, which is sufficient upstream.
- Documentation is built standalone, without cloning the deployed-docs
  repository. Trailbook rejects `DOWNLOAD_ALL_VERSIONS` and `IS_RELEASE` both
  being off, so the standalone path forces `IS_RELEASE=ON`; the effect is
  confined to the build directory and nothing is pushed.
- No deployment of any kind is attempted.

## Setting up a new fork or organisation

Documentation deployment stays inoperative until an administrator has:

1. created the docs App, installed it on `<owner>/everest.github.io` with
   `contents: write`, and set `DOCS_DEPLOY_APP_CLIENT_ID` and
   `DOCS_DEPLOY_APP_PRIVATE_KEY`;
2. done the same for the coverage App and its two secrets, if the coverage
   jobs are to be re-enabled;
3. optionally set `PRIVATE_DEPS_SSH_KEY`, only if private dependencies are in
   use.
