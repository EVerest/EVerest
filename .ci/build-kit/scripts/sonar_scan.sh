#!/bin/sh
#
# SonarQube Cloud analysis, run inside the build-kit container after compile.sh.
#
# It has to run here and not on the runner: the C/C++ analyzer replays every entry of
# compile_commands.json, so it needs the same compiler, system headers, generated headers
# ($EXT_MOUNT/build) and CPM dependency sources ($EXT_MOUNT/cache/cpm) as the build.
#
# Expected environment (passed in with `docker run --env`):
#   SONAR_TOKEN               analysis token (repository secret)
#   SONAR_SCANNER_VERSION     sonar-scanner CLI version, e.g. 8.1.0.6389
#   SONAR_PR_KEY / SONAR_PR_BRANCH / SONAR_PR_BASE   set for pull requests
#   SONAR_BRANCH              set for branch analysis (e.g. main)
#
# $EXT_MOUNT/scripts/sonar-project.properties holds the static settings.

set -e

if [ -z "$SONAR_TOKEN" ]; then
    echo "SONAR_TOKEN is not set, skipping SonarQube Cloud analysis"
    exit 0
fi

COMPILE_COMMANDS="$EXT_MOUNT/build/compile_commands.json"
if [ ! -f "$COMPILE_COMMANDS" ]; then
    echo "No $COMPILE_COMMANDS - configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    exit 1
fi

SCANNER_DIR="$EXT_MOUNT/cache/sonar-scanner-$SONAR_SCANNER_VERSION"
if [ ! -x "$SCANNER_DIR/bin/sonar-scanner" ]; then
    echo "Downloading sonar-scanner $SONAR_SCANNER_VERSION"
    curl -sSfL -o /tmp/sonar-scanner.zip \
        "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${SONAR_SCANNER_VERSION}-linux-x64.zip"
    # the image has python3 but no unzip; keep the file modes (java, jspawnhelper must stay executable)
    python3 - /tmp/sonar-scanner.zip "$EXT_MOUNT/cache/" <<'PY'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for info in z.infolist():
        path = z.extract(info, sys.argv[2])
        mode = info.external_attr >> 16
        if mode and not info.is_dir():
            os.chmod(path, mode & 0o7777)
PY
    mv "$EXT_MOUNT/cache/sonar-scanner-${SONAR_SCANNER_VERSION}-linux-x64" "$SCANNER_DIR"
    rm /tmp/sonar-scanner.zip
fi

# settings come from the scripts dir (copied from the trusted ref), not from the analysed checkout
set -- \
    -Dproject.settings="$EXT_MOUNT/scripts/sonar-project.properties" \
    -Dsonar.projectBaseDir="$EXT_MOUNT/source" \
    -Dsonar.cfamily.compile-commands="$COMPILE_COMMANDS" \
    -Dsonar.working.directory="$EXT_MOUNT/build/.scannerwork"

if [ -n "$SONAR_PR_KEY" ]; then
    set -- "$@" \
        -Dsonar.pullrequest.key="$SONAR_PR_KEY" \
        -Dsonar.pullrequest.branch="$SONAR_PR_BRANCH" \
        -Dsonar.pullrequest.base="$SONAR_PR_BASE"
elif [ -n "$SONAR_BRANCH" ]; then
    set -- "$@" -Dsonar.branch.name="$SONAR_BRANCH"
fi

cd "$EXT_MOUNT/source"
exec "$SCANNER_DIR/bin/sonar-scanner" "$@"
