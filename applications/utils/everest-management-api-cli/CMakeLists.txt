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
    ev_install_pip_package(
        NAME "everest-management-api-cli"
        EDITABLE
    )
endif()
