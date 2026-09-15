add_subdirectory(d20)
if(ISO15118_BUILD_EV_SIDE)
    add_subdirectory(ev)
endif()
add_subdirectory(fsm)
add_subdirectory(io)
add_subdirectory(session)
add_subdirectory(states)

# add_executable(secc)
#
# target_sources(secc
#     PRIVATE
#         secc.cpp
# )
#
# target_link_libraries(secc
#     PRIVATE
#         iso15118::iso15118
# )
