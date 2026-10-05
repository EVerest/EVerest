# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Turns the archive of one statically linked module into an archive holding a single relocatable object in which
# every strong global symbol except the module entry is local, so that modules linked into one binary cannot collide
# the way they cannot collide as shared objects loaded with RTLD_LOCAL. Weak symbols (inline functions, templates)
# stay global and are shared between modules.
#
# Expects: ARCHIVE, PRIVATE_ARCHIVES (|-separated, may be empty), KEEP_SYMBOL, WORK_DIR, LINKER, NM, OBJCOPY, AR

string(REPLACE "|" ";" PRIVATE_ARCHIVES "${PRIVATE_ARCHIVES}")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(COMBINED "${WORK_DIR}/combined.o")

execute_process(
    COMMAND "${LINKER}" -r -o "${COMBINED}" --whole-archive "${ARCHIVE}" ${PRIVATE_ARCHIVES}
    RESULT_VARIABLE RESULT
)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Partial link of ${ARCHIVE} failed")
endif()

execute_process(
    COMMAND "${NM}" --defined-only --extern-only "${COMBINED}"
    OUTPUT_VARIABLE SYMBOLS
    RESULT_VARIABLE RESULT
)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Listing the symbols of ${COMBINED} failed")
endif()

string(REGEX MATCHALL "[^\n]+" LINES "${SYMBOLS}")
set(LOCALIZE "")
foreach(LINE ${LINES})
    # "<address> <type> <name>"; upper-case types other than W and V are strong definitions
    if(LINE MATCHES "^[0-9a-fA-F]* ([BCDGRST]) (.+)$" AND NOT CMAKE_MATCH_2 STREQUAL KEEP_SYMBOL)
        string(APPEND LOCALIZE "${CMAKE_MATCH_2}\n")
    endif()
endforeach()
file(WRITE "${WORK_DIR}/localize.txt" "${LOCALIZE}")

execute_process(
    COMMAND "${OBJCOPY}" "--localize-symbols=${WORK_DIR}/localize.txt" "${COMBINED}"
    RESULT_VARIABLE RESULT
)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Localizing the symbols of ${COMBINED} failed")
endif()

file(REMOVE "${ARCHIVE}")
execute_process(
    COMMAND "${AR}" rcs "${ARCHIVE}" "${COMBINED}"
    RESULT_VARIABLE RESULT
)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Writing ${ARCHIVE} failed")
endif()
