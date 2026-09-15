#!/bin/bash

. "${1}"

echo "$INSTALLING"
test -f "${3}"
sleep 2

# A firmware artifact whose remote filename ends in -bad.pnx is designated
# to fail installation. "${2}" is the remote location URI; strip any query
# or fragment before taking the basename. Callers that omit it fall through
# to the success path unchanged.
location="${2%%[?#]*}"
case "${location##*/}" in
    *-bad.pnx)
        echo "$INSTALL_VERIFICATION_FAILED"
        exit 0
        ;;
esac

echo "$INSTALLED"
