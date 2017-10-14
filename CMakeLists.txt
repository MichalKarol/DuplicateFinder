cmake_minimum_required(VERSION 3.2)
project(DuplicateFinder)

find_package(OpenSSL REQUIRED) 
if( OpenSSL_FOUND )
    include_directories(${OPENSSL_INCLUDE_DIRS})
    link_directories(${OPENSSL_LIBRARIES})
    message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z")

if (CMAKE_COMPILER_IS_GNUXX)
  option(ENABLE_COVERAGE "Enable coverage reporting for gcc/clang" FALSE)

  if (ENABLE_COVERAGE)
    add_compile_options(--coverage -O0)
  endif()
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -lboost_program_options -lcrypto -lpthread -lstdc++fs")

add_library(stdc++fs UNKNOWN IMPORTED)
set_property(TARGET stdc++fs PROPERTY IMPORTED_LOCATION "/usr/lib/gcc/x86_64-linux-gnu/7/libstdc++fs.a")

add_executable(duplicateFinder main.cpp)
target_link_libraries(duplicateFinder --coverage stdc++fs)

#enable_testing()
#add_executable(tester tester.cpp)
#target_link_libraries(tester --coverage)
#add_test(Tester tester)