cmake_minimum_required(VERSION 3.20)
project(orbwvr)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio
        GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)

add_executable(orbwvr main.cpp src/postgres/PgConnection.cpp src/postgres/PgResult.cpp src/database.cpp src/result.cpp) 

target_compile_definitions(orbwvr PUBLIC ASIO_STANDALONE)

target_include_directories(orbwvr PUBLIC ${asio_SOURCE_DIR}/asio/include 
                                        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(orbwvr PUBLIC Threads::Threads PostgreSQL::PostgreSQL)