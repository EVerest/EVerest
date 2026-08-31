#
# Tests for the SLAC protocol layer and the transport.
#
# File names do not repeat slac_meow - the directory already says it. Target names do keep the
# prefix, since they and the ctest names they produce live in a namespace shared with every other
# library's tests, where a bare config_test would be ambiguous.
#
add_executable(slac_meow_message_init_test message_init_test.cpp)
target_link_libraries(slac_meow_message_init_test PRIVATE slac_meow)
target_compile_features(slac_meow_message_init_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_message_init_test COMMAND slac_meow_message_init_test)

add_executable(slac_meow_status_test status_test.cpp)
target_link_libraries(slac_meow_status_test PRIVATE slac_meow nlohmann_json::nlohmann_json)
target_compile_features(slac_meow_status_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_status_test COMMAND slac_meow_status_test)
