ev_setup_cpp_module()
target_link_libraries(${MODULE_NAME} PRIVATE atomic everest::everest_api_types everest::everest_api_module_helpers)
target_sources(${MODULE_NAME} PRIVATE "main/generic_errorImpl.cpp" "energy_grid/energyImpl.cpp")
