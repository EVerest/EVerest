#!/bin/sh
#
# Commit and push the freshly built documentation to the deployed-docs
# repository.
#
# This runs in a fresh container from the build-kit image, so the credential
# helper build_docs.sh configured is not available here; this script sets up
# its own with the write token.

set -eu

: "${EXT_MOUNT:?must be set}"
: "${DOCS_REPO_WRITE_TOKEN:?must be set}"
: "${GITHUB_SHA:?must be set}"
: "${COMMITTER_NAME:?must be set}"
: "${COMMITTER_EMAIL:?must be set}"

DEPLOYED_DOCS_REPO="$EXT_MOUNT/build/docs/deployed_docs_repo"

# Fail fast instead of blocking on an interactive credential prompt.
export GIT_TERMINAL_PROMPT=0

if [ ! -d "$DEPLOYED_DOCS_REPO/.git" ]; then
    echo "::error::$DEPLOYED_DOCS_REPO is not a git repository. The docs build ran without access to the deployed-docs repository, so there is nothing to deploy."
    exit 1
fi

git -C "$DEPLOYED_DOCS_REPO" config user.name "$COMMITTER_NAME"
git -C "$DEPLOYED_DOCS_REPO" config user.email "$COMMITTER_EMAIL"

git -C "$DEPLOYED_DOCS_REPO" add -A

# Must be `if`, not `cmd && exit 0`: under `set -e` a failing && list exits 1,
# which would turn "nothing to deploy" into a failed job.
if git -C "$DEPLOYED_DOCS_REPO" diff --cached --quiet; then
    echo "No documentation changes to deploy."
    exit 0
fi

git -C "$DEPLOYED_DOCS_REPO" commit -m "Update documentation from commit $GITHUB_SHA"

# The helper reads the token from the environment at push time, so it is never
# written to .git/config on the bind-mounted workspace.
# shellcheck disable=SC2016  # intentional: $DOCS_REPO_WRITE_TOKEN must stay
# unexpanded so git resolves it from the environment when it runs the helper.
git -C "$DEPLOYED_DOCS_REPO" \
    -c credential.helper='!f() { test "$1" = get && printf "username=x-access-token\npassword=%s\n" "$DOCS_REPO_WRITE_TOKEN"; }; f' \
    push origin HEAD:main

echo "Documentation pushed to the deployed-docs repository."
