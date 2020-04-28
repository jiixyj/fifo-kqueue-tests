include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(ATFTest)

add_library(atf-c INTERFACE)
add_library(atf::atf-c ALIAS atf-c)
target_include_directories(atf-c INTERFACE "${CMAKE_CURRENT_LIST_DIR}/microatf")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#

macro(atf_test _testname)
  add_executable("${_testname}" "${_testname}.c")
  target_link_libraries("${_testname}" PRIVATE Threads::Threads atf::atf-c)
  atf_discover_tests("${_testname}" ${ARGN})
endmacro()

#

atf_test(pipe_kqueue_test)
atf_test(fifo_kqueue)
