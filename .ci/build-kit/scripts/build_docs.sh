#!/bin/sh

TRAILBOOK_everest_IS_RELEASE=${TRAILBOOK_everest_IS_RELEASE:-"OFF"}
TRAILBOOK_everest_INSTANCE_NAME=${TRAILBOOK_everest_INSTANCE_NAME:-"nightly"}
TRAILBOOK_everest_DISPLAY_NAME=${TRAILBOOK_everest_DISPLAY_NAME:-""}
TRAILBOOK_everest_OVERWRITE_EXISTING_INSTANCE=${TRAILBOOK_everest_OVERWRITE_EXISTING_INSTANCE:-"OFF"}
TRAILBOOK_everest_DOWNLOAD_ALL_VERSIONS=${TRAILBOOK_everest_DOWNLOAD_ALL_VERSIONS:-"ON"}

# Credential helper, so the token never lands in .git/config.
if [ -n "${DOCS_REPO_READ_TOKEN:-}" ]; then
    # shellcheck disable=SC2016  # $DOCS_REPO_READ_TOKEN is expanded by git, not here
    git config --global credential.helper \
        '!f() { test "$1" = get && printf "username=x-access-token\npassword=%s\n" "$DOCS_REPO_READ_TOKEN"; }; f'
fi

# Needed for private dependencies fetched over SSH via the forwarded agent.
# The deployed-docs repository is reached over HTTPS and does not need this.
mkdir -p ~/.ssh
ssh-keyscan github.com >> ~/.ssh/known_hosts
chmod 600 ~/.ssh/known_hosts

cmake \
    -B "$EXT_MOUNT/build" \
    -S "$EXT_MOUNT/source" \
    -G Ninja \
    -D EVC_ENABLE_CCACHE=ON \
    -D EVEREST_ENABLE_COMPILE_WARNINGS=ON \
    -D EVEREST_ENABLE_RS_SUPPORT=ON \
    -D EVEREST_BUILD_DOCS=ON \
    -D TRAILBOOK_everest_DOWNLOAD_ALL_VERSIONS="$TRAILBOOK_everest_DOWNLOAD_ALL_VERSIONS" \
    -D TRAILBOOK_everest_IS_RELEASE="$TRAILBOOK_everest_IS_RELEASE" \
    -D TRAILBOOK_everest_INSTANCE_NAME="$TRAILBOOK_everest_INSTANCE_NAME" \
    -D TRAILBOOK_everest_DISPLAY_NAME="$TRAILBOOK_everest_DISPLAY_NAME" \
    -D TRAILBOOK_everest_OVERWRITE_EXISTING_INSTANCE="$TRAILBOOK_everest_OVERWRITE_EXISTING_INSTANCE" \
    -D EVEREST_DOCS_REPO_URL="$EVEREST_DOCS_REPO_URL"
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Configuring failed with return code $retVal"
    exit $retVal
fi

ninja -C "$EXT_MOUNT/build" trailbook_everest
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Compiling failed with return code $retVal"
    exit $retVal
fi
