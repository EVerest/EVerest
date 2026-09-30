#!/bin/bash
##
## SPDX-License-Identifier: Apache-2.0
## Copyright Pionix GmbH and Contributors to EVerest
##
./dist/bin/charge_point \
    --maindir ./dist/ocpp \
    --conf config-docker-tls.json
