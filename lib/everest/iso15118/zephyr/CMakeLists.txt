# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Pionix GmbH and Contributors to EVerest
#
# West module glue: wires libiso15118 and its cbv2g/util dependencies into a Zephyr build.
# Selected via Kconfig ISO15118 (see zephyr/Kconfig). Builds with ISO15118_ENABLE_TLS=OFF.

if(NOT CONFIG_ISO15118)
  return()
endif()

set(ISO15118_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_subdirectory(${ISO15118_ROOT}/../cbv2g ${CMAKE_CURRENT_BINARY_DIR}/cbv2g)

# --- everest::util: header-only, no compiled sources.
add_library(everest_util INTERFACE)
add_library(everest::util ALIAS everest_util)
target_include_directories(everest_util INTERFACE ${ISO15118_ROOT}/../util/include)
target_compile_features(everest_util INTERFACE cxx_std_17)

# --- iso15118 itself ---
set(DISABLE_EDM ON)
set(BUILD_TESTING OFF)
set(ISO15118_BUILD_TESTING OFF)
set(ISO15118_INSTALL OFF)
# TLS support (OpenSSL-backed) is not available on Zephyr; a mbedTLS-backed
# io::make_tls_connection() is planned as a separate follow-up.
set(ISO15118_ENABLE_TLS OFF)

add_subdirectory(${ISO15118_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/iso15118)

foreach(zephyr_dependent_target iso15118 cbv2g_tp cbv2g_iso20 cbv2g_exi_codec cbv2g_din cbv2g_iso2)
  if(TARGET ${zephyr_dependent_target})
    target_link_libraries(${zephyr_dependent_target} PUBLIC zephyr_interface)
  endif()
endforeach()
# everest_util is INTERFACE-only (header-only), so it must be linked with the INTERFACE keyword.
target_link_libraries(everest_util INTERFACE zephyr_interface)
