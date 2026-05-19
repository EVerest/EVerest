
# Wrapper around add_trailbook() that automatically generates and installs
# a versions.json file into the multiversion root directory alongside the HTML output.
#
# Accepts exactly the same arguments as add_trailbook().
#
# Usage:
#   add_trailbook_ev(
#       NAME <trailbook_name>
#       [STEM_DIRECTORY <stem_directory>]
#       [REQUIREMENTS_TXT <requirements_txt_path>]
#       INSTANCE_NAME <instance_name>
#       [DEPLOYED_DOCS_REPO_URL <deployed_docs_repo_url>]
#       [DEPLOYED_DOCS_REPO_BRANCH <deployed_docs_repo_branch>]
#   )
function(add_trailbook_ev)
    # Parse only NAME here so we can refer to it; forward all args to add_trailbook()
    cmake_parse_arguments("_ev" "" "NAME" "" ${ARGN})

    if("${_ev_NAME}" STREQUAL "")
        message(FATAL_ERROR "add_trailbook_ev: NAME argument is required")
    endif()

    add_trailbook(${ARGN})

    # -- Locate artefacts via target properties set by add_trailbook() --
    get_target_property(
        _TRAILBOOK_CURRENT_BINARY_DIR
        trailbook_${_ev_NAME}
        TRAILBOOK_CURRENT_BINARY_DIR
    )
    get_target_property(
        _TRAILBOOK_BUILD_DIRECTORY
        trailbook_${_ev_NAME}
        TRAILBOOK_BUILD_DIRECTORY
    )
    get_target_property(
        _TRAILBOOK_METADATA_YAML_FILE
        trailbook_${_ev_NAME}
        TRAILBOOK_METADATA_YAML_FILE
    )
    get_target_property(
        _DEPS_STAGE_BUILD_SPHINX_AFTER
        trailbook_${_ev_NAME}
        DEPS_STAGE_BUILD_SPHINX_AFTER
    )

    set(_VERSIONS_JSON_FILE     "${_TRAILBOOK_CURRENT_BINARY_DIR}/versions_${_ev_NAME}.json")
    set(_VERSIONS_JSON_INSTALLED "${_TRAILBOOK_BUILD_DIRECTORY}/versions.json")
    set(_CHECK_DONE_FILE        "${_TRAILBOOK_CURRENT_BINARY_DIR}/create_versions_json.check_done")

    add_custom_command(
        OUTPUT
            ${_CHECK_DONE_FILE}
        DEPENDS
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/create_versions_json.py
            ${_TRAILBOOK_METADATA_YAML_FILE}
            trailbook_${_ev_NAME}_stage_build_sphinx_after
            ${_DEPS_STAGE_BUILD_SPHINX_AFTER}
        COMMENT
            "Trailbook: ${_ev_NAME} - Creating and installing versions.json"
        COMMAND
            ${CMAKE_COMMAND} -E rm -f ${_VERSIONS_JSON_FILE}
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/create_versions_json.py
            --metadata-yaml-path "${_TRAILBOOK_METADATA_YAML_FILE}"
            --output-path "${_VERSIONS_JSON_FILE}"
        COMMAND
            ${CMAKE_COMMAND} -E rm -f ${_VERSIONS_JSON_INSTALLED}
        COMMAND
            ${CMAKE_COMMAND} -E copy
            ${_VERSIONS_JSON_FILE}
            ${_VERSIONS_JSON_INSTALLED}
        COMMAND
            ${CMAKE_COMMAND} -E touch ${_CHECK_DONE_FILE}
    )

    add_custom_target(
        trailbook_${_ev_NAME}_create_versions_json
        DEPENDS
            ${_CHECK_DONE_FILE}
        COMMENT
            "Install versions.json for trailbook ${_ev_NAME}"
    )

    set_property(
        TARGET
            trailbook_${_ev_NAME}
        APPEND
        PROPERTY
            ADDITIONAL_DEPS_STAGE_POSTPROCESS_SPHINX_BEFORE
            trailbook_${_ev_NAME}_create_versions_json
            ${_CHECK_DONE_FILE}
    )
endfunction()
