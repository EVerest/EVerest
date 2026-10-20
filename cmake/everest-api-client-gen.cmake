# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Makes the `everest-api-client-gen` tool available in the build (installed into
# the build venv when one is active, otherwise found on the system) and exposes
# its source and template files as a variable so generated clients regenerate
# when the generator changes. Mirrors cmake/ev-cli.cmake.

function(setup_everest_api_client_gen)
    get_target_property(SOURCE_DIRECTORY ev_pip_package_everest-api-client-gen SOURCE_DIRECTORY)

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

        message(STATUS "Installing everest-api-client-gen from: ${SOURCE_DIRECTORY}")
        ev_pip_install_local(
            PACKAGE_NAME "everest-api-client-gen"
            PACKAGE_SOURCE_DIRECTORY "${SOURCE_DIRECTORY}"
        )
        unset(EVEREST_API_CLIENT_GEN CACHE)
        find_program(EVEREST_API_CLIENT_GEN everest-api-client-gen HINTS ${EV_ACTIVATE_PYTHON_VENV_PATH_TO_VENV}/bin REQUIRED)
        message(STATUS "Using everest-api-client-gen from: ${EVEREST_API_CLIENT_GEN}")
    endif()

    # The venv install is editable, so these sources are what runs: generated
    # clients rebuild when any of them changes (used as DEPENDS on the
    # generation custom commands).
    file(GLOB_RECURSE API_CLIENT_GEN_SOURCES CONFIGURE_DEPENDS
        "${SOURCE_DIRECTORY}/src/everest_api_client_gen/*.py"
        "${SOURCE_DIRECTORY}/src/everest_api_client_gen/templates/*.j2"
    )
    set(EVEREST_API_CLIENT_GEN "${EVEREST_API_CLIENT_GEN}" PARENT_SCOPE)
    set(EVEREST_API_CLIENT_GEN_TEMPLATES "${API_CLIENT_GEN_SOURCES}" PARENT_SCOPE)
endfunction()
