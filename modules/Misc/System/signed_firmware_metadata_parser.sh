#!/bin/bash
awk '/===== BEGIN METADATA =====/{found=1; next} /===== END METADATA =====/{found=0} found' "${3}"
