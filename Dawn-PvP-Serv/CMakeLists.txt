# Tests only. The client and server themselves build from restoration.sln.
cmake_minimum_required(VERSION 3.25)
project(DawnTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The tests check with assert(), so keep it on in every config.
foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
    string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
endforeach()

enable_testing()
add_subdirectory(tests)
