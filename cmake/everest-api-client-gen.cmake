# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Makes the `everest-api-client-gen` tool available in the build (installed into
# the build venv when one is active, otherwise found on the system) and exposes
# its template files as a variable so generated clients regenerate when a
# template changes. Mirrors cmake/ev-cli.cmake.

function(setup_everest_api_client_gen)
    if(NOT ${${PROJECT_NAME}_USE_PYTHON_VENV})
        message(STATUS "Using system everest-api-client-gen instead of installing it in the build venv.")
        find_program(EVEREST_API_CLIENT_GEN everest-api-client-gen REQUIRED)
    else()
        ev_is_python_venv_active(
            RESULT_VAR IS_PYTHON_VENV_ACTIVE
        )
        if(NOT ${IS_PYTHON_VENV_ACTIVE})
            message(FATAL_ERROR "Python venv is not active. Please activate the python venv before running this command.")
        endif()

        get_target_property(SOURCE_DIRECTORY ev_pip_package_everest-api-client-gen SOURCE_DIRECTORY)
        message(STATUS "Installing everest-api-client-gen from: ${SOURCE_DIRECTORY}")
        ev_pip_install_local(
            PACKAGE_NAME "everest-api-client-gen"
            PACKAGE_SOURCE_DIRECTORY "${SOURCE_DIRECTORY}"
        )
        unset(EVEREST_API_CLIENT_GEN CACHE)
        find_program(EVEREST_API_CLIENT_GEN everest-api-client-gen HINTS ${EV_ACTIVATE_PYTHON_VENV_PATH_TO_VENV}/bin REQUIRED)
        message(STATUS "Using everest-api-client-gen from: ${EVEREST_API_CLIENT_GEN}")
    endif()

    # Expose the generator's template files so generated clients rebuild when a
    # template changes (used as DEPENDS on the generation custom commands).
    execute_process(
        COMMAND ${EVEREST_API_CLIENT_GEN} --get-templates
        OUTPUT_VARIABLE API_CLIENT_GEN_TEMPLATES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE API_CLIENT_GEN_TEMPLATES_RESULT
    )
    if(API_CLIENT_GEN_TEMPLATES_RESULT)
        message(FATAL_ERROR "Could not get templates from everest-api-client-gen.")
    endif()
    string(REPLACE "\n" ";" API_CLIENT_GEN_TEMPLATES "${API_CLIENT_GEN_TEMPLATES}")
    set(EVEREST_API_CLIENT_GEN "${EVEREST_API_CLIENT_GEN}" PARENT_SCOPE)
    set(EVEREST_API_CLIENT_GEN_TEMPLATES "${API_CLIENT_GEN_TEMPLATES}" PARENT_SCOPE)
endfunction()
