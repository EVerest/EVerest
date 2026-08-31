#
# SLAC state machine built on fsm::v2 (everest::util), in the libiso15118 style: one class per
# state, feed() dispatching to the next state.
#
# The library owns everything it needs. Inside it three areas are kept apart by directory:
#
#   protocol/  the wire format and its helpers - may not reference a state machine
#   evse/, ev/ the state machines - may include protocol/ and time.hpp, and nothing else
#   io/        the transport - a dead end that nothing else here includes, so the machines
#              cannot reach it even by accident; meant to be lifted into the SLAC module later,
#              which is then a folder move and four lines out of target_sources
#
# The protocol sources are a copy of slac::neo's, which stays exactly as upstream has it so the two
# implementations can be diffed against each other. No target may link both libraries: they define
# the same protocol symbols.
#
add_library(slac_meow)
add_library(slac::meow ALIAS slac_meow)

target_include_directories(slac_meow
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_sources(slac_meow
    PRIVATE
    # protocol - the definitions
    src/protocol/messages.cpp
    src/protocol/homeplug_message.cpp
    src/protocol/utils.cpp
    src/protocol/builders.cpp
    src/protocol/format.cpp

    # shared by both machines
    src/status.cpp

    # EVSE side
    src/evse/states.cpp
    src/evse/fsm.cpp
    src/evse/config.cpp
    src/evse/session_data.cpp
    src/evse/link_status.cpp
    src/evse/state/init.cpp
    src/evse/state/reset.cpp
    src/evse/state/reset_chip.cpp
    src/evse/state/idle.cpp
    src/evse/state/matching.cpp
    src/evse/state/session.cpp
    src/evse/state/wait_for_link.cpp
    src/evse/state/matched.cpp
    src/evse/state/failed.cpp

    # transport - not used by the state machines above
    src/io/socket.cpp
    src/io/event.cpp
)

target_link_libraries(slac_meow
    PUBLIC
        everest::util
        # io/ only
        everest::io
    PRIVATE
        # protocol/utils.cpp only
        everest::tls
        everest::everest_api_types
        nlohmann_json::nlohmann_json
)

target_compile_features(slac_meow PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
