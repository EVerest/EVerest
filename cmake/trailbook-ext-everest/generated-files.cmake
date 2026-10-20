# Generated documentation files and directories are recorded across configure
# runs so that a file whose input disappeared (a removed interface, type, module page or
# handwritten page) is deleted instead of lingering in the instance source
# directory, where the glob toctrees would keep listing it.

# This function is for internal use by the generators of this package.
#
# It records FILES as generated for the trailbook and, once per trailbook,
# schedules the removal of stale files for the end of the configure run.
#
# Usage:
# _trailbook_ev_register_generated_files(
#     TRAILBOOK_NAME <trailbook_name>
#     FILES <file>...
# )
function(_trailbook_ev_register_generated_files)
    set(options)
    set(one_value_args
        TRAILBOOK_NAME
    )
    set(multi_value_args
        FILES
    )
    cmake_parse_arguments(
        "args"
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )
    if(NOT args_TRAILBOOK_NAME)
        message(FATAL_ERROR "_trailbook_ev_register_generated_files: TRAILBOOK_NAME argument is required")
    endif()

    get_property(TRAILBOOKS GLOBAL PROPERTY TRAILBOOK_EV_TRAILBOOKS_WITH_GENERATED_FILES)
    if(NOT args_TRAILBOOK_NAME IN_LIST TRAILBOOKS)
        set_property(GLOBAL APPEND PROPERTY TRAILBOOK_EV_TRAILBOOKS_WITH_GENERATED_FILES ${args_TRAILBOOK_NAME})
        if(NOT TRAILBOOKS)
            # arguments of a deferred call are evaluated when it runs, so the
            # trailbook names are taken from the property instead
            cmake_language(
                DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
                CALL _trailbook_ev_remove_stale_generated_files
            )
        endif()
    endif()
    set_property(GLOBAL APPEND PROPERTY TRAILBOOK_EV_GENERATED_FILES_${args_TRAILBOOK_NAME} ${args_FILES})
endfunction()

# This function is for internal use only.
#
# It runs at the end of the configure run: for every trailbook with
# registered files it deletes the files recorded by the previous run that no
# generator registered this time, then records the current set in the
# trailbook's binary directory.
function(_trailbook_ev_remove_stale_generated_files)
    get_property(TRAILBOOKS GLOBAL PROPERTY TRAILBOOK_EV_TRAILBOOKS_WITH_GENERATED_FILES)
    foreach(TRAILBOOK_NAME IN LISTS TRAILBOOKS)
        get_target_property(
            TRAILBOOK_CURRENT_BINARY_DIR
            trailbook_${TRAILBOOK_NAME}
            TRAILBOOK_CURRENT_BINARY_DIR
        )
        set(RECORD_FILE "${TRAILBOOK_CURRENT_BINARY_DIR}/trailbook_ev_generated_files.txt")
        get_property(CURRENT_FILES GLOBAL PROPERTY TRAILBOOK_EV_GENERATED_FILES_${TRAILBOOK_NAME})

        if(EXISTS "${RECORD_FILE}")
            file(STRINGS "${RECORD_FILE}" PREVIOUS_FILES)
            foreach(FILE_PATH IN LISTS PREVIOUS_FILES)
                if(NOT FILE_PATH IN_LIST CURRENT_FILES AND EXISTS "${FILE_PATH}")
                    message(STATUS "Removing stale generated doc file: ${FILE_PATH}")
                    file(REMOVE_RECURSE "${FILE_PATH}")
                endif()
            endforeach()
        endif()

        list(REMOVE_DUPLICATES CURRENT_FILES)
        list(SORT CURRENT_FILES)
        list(JOIN CURRENT_FILES "\n" RECORD_CONTENT)
        string(APPEND RECORD_CONTENT "\n")
        set(PREVIOUS_CONTENT "")
        if(EXISTS "${RECORD_FILE}")
            file(READ "${RECORD_FILE}" PREVIOUS_CONTENT)
        endif()
        if(NOT PREVIOUS_CONTENT STREQUAL RECORD_CONTENT)
            file(WRITE "${RECORD_FILE}" "${RECORD_CONTENT}")
        endif()
        # A removed page leaves no newer input behind; the record, rewritten
        # only when the set changes, makes the site rebuild in that case.
        set_property(
            TARGET
                trailbook_${TRAILBOOK_NAME}
            APPEND
            PROPERTY
                ADDITIONAL_DEPS_STAGE_BUILD_SPHINX_BEFORE
                    "${RECORD_FILE}"
        )
    endforeach()
endfunction()
