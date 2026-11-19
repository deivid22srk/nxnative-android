cmake_minimum_required(VERSION 3.22.1)
project(nxnative_android C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libadrenotools)
set(LN ${AT}/lib/linkernsbypass)

add_library(nxnative_android SHARED
    native_bridge.cpp
    vulkan_present.cpp
    vk_api.cpp
)

target_include_directories(nxnative_android PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AT}/include
)

# Core sources are platform-neutral and also compiled by the host test in
# tests/native/test_nsp.cpp.
add_library(nx2a_core STATIC
    keys.cpp
    nsp.cpp
    import_check.cpp
    crypto.cpp
    content_check.cpp
)
target_include_directories(nx2a_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(nxnative_android PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_options(nx2a_core PRIVATE -Wall -Wextra)
target_link_options(nxnative_android PRIVATE -Wl,-z,max-page-size=16384)

find_library(log-lib log)
target_link_libraries(nxnative_android PRIVATE nx2a_core android log dl adrenotools)

# ---------------------------------------------------------------------------
# libadrenotools (BSD-2-Clause, third_party/libadrenotools): loads a custom
# Vulkan driver (Turnip) in a private linker namespace. The three hook
# libraries are loaded by name from the APK's nativeLibraryDir, so each is a
# separate shared library; libhook_impl must stay global for them.
# ---------------------------------------------------------------------------
add_library(adrenotools SHARED
    ${AT}/src/driver.cpp
    ${AT}/src/bcenabler.cpp
    ${LN}/android_linker_ns.cpp
    ${LN}/elf_soname_patcher.cpp
)
set_target_properties(adrenotools PROPERTIES OUTPUT_NAME adrenotools)
target_include_directories(adrenotools PRIVATE ${AT}/include ${AT} ${LN})
target_compile_options(adrenotools PRIVATE -fPIC -w)
target_link_libraries(adrenotools PRIVATE android log dl)
target_link_options(adrenotools PRIVATE -Wl,-z,max-page-size=16384)

add_library(hook_impl SHARED
    ${AT}/src/hook/hook_impl.cpp
    ${LN}/android_linker_ns.cpp
    ${LN}/elf_soname_patcher.cpp
)
set_target_properties(hook_impl PROPERTIES OUTPUT_NAME hook_impl)
target_include_directories(hook_impl PRIVATE ${AT}/include ${AT} ${LN} ${AT}/src/hook)
target_compile_options(hook_impl PRIVATE -fPIC -w -fvisibility=hidden)
target_link_libraries(hook_impl PRIVATE android log dl)
target_link_options(hook_impl PRIVATE -Wl,-z,max-page-size=16384)

foreach(hook main_hook file_redirect_hook gsl_alloc_hook)
    add_library(${hook} SHARED ${AT}/src/hook/${hook}.c)
    set_target_properties(${hook} PROPERTIES OUTPUT_NAME ${hook})
    target_include_directories(${hook} PRIVATE ${AT}/include ${AT} ${LN} ${AT}/src/hook)
    target_compile_options(${hook} PRIVATE -fPIC -w)
    target_link_libraries(${hook} PRIVATE hook_impl android log dl)
    # The hooks interpose symbols in the driver's namespace.
    target_link_options(${hook} PRIVATE -Wl,-z,global -Wl,-z,max-page-size=16384)
endforeach()