# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Pionix GmbH and Contributors to EVerest
#
# Smoke test for the libiso15118 Zephyr module wiring
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_iso15118)

target_sources(app PRIVATE src/main.cpp)
target_link_libraries(app PRIVATE iso15118::iso15118)
