add_library(slac)
add_library(slac::slac ALIAS slac)

target_include_directories(slac
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_sources(slac
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
    src/fsm/evse/machine.cpp
    src/fsm/evse/init.cpp
    src/fsm/evse/reset.cpp
    src/fsm/evse/reset_chip.cpp
    src/fsm/evse/matching.cpp
    src/fsm/evse/wait_for_link.cpp
    src/fsm/evse/matched.cpp
    src/fsm/ev/machine.cpp

    #utils
    src/timer.cpp
)

target_link_libraries(slac
    PUBLIC
        everest::io
        everest::tls
    PRIVATE
        everest::util
        everest::everest_api_types
        nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
