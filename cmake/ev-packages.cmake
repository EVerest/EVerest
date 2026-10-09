# checks EVEREST_PACKAGES / EVEREST_TEST_PACKAGES and sets EVEREST_BUILD_<PACKAGE> / EVEREST_TEST_<PACKAGE>
set(_everest_packages base iso15118 ocpp core runtime)

function(_ev_check_packages LIST_VAR)
    set(unknown ${${LIST_VAR}})
    list(REMOVE_ITEM unknown ${_everest_packages})
    if(unknown)
        message(FATAL_ERROR "${LIST_VAR}: unknown package(s) ${unknown}")
    endif()
endfunction()

_ev_check_packages(EVEREST_PACKAGES)
_ev_check_packages(EVEREST_TEST_PACKAGES)

foreach(_pkg IN LISTS _everest_packages)
    string(TOUPPER ${_pkg} _PKG)
    if(_pkg IN_LIST EVEREST_PACKAGES)
        set(EVEREST_BUILD_${_PKG} ON)
    else()
        set(EVEREST_BUILD_${_PKG} OFF)
    endif()
    if(EVEREST_CORE_BUILD_TESTING AND EVEREST_BUILD_${_PKG} AND _pkg IN_LIST EVEREST_TEST_PACKAGES)
        set(EVEREST_TEST_${_PKG} ON)
    else()
        set(EVEREST_TEST_${_PKG} OFF)
    endif()
endforeach()

# an installed iso15118, ocpp or core brings its own installed base; mixing it with this tree's base is refused
if(EVEREST_BUILD_BASE AND EVEREST_BUILD_RUNTIME)
    foreach(_pkg IN ITEMS iso15118 ocpp core)
        if(NOT _pkg IN_LIST EVEREST_PACKAGES)
            message(FATAL_ERROR "EVEREST_PACKAGES: base is built here, so ${_pkg} must be built here too "
                "(an installed ${_pkg} would bring its own base); add ${_pkg} or take base from the installed prefix as well")
        endif()
    endforeach()
endif()
