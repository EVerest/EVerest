#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
    main/fsm_controller_neo.cpp
)

# Which SLAC library the module is built against. Both implement the same protocol and present
# nearly the same contract; main/slac_backend.hpp holds the differences. See that header.
set(EVEREST_SLAC_BACKEND "neo" CACHE STRING "SLAC library to build EvseSlacNeo against")
set_property(CACHE EVEREST_SLAC_BACKEND PROPERTY STRINGS neo meow)

if(EVEREST_SLAC_BACKEND STREQUAL "neo")
    set(EVEREST_SLAC_BACKEND_LIB slac::neo)
elseif(EVEREST_SLAC_BACKEND STREQUAL "meow")
    set(EVEREST_SLAC_BACKEND_LIB slac::meow)
    target_compile_definitions(${MODULE_NAME} PRIVATE EVEREST_SLAC_BACKEND_MEOW)
else()
    message(FATAL_ERROR "EVEREST_SLAC_BACKEND must be one of: neo meow (got '${EVEREST_SLAC_BACKEND}')")
endif()

target_link_libraries(${MODULE_NAME}
    PRIVATE
        everest::everest_api_types
        ${EVEREST_SLAC_BACKEND_LIB}
        everest::io
)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "main/slacImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
# insert other things like install cmds etc here
# CM_AMP_MAP amplitude map is loaded from an operator YAML file (see slacImpl.cpp).
target_link_libraries(${MODULE_NAME} PRIVATE everest::yaml)

if(EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
