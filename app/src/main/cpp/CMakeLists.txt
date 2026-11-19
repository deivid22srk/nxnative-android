cmake_minimum_required(VERSION 3.22.1)
project(nxnative_android C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(nxnative_android SHARED
    native_bridge.cpp
    vulkan_present.cpp
)

target_include_directories(nxnative_android PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Core sources are platform-neutral and also compiled by the host test in
# tests/native/test_nsp.cpp.
add_library(nx2a_core STATIC keys.cpp nsp.cpp import_check.cpp)
target_include_directories(nx2a_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(nxnative_android PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_options(nx2a_core PRIVATE -Wall -Wextra)

find_library(log-lib log)
target_link_libraries(nxnative_android PRIVATE nx2a_core android log vulkan)
