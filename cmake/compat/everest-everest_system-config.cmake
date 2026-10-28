message(WARNING "find_package(everest-everest_system): this package does not exist any more and THIS FORWARDER WILL BE REMOVED. Use find_package(everest-core) and the everest:: targets instead.")
include(${CMAKE_CURRENT_LIST_DIR}/../everest-core/everest-core-config.cmake)
set(everest-everest_system_VERSION ${everest-core_VERSION})
