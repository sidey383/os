cmake_minimum_required(VERSION 3.16)
project(project C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

add_executable(project
        main.c
        http.c
        http.c
        tcp.c
        threadList.c
        socketEcho.c
)

find_package(Threads REQUIRED)
target_link_libraries(project PRIVATE Threads::Threads)
