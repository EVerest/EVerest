add_library(everest_util INTERFACE)
add_library(everest::util ALIAS everest_util)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set_target_properties(everest_util PROPERTIES
  VERSION 0.0.1
)


target_include_directories(everest_util
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set_target_properties(everest_util
    PROPERTIES
        BUILD_INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
        EXPORT_NAME util
)

install(TARGETS everest_util
    EXPORT everest-core-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/everest
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

if(DISABLE_EDM)
    ev_packaging(
        NAME everest-util
        LIBRARY_NAME util
        NAMESPACE everest
        TYPE INTERFACE
    )
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
