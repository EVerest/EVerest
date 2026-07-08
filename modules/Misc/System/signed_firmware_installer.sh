#!/bin/bash

. "${1}"

echo "$INSTALLING"
test -f "${3}"
sleep 2
echo "$INSTALLED"
