add_compile_options(${IEEE2030_COMPILE_OPTIONS_WARNING})

include(FetchContent)
if (NOT TARGET Catch2::Catch2WithMain)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.10.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_GetProperties(Catch2)
if(catch2_POPULATED)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

add_subdirectory(ieee2030)
add_subdirectory(charger)
add_subdirectory(common)
