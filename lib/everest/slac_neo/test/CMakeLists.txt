add_executable(evse_slac_neo_set_key_test evse_slac_neo_set_key_test.cpp)

target_link_libraries(evse_slac_neo_set_key_test
    PRIVATE
        slac_neo
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_neo_set_key_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_set_key_test COMMAND evse_slac_neo_set_key_test)
