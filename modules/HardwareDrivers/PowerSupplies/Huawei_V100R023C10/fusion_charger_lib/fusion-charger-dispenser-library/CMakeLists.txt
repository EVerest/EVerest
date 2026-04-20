file(GLOB LIB_SOURCES lib/*.cpp)
add_library(fusion_charger_dispenser STATIC ${LIB_SOURCES})
target_include_directories(fusion_charger_dispenser PUBLIC include)
target_link_libraries(fusion_charger_dispenser PUBLIC fusion_charger_goose_driver fusion_charger_modbus_driver fusion_charger_modbus_extensions modbus-ssl Huawei::FusionCharger::LogInterface)

if(FUSION_CHARGER_LIB_BUILD_TESTS)
  add_subdirectory(tests)

  option(BUILD_ACCEPTANCE_TESTS "Build acceptance tests" OFF)
  if (BUILD_ACCEPTANCE_TESTS)
    add_subdirectory(user-acceptance-tests)
    include(FetchContent)
    FetchContent_Declare(
      MQTT-C
      GIT_REPOSITORY https://github.com/LiamBindle/MQTT-C
      GIT_TAG        v1.1.6
    )
    FetchContent_MakeAvailable(MQTT-C)
    add_subdirectory(power_stack_mock)
  endif()
endif()
