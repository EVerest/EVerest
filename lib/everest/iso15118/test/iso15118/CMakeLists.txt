add_subdirectory(d2)
add_subdirectory(d20)
if(ISO15118_BUILD_EV_SIDE)
    add_subdirectory(ev)
endif()
add_subdirectory(fsm)
add_subdirectory(io)
add_subdirectory(session)
add_subdirectory(states)

include(Catch)

add_executable(test_enum_names enum_names.cpp)

target_link_libraries(test_enum_names
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_enum_names)

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
