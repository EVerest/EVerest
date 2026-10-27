# This function generates an RST file from an interface definition file.
#
# Arguments:
#   TRAILBOOK_NAME (required): Name of the trailbook instance.
#   INTERFACE_FILE (required): Absolute path to the interface definition file.
# Usage:
# trailbook_ev_generate_rst_from_interface(
#     TRAILBOOK_NAME <trailbook_name>
#     INTERFACE_FILE <path_to_interface_definition_file>
# )
function(trailbook_ev_generate_rst_from_interface)
    cmake_parse_arguments("args" "" "TRAILBOOK_NAME;INTERFACE_FILE" "" ${ARGN})
    _trailbook_ev_require_trailbook(${CMAKE_CURRENT_FUNCTION} "${args_TRAILBOOK_NAME}")
    _trailbook_ev_require_path(${CMAKE_CURRENT_FUNCTION} INTERFACE_FILE "${args_INTERFACE_FILE}")

    get_target_property(
        TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
        trailbook_${args_TRAILBOOK_NAME}
        TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
    )
    get_filename_component(INTERFACE_NAME ${args_INTERFACE_FILE} NAME_WE)
    set(GENERATED_FILE "${TRAILBOOK_INSTANCE_SOURCE_DIRECTORY}/reference/interfaces/${INTERFACE_NAME}.rst")
    set(TEMPLATES_DIRECTORY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates")
    # The error descriptions referenced by the interface are rendered into
    # the page (--errors-yaml-path below).
    file(GLOB ERROR_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/errors/*.yaml")

    add_custom_command(
        OUTPUT
            ${GENERATED_FILE}
        DEPENDS
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/process_template.py
            ${args_INTERFACE_FILE}
            ${ERROR_FILES}
            ${TEMPLATES_DIRECTORY}/interface.rst.jinja
            ${TEMPLATES_DIRECTORY}/macros.jinja
        COMMENT
            "Generating RST file ${GENERATED_FILE} from interface definition ${args_INTERFACE_FILE}"
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/process_template.py
            --template-dir "${TEMPLATES_DIRECTORY}"
            --template-file "${TEMPLATES_DIRECTORY}/interface.rst.jinja"
            --name "${INTERFACE_NAME}"
            --data-file "${args_INTERFACE_FILE}"
            --errors-yaml-path "${CMAKE_SOURCE_DIR}/errors/"
            --target-file "${GENERATED_FILE}"
    )
    _trailbook_ev_add_generated(
        TRAILBOOK_NAME ${args_TRAILBOOK_NAME}
        TARGET trailbook_${args_TRAILBOOK_NAME}_generate_rst_from_interface_${INTERFACE_NAME}
        FILES ${GENERATED_FILE}
    )
endfunction()
