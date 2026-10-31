add_library(everest_date STATIC src/date.cpp)
add_library(everest::date ALIAS everest_date)
ev_register_library_target(everest_date)

if(DISABLE_EDM)
    find_package(date REQUIRED)
endif()

target_include_directories(everest_date
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(everest_date
    PUBLIC
        date::date-tz
)

target_compile_features(everest_date PUBLIC cxx_std_17)

set_target_properties(everest_date
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME date
)

install(TARGETS everest_date
    EXPORT everest-base-targets
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
