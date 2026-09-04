add_library(slac_neo)
add_library(slac::neo ALIAS slac_neo)

find_package(Boost REQUIRED )

target_include_directories(slac_neo
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_sources(slac_neo
    PRIVATE
    src/slac_event.cpp
    src/slac_socket.cpp
    src/slac_fsm.cpp
    src/ev_slac_fsm.cpp
    src/slac_utils.cpp
    src/slac_messages.cpp
    src/HomeplugMessage.cpp
    src/EvseSlacConfig.cpp
    src/MatchingSessionData.cpp
    src/telemetry.cpp
    src/fsm/misc.cpp
    src/fsm/ev/context.cpp
    src/fsm/evse/context.cpp
    src/fsm/evse/validate_handler.cpp
    src/fsm/evse/amp_map_handler.cpp

    #utils
    src/timer.cpp
)

# The msm machines are instantiated in slac_fsm.cpp and ev_slac_fsm.cpp. A mistake in a guard or
# action functor otherwise produces tens of kilobytes of "required from" template backtrace; one
# frame is enough to locate the offending Row. Diagnostics only, no effect on the generated code.
# Set to 0 for the full backtrace when the trimmed one does not show the instantiating Row.
set(SLAC_NEO_TEMPLATE_BACKTRACE_LIMIT 1 CACHE STRING
    "Template instantiation frames GCC/Clang print for errors in the slac_neo msm machines (0 = all)")
target_compile_options(slac_neo
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ftemplate-backtrace-limit=${SLAC_NEO_TEMPLATE_BACKTRACE_LIMIT}>
)

target_link_libraries(slac_neo
    PUBLIC
        everest::io
        everest::tls
        Boost::boost
    PRIVATE
        everest::everest_api_types
        nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
