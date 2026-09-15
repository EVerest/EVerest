# Minimal CMake config for header-only json-rpc-cxx

# Define INTERFACE library target
add_library(json-rpc-cxx INTERFACE)

# Compute the include and library directories relative to this file.

get_filename_component(_JSONRPCCXX_CMAKEDIR "${CMAKE_CURRENT_LIST_DIR}/../" REALPATH)
message(STATUS "Found json-rpc-cxx cmake config in ${CMAKE_CURRENT_LIST_DIR}, CMake dir is ${_JSONRPCCXX_CMAKEDIR}")

# We will get deployed in ${INSTALL_PREFIX}/lib/cmake/json-rpc-cxx
# and need to find ${INSTALL_PREFIX}/include.
get_filename_component(_JSONRPCCXX_INCLUDEDIR
    "${_JSONRPCCXX_CMAKEDIR}/../../include/"
    ABSOLUTE
)
message(STATUS "Setting json-rpc-cxx include dir to ${_JSONRPCCXX_INCLUDEDIR}")
# get_filename_component(_JSONRPCCXX_LIBDIR "${_JSONRPCCXX_CMAKEDIR}/../" ABSOLUTE)

# Add include directory
target_include_directories(json-rpc-cxx INTERFACE
    ${_JSONRPCCXX_INCLUDEDIR}
)

# Ensure dependency on nlohmann_json is pulled in
include(CMakeFindDependencyMacro)
find_dependency(nlohmann_json)
