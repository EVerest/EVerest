cmake_minimum_required(VERSION 3.16)

project(everest-timer
    VERSION 0.1.4
    DESCRIPTION "EVerest timer library"
    LANGUAGES CXX C
)

# options
option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(CMAKE_RUN_CLANG_TIDY "Run clang-tidy" OFF)

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(LIBTIMER_BUILD_TESTING ON)
    include(CTest)
    add_subdirectory(tests)
endif()

# this policy allows us to continue using the removed FindBoost module for now
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# dependencies
find_package(Boost)
if(Boost_VERSION_STRING VERSION_LESS "1.69.0")
    find_package(Boost
        COMPONENTS
            system
        REQUIRED
    )
endif()

if (DISABLE_EDM)
    find_package(date REQUIRED)
endif()


# logging library
add_subdirectory(lib)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# packaging
install(
    TARGETS timer
    EXPORT everest-base-targets
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)
