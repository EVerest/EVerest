# detect if we try to build inside everest-core and local dependencies are available
message(STATUS "Attempting build with autodetected local dependencies")

get_filename_component(EVC_EVEREST_LIB_DIR ${PROJECT_SOURCE_DIR} DIRECTORY)

function(ev_register_library_target NAME)
endfunction()

set(EVC_UTIL_DIR "${EVC_EVEREST_LIB_DIR}/util")
if (EVC_UTIL_DIR AND NOT DISABLE_IEEE2030_LOCAL_DEPENDENCIES)
    message(STATUS "Detected util in ${EVC_UTIL_DIR}, if you do not want this set -DDISABLE_IEEE2030_LOCAL_DEPENDENCIES=ON")
    if (BUILD_TESTING)
        message(STATUS "Setting BUILD_TESTING temporary to false")
        set(CACHE_BUILD_TESTING ON)
        set(BUILD_TESTING OFF)
    endif()
    add_subdirectory("${EVC_UTIL_DIR}" util)
    if (CACHE_BUILD_TESTING)
        set(BUILD_TESTING ON)
    endif()
endif()

get_filename_component(EVC_LIB_DIR ${EVC_EVEREST_LIB_DIR} DIRECTORY)
get_filename_component(EVC_DIR ${EVC_LIB_DIR} DIRECTORY)
set(EVC_EDM_DIR "${EVC_DIR}/applications/dependency_manager")

# use edm from everest-core
add_subdirectory("${EVC_EDM_DIR}" edm_tool)
