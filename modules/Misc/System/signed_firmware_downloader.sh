#!/bin/bash

. "${1}"

SIGNATURE_VALIDATION_DIR=$(mktemp -d /tmp/signature_validation_XXXXX)
sleep 2
echo "$DOWNLOADING"

sleep 2
# Run the transfer in the background and wait on it so a TERM/INT reaching this
# script is forwarded to curl instead of being deferred until curl exits. Without
# this the cancelled transfer keeps its output pipe open and stalls the caller.
trap 'kill "$curl_pid" 2>/dev/null' TERM INT
curl --progress-bar --ssl --connect-timeout "$CONNECTION_TIMEOUT" "${2}" -o "${3}" &
curl_pid=$!
wait "$curl_pid"
curl_exit_code=$?
trap - TERM INT
sleep 2
if [[ $curl_exit_code -eq 0 ]]; then
    echo "$DOWNLOADED"
    echo -e "${4}" >"$SIGNATURE_VALIDATION_DIR/firmware_signature.base64"
    echo -e "${5}" >"$SIGNATURE_VALIDATION_DIR/firmware_cert.pem"
    openssl x509 -pubkey -noout -in "$SIGNATURE_VALIDATION_DIR/firmware_cert.pem" >"$SIGNATURE_VALIDATION_DIR/pubkey.pem"
    openssl base64 -d -in "$SIGNATURE_VALIDATION_DIR/firmware_signature.base64" -out "$SIGNATURE_VALIDATION_DIR/firmware_signature.sha256"
    r=$(openssl dgst -sha256 -verify "$SIGNATURE_VALIDATION_DIR/pubkey.pem" -signature "$SIGNATURE_VALIDATION_DIR/firmware_signature.sha256" "${3}")

    if [ "$r" = "Verified OK" ]; then
        echo "$SIGNATURE_VERIFIED"
    else
        echo "$INVALID_SIGNATURE"
    fi
else
    echo "$DOWNLOAD_FAILED"
fi

rm -rf "$SIGNATURE_VALIDATION_DIR"
