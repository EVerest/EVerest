# This function copies the handwritten documentation of a module into its
# reference directory, where the page generated from the manifest includes
# it. index.rst is copied as index.inc.
#
# Arguments:
#   TRAILBOOK_NAME (required): Name of the trailbook instance.
#   MODULE_NAME (required): The name of the module.
#   HANDWRITTEN_DIR (required): Absolute path to the module's docs directory.
# Usage:
# trailbook_ev_add_module_handwritten_doc(
#   TRAILBOOK_NAME <trailbook_name>
#   MODULE_NAME <module_name>
#   HANDWRITTEN_DIR <absolute_path_to_handwritten_docs_directory>
# )
function(trailbook_ev_add_module_handwritten_doc)
    cmake_parse_arguments("args" "" "TRAILBOOK_NAME;MODULE_NAME;HANDWRITTEN_DIR" "" ${ARGN})
    _trailbook_ev_require_trailbook(${CMAKE_CURRENT_FUNCTION} "${args_TRAILBOOK_NAME}")
    if(NOT args_MODULE_NAME)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION}: MODULE_NAME argument is required")
    endif()
    _trailbook_ev_require_path(${CMAKE_CURRENT_FUNCTION} HANDWRITTEN_DIR "${args_HANDWRITTEN_DIR}")

    get_target_property(
        TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
        trailbook_${args_TRAILBOOK_NAME}
        TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
    )
    file(RELATIVE_PATH RELATIVE_PATH_HANDWRITTEN_DIR "${CMAKE_SOURCE_DIR}/modules" "${args_HANDWRITTEN_DIR}")
    get_filename_component(RELATIVE_PATH_MODULE "${RELATIVE_PATH_HANDWRITTEN_DIR}" DIRECTORY)
    set(DOC_DIRECTORY "${TRAILBOOK_INSTANCE_SOURCE_DIRECTORY}/reference/modules/${RELATIVE_PATH_MODULE}")

    file(GLOB_RECURSE SOURCE_FILES RELATIVE "${args_HANDWRITTEN_DIR}" CONFIGURE_DEPENDS "${args_HANDWRITTEN_DIR}/*")
    set(COPIED_FILES "")
    foreach(SOURCE_FILE IN LISTS SOURCE_FILES)
        set(SRC_FILE_PATH "${args_HANDWRITTEN_DIR}/${SOURCE_FILE}")
        if(IS_DIRECTORY "${SRC_FILE_PATH}")
            continue()
        endif()
        get_filename_component(RELATIVE_SUBDIR "${SOURCE_FILE}" DIRECTORY)
        get_filename_component(FILE_NAME "${SOURCE_FILE}" NAME)
        if("${FILE_NAME}" STREQUAL "index.rst")
            set(FILE_NAME "index.inc")
        endif()
        set(DEST_DIR "${DOC_DIRECTORY}")
        if(NOT "${RELATIVE_SUBDIR}" STREQUAL "")
            string(APPEND DEST_DIR "/${RELATIVE_SUBDIR}")
        endif()
        set(DEST_FILE_PATH "${DEST_DIR}/${FILE_NAME}")

        add_custom_command(
            OUTPUT "${DEST_FILE_PATH}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEST_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${SRC_FILE_PATH}" "${DEST_FILE_PATH}"
            DEPENDS "${SRC_FILE_PATH}"
            COMMENT "Processing doc file: ${SOURCE_FILE} -> ${RELATIVE_SUBDIR}/${FILE_NAME}"
            VERBATIM
        )
        list(APPEND COPIED_FILES "${DEST_FILE_PATH}")
    endforeach()

    if(COPIED_FILES)
        _trailbook_ev_add_generated(
            TRAILBOOK_NAME ${args_TRAILBOOK_NAME}
            TARGET trailbook_${args_TRAILBOOK_NAME}_handwritten_doc_module_${args_MODULE_NAME}
            FILES ${COPIED_FILES}
        )
    endif()
endfunction()
