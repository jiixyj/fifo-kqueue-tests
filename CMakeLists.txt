cmake_minimum_required(VERSION 3.7.2)
project(fifo-kqueue C)

#

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

#

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#

add_executable(fifo-kqueue main.c)

add_library(coro coro_pthread.c)
target_link_libraries(coro PRIVATE Threads::Threads)

target_link_libraries(fifo-kqueue PRIVATE coro)
