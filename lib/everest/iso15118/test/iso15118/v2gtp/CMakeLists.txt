# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

include(Catch)

add_executable(test_sdp_codec sdp.cpp)
target_link_libraries(test_sdp_codec PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_sdp_codec DISCOVERY_MODE PRE_TEST)
