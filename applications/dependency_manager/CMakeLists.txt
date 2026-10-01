ev_setup_cmake_variables_python_wheel()

ev_declare_pip_package(
    NAME edm
    SOURCE_DIRECTORY .
)

ev_is_python_venv_active(
    RESULT_VAR IS_PYTHON_VENV_ACTIVE
)

if(NOT ${IS_PYTHON_VENV_ACTIVE})
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
