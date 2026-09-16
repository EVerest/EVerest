ev_add_module(Auth)
ev_add_module(Evse15118D20)
ev_add_module(EvseManager)
ev_add_module(EvseSecurity)
ev_add_module(EvseSlac)
ev_add_module(EvseV2G)
ev_add_module(IsoMux)
ev_add_module(Iso15118InternetVas)
ev_add_module(McsDataLink)
ev_add_module(OCPP)
ev_add_module(OCPP201)
ev_add_module(OCPPmulti)
if(${EVEREST_ENABLE_RS_SUPPORT})
    ev_add_module(RsEvseManager)

    if(EVEREST_CORE_BUILD_TESTING)
        # ev_add_rs_module() does not add_subdirectory() the module, so there is no
        # per-module CMakeLists.txt hook to register cargo tests from. Register the
        # test here instead, with the same environment build_rust_modules uses.
        # The linker and the rustflags come from the workspace's generated
        # .cargo/config.toml, which is why neither is passed here: a --config on the
        # command line overrides that file, and cargo then treats this as a different
        # build and recompiles the whole workspace on every test run.
        # LD_LIBRARY_PATH is needed to run the resulting binary against libframework.
        add_test(
            NAME RsEvseManager_unit_tests
            COMMAND ${CMAKE_COMMAND} -E env
                "EVEREST_CORE_ROOT=${CMAKE_SOURCE_DIR}"
                "EVEREST_RS_LINK_DEPENDENCIES=${RUST_LINK_DEPENDENCIES_FILE}"
                "LD_LIBRARY_PATH=${PROJECT_BINARY_DIR}/lib/everest/framework/lib"
                ${CARGO_EXECUTABLE} test -p RsEvseManager
                    --target $<TARGET_PROPERTY:build_rust_modules,RUST_TARGET_TRIPLE>
            WORKING_DIRECTORY ${RUST_WORKSPACE_DIR}
        )
    endif()
endif()
ev_add_module(StaticISO15118VASProvider)
