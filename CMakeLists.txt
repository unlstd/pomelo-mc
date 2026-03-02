cmake_minimum_required(VERSION 3.16)
project(pomelo-mc VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(pomelo-mc
    src/main.cpp
)

if(WIN32)
    target_link_libraries(pomelo-mc ws2_32)
endif()

# add_subdirectory(external/SDL)


# header-only
target_include_directories(pomelo-mc PUBLIC
    include
)

target_link_libraries(pomelo-mc
    PRIVATE
)

