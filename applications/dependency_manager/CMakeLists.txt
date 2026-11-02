ev_setup_cmake_variables_python_wheel()

ev_declare_pip_package(
    NAME edm
    SOURCE_DIRECTORY .
)

# An edm that is already available, e.g. the one an out-of-tree project runs with, must be recent enough.
set(EVEREST_REQUIRED_EDM_VERSION "0.9.0")

find_program(EVEREST_DEPENDENCY_MANAGER edm)

ev_is_python_venv_active(
    RESULT_VAR IS_PYTHON_VENV_ACTIVE
)

if(EVEREST_DEPENDENCY_MANAGER)
    execute_process(
        COMMAND ${EVEREST_DEPENDENCY_MANAGER} --version
        OUTPUT_VARIABLE EDM_VERSION_FULL
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REPLACE "edm " "" EDM_VERSION "${EDM_VERSION_FULL}")
    if(NOT EDM_VERSION VERSION_GREATER_EQUAL EVEREST_REQUIRED_EDM_VERSION)
        message(FATAL_ERROR "edm version ${EVEREST_REQUIRED_EDM_VERSION} or higher is required, but ${EVEREST_DEPENDENCY_MANAGER} is version '${EDM_VERSION}'. Please upgrade it, e.g. with: python3 -m pip install ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    message(STATUS "Using edm ${EDM_VERSION} from: ${EVEREST_DEPENDENCY_MANAGER}")
elseif(NOT ${IS_PYTHON_VENV_ACTIVE})
    message(WARNING "Python venv is not active. Please ensure that edm is available in your environment.")
else()
    ev_install_pip_package(
        NAME "edm"
        EDITABLE
    )
    unset(EVEREST_DEPENDENCY_MANAGER CACHE)
    find_program(EVEREST_DEPENDENCY_MANAGER edm HINTS ${EV_ACTIVATE_PYTHON_VENV_PATH_TO_VENV}/bin REQUIRED)
    message(STATUS "Using edm from: ${EVEREST_DEPENDENCY_MANAGER}")
endif()
