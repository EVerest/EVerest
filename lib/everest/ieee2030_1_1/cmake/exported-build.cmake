# download everest-core (source of util)
include(ExternalProject)
ExternalProject_Add(
    everest-core-src
    DOWNLOAD_DIR "everest-core/src"
    GIT_REPOSITORY "https://github.com/EVerest/everest-core.git"
    GIT_TAG "main"
    TIMEOUT 30
    LOG_DOWNLOAD ON
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

# util is header-only
ExternalProject_Get_Property(everest-core-src SOURCE_DIR)
set(UTIL_INCLUDE_DIR "${SOURCE_DIR}/lib/everest/util/include")

# workaround for https://gitlab.kitware.com/cmake/cmake/-/issues/15052
file(MAKE_DIRECTORY ${UTIL_INCLUDE_DIR})

add_library(everest_util INTERFACE IMPORTED GLOBAL)
add_library(everest::util ALIAS everest_util)
set_property(TARGET everest_util PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${UTIL_INCLUDE_DIR})
add_dependencies(everest_util everest-core-src)
