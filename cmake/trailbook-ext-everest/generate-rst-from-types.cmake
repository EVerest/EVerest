# This function generates an RST file from a types definition file.
#
# Arguments:
#   TRAILBOOK_NAME (required): Name of the trailbook instance.
#   TYPES_FILE (required): Absolute path to the types definition file.
# Usage:
# trailbook_ev_generate_rst_from_types(
#   TRAILBOOK_NAME <trailbook_name>
#   TYPES_FILE <absolute_path_to_types_definition_file>
# )
function(trailbook_ev_generate_rst_from_types)
    cmake_parse_arguments("args" "" "TRAILBOOK_NAME;TYPES_FILE" "" ${ARGN})
    _trailbook_ev_require_trailbook(${CMAKE_CURRENT_FUNCTION} "${args_TRAILBOOK_NAME}")
    _trailbook_ev_require_path(${CMAKE_CURRENT_FUNCTION} TYPES_FILE "${args_TYPES_FILE}")

    get_target_property(
        TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
        trailbook_${args_TRAILBOOK_NAME}
        TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
    )
    get_filename_component(TYPES_NAME ${args_TYPES_FILE} NAME_WE)
    set(GENERATED_FILE "${TRAILBOOK_INSTANCE_SOURCE_DIRECTORY}/reference/types/${TYPES_NAME}.rst")
    set(TEMPLATES_DIRECTORY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates")

    add_custom_command(
        OUTPUT
            ${GENERATED_FILE}
        DEPENDS
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/process_template.py
            ${args_TYPES_FILE}
            ${TEMPLATES_DIRECTORY}/types.rst.jinja
            ${TEMPLATES_DIRECTORY}/macros.jinja
        COMMENT
            "Generating RST file ${GENERATED_FILE} from types definition ${args_TYPES_FILE}"
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/process_template.py
            --template-dir "${TEMPLATES_DIRECTORY}"
            --template-file "${TEMPLATES_DIRECTORY}/types.rst.jinja"
            --name "${TYPES_NAME}"
            --data-file "${args_TYPES_FILE}"
            --target-file "${GENERATED_FILE}"
    )
    _trailbook_ev_add_generated(
        TRAILBOOK_NAME ${args_TRAILBOOK_NAME}
        TARGET trailbook_${args_TRAILBOOK_NAME}_generate_rst_from_types_${TYPES_NAME}
        FILES ${GENERATED_FILE}
    )
endfunction()
