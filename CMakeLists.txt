cmake_minimum_required(VERSION 3.16.3)

project(postgrespp CXX)

add_subdirectory(src)

option(POSTGRESPP_BUILD_TESTS "Build tests" OFF)

if(POSTGRESPP_BUILD_TESTS)
  add_subdirectory(test)
endif()
