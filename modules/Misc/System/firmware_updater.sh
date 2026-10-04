#!/bin/bash

#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#

. "${1}"

echo "$DOWNLOADING"
protocols=ftp,ftps,http,https
if curl --version | grep -qE '^Protocols:(.* )?sftp( |$)'; then
    protocols+=,sftp
fi
curl --progress-bar --ssl --proto "=$protocols" --proto-default https --connect-timeout "$CONNECTION_TIMEOUT" "${2}" -o "${3}"
curl_exit_code=$?
sleep 2
if [[ $curl_exit_code -eq 0 ]]; then
    echo "$DOWNLOADED"
else
    echo "$DOWNLOAD_FAILED"
fi
sleep 2

if [[ $curl_exit_code -eq 0 ]]; then
    echo "$INSTALLING"
    sleep 2
    echo "$INSTALLED"
    sleep 2
fi
