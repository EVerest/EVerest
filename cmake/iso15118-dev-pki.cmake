# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

# Generates the ISO 15118-2 development PKI and checks that every file the ISO 15118
# configurations load exists. Runs create_certs.sh from a copy of the vendored assets
# in DEV_PKI_WORKING_DIR (recreated) and installs into DEV_PKI_INSTALL_DIR/config/certs.
#
# Used from configure through include(), and as
#   cmake -DDEV_PKI_WORKING_DIR=<dir> -DDEV_PKI_INSTALL_DIR=<dir> -P iso15118-dev-pki.cmake

function(iso15118_generate_dev_pki working_dir install_dir)
    if(NOT working_dir OR NOT install_dir)
        message(FATAL_ERROR "iso15118-dev-pki.cmake needs DEV_PKI_WORKING_DIR and DEV_PKI_INSTALL_DIR")
    endif()
    set(assets "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets/iso15118-pki")
    file(REMOVE_RECURSE "${working_dir}")
    file(COPY "${assets}/" DESTINATION "${working_dir}")
    execute_process(
        COMMAND "${working_dir}/create_certs.sh" -v iso-2 -i "${install_dir}"
        WORKING_DIRECTORY "${working_dir}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )

    set(certs "${install_dir}/config/certs")
    file(STRINGS "${assets}/expected-files.txt" expected REGEX "^[^#]")
    set(missing "")
    foreach(file IN LISTS expected)
        set(size 0)
        if(EXISTS "${certs}/${file}")
            file(SIZE "${certs}/${file}" size)
        endif()
        if(size EQUAL 0)
            string(APPEND missing "\n  ${file}")
        endif()
    endforeach()
    if(missing)
        set(missing "Missing or empty under ${certs}:${missing}\n")
    endif()

    if(NOT result EQUAL 0 OR missing)
        message(FATAL_ERROR
            "ISO 15118-2 development certificate generation failed, create_certs.sh exited with ${result}.\n"
            "${missing}"
            "create_certs.sh output:\n${output}\n"
            "Turn the generation off with -DISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES=OFF.")
    endif()
    message(STATUS "ISO 15118-2 development certificates generated")
endfunction()

iso15118_generate_dev_pki("${DEV_PKI_WORKING_DIR}" "${DEV_PKI_INSTALL_DIR}")
