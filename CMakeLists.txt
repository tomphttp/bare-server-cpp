cmake_minimum_required(VERSION 3.10)

# set the project name
project(BareServer VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

configure_file(./src/config.h.in ./src/config.h)

file(GLOB_RECURSE sources ${PROJECT_SOURCE_DIR} ./src/*.cpp)

include_directories(${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ./certify/include/)

if (MSVC)
  add_definitions(/bigobj)
endif ()

add_executable(BareServer ${sources})


find_package(Threads REQUIRED)
target_link_libraries(BareServer ${CMAKE_THREAD_LIBS_INIT})

find_package(OpenSSL REQUIRED)
target_link_libraries(BareServer OpenSSL::SSL OpenSSL::Crypto) 

find_package(Boost COMPONENTS program_options REQUIRED)
target_link_libraries(BareServer ${Boost_LIBRARIES})
