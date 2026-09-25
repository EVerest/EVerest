cmake_minimum_required(VERSION 3.14)

project(everest-utils
    VERSION 0.7.3
    DESCRIPTION "A collection of utilities for the EVerest project"
    LANGUAGES CXX C
)

find_package(everest-cmake 0.5 REQUIRED
    PATHS ../everest-cmake
)

ev_setup_cmake_variables_python_wheel()

ev_add_pip_package(
    NAME ev-dev-tools
    SOURCE_DIRECTORY ev-dev-tools
)

if(EVEREST_CORE_BUILD_TESTING)
    add_test(
        NAME ev-dev-tools_pytest
        COMMAND ${EV_ACTIVATE_PYTHON_VENV_PATH_TO_VENV}/bin/python -m pytest -p no:cacheprovider
            ${CMAKE_CURRENT_SOURCE_DIR}/ev-dev-tools/tests
    )
    set_tests_properties(ev-dev-tools_pytest
        PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/ev-dev-tools/src"
    )
endif()

ev_add_pip_package(
    NAME everest-testing
    SOURCE_DIRECTORY everest-testing
)

add_subdirectory(everest-testing)

# client for the management APIs; the management API tests import it
if(EVEREST_CORE_BUILD_TESTING)
    ev_add_pip_package(
        NAME everest-management-api-cli
        SOURCE_DIRECTORY everest-management-api-cli
    )
    add_subdirectory(everest-management-api-cli)
endif()
