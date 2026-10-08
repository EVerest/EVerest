# checks EVEREST_PACKAGES and sets EVEREST_BUILD_<PACKAGE>
set(_everest_packages base iso15118 ocpp core runtime)

function(_ev_check_packages LIST_VAR)
    set(unknown ${${LIST_VAR}})
    list(REMOVE_ITEM unknown ${_everest_packages})
    if(unknown)
        message(FATAL_ERROR "${LIST_VAR}: unknown package(s) ${unknown}")
    endif()
endfunction()

_ev_check_packages(EVEREST_PACKAGES)

foreach(_pkg IN LISTS _everest_packages)
    string(TOUPPER ${_pkg} _PKG)
    if(_pkg IN_LIST EVEREST_PACKAGES)
        set(EVEREST_BUILD_${_PKG} ON)
    else()
        set(EVEREST_BUILD_${_PKG} OFF)
    endif()
endforeach()
