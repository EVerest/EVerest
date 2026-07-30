ev_create_pip_install_targets(
    PACKAGE_NAME
        "everest-management-api-cli"
)

ev_create_python_wheel_targets(
    PACKAGE_NAME
        "everest-management-api-cli"
)

# automatically install the CLI into the venv if it is active, like everest-testing
ev_is_python_venv_active(
    RESULT_VAR IS_PYTHON_VENV_ACTIVE
)
if(IS_PYTHON_VENV_ACTIVE)
    get_target_property(SOURCE_DIRECTORY ev_pip_package_everest-management-api-cli SOURCE_DIRECTORY)
    message(STATUS "Installing everest-management-api-cli from: ${SOURCE_DIRECTORY}")
    ev_pip_install_local(
        PACKAGE_NAME "everest-management-api-cli"
        PACKAGE_SOURCE_DIRECTORY "${SOURCE_DIRECTORY}"
    )
endif()
