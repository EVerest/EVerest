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
    src/slac_utils.cpp
    src/slac_messages.cpp
    src/HomeplugMessage.cpp
    src/EvseSlacConfig.cpp
    src/MatchingSessionData.cpp

    #fsm
    src/fsm/misc.cpp
    src/fsm/context.cpp
    src/fsm/states/others.cpp
    src/fsm/states/matching.cpp
    src/fsm/states/matching_handle_slac.cpp

    #utils
    src/timer.cpp
)

target_link_libraries(slac_neo
    PUBLIC
        everest::io
        everest::tls
        fsm::fsm
        Boost::boost
)
