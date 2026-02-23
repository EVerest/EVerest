set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads)

find_package(Boost COMPONENTS program_options REQUIRED)

add_executable(libtimer_main main.cpp)
target_link_libraries(libtimer_main
    PRIVATE
        Threads::Threads
        Boost::program_options
        everest::timer
)
