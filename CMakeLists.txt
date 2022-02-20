cmake_minimum_required(VERSION 3.10)

# set the project name
project(BareServer VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

configure_file(./src/config.h.in ./src/config.h)

file(GLOB_RECURSE sources ${PROJECT_SOURCE_DIR} ./src/*.cpp)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost COMPONENTS program_options REQUIRED)

find_library(LIBSTATGRAB NAMES statgrab PATHS)

include_directories(${Boost_INCLUDE_DIRS})

include_directories(${OPENSSL_INCLUDE_DIR})

# add the executable
add_executable(BareServer ${sources})

target_link_libraries(BareServer ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(BareServer ${Boost_LIBRARIES})