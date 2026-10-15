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

ev_add_pip_package(
    NAME everest-testing
    SOURCE_DIRECTORY everest-testing
)

add_subdirectory(everest-testing)

# interactive client for the EVerest API modules; generates one python client
# package per API from its AsyncAPI spec using the in-tree Python generator
# everest-api-client-gen (no npm/asyncapi toolchain)
if(EVEREST_ENABLE_PY_SUPPORT AND EVEREST_BUILD_API_CLIENTS)
    ev_add_pip_package(
        NAME everest-api-client-gen
        SOURCE_DIRECTORY everest-api-client-gen
    )
    add_subdirectory(everest-api-client)
endif()

# client for the management APIs; the management API tests import it
if(EVEREST_CORE_BUILD_TESTING)
    ev_add_pip_package(
        NAME everest-management-api-cli
        SOURCE_DIRECTORY everest-management-api-cli
    )
    add_subdirectory(everest-management-api-cli)
endif()
