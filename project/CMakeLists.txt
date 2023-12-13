cmake_minimum_required(VERSION 3.16)
project(project C)

set(CMAKE_C_STANDARD 11)

add_executable(project main.c
        http.c
        http.c
        tcp.c
        threadList.c
        socketBuffer.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(project PRIVATE Threads::Threads)
