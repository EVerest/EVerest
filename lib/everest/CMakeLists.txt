# Conditionally include libraries based on EVEREST_INCLUDE_LIBS / EVEREST_EXCLUDE_LIBS.
# When neither is set, all libraries are built (backwards compatible).
# The library list is defined in cmake/ev-lib-dependencies.cmake (EVEREST_LIB_SUBDIRS).

foreach(_lib IN LISTS EVEREST_LIB_SUBDIRS)
    ev_should_build_lib(${_lib} _should_build)
    if(_should_build)
        add_subdirectory(${_lib})
    endif()
endforeach()

if(EVEREST_DEPENDENCY_ENABLED_EVEREST_GPIO)
    ev_should_build_lib(gpio _should_build)
    if(_should_build)
        add_subdirectory(gpio)
    endif()
endif()

if(EVEREST_DEPENDENCY_ENABLED_SDBUS_CPP)
    ev_should_build_lib(system _should_build)
    if(_should_build)
        add_subdirectory(system)
    endif()
endif()
