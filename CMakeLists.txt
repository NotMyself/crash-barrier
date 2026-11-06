cmake_minimum_required(VERSION 3.25)
project(CrashBarrier LANGUAGES CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CrashBarrier is a Windows x64 DLL; configure with an x64 MSVC toolchain.")
endif()

# Version comes from git tags via scripts/version.ps1 (scripts/build.ps1 passes it in).
set(CRASHBARRIER_VERSION "0.0.0-dev" CACHE STRING "Semantic version stamped into the DLL and the trace header")
set(CRASHBARRIER_BUILD_NUMBER 0 CACHE STRING "Fourth component of the Windows file version (CI run number)")
if(NOT CRASHBARRIER_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "CRASHBARRIER_VERSION must start with MAJOR.MINOR.PATCH, got '${CRASHBARRIER_VERSION}'")
endif()
set(V_MAJOR ${CMAKE_MATCH_1})
set(V_MINOR ${CMAKE_MATCH_2})
set(V_PATCH ${CMAKE_MATCH_3})
set(V_BUILD ${CRASHBARRIER_BUILD_NUMBER})
configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/version.rc @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/WX> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                    $<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX CRASHBARRIER_VERSION="${CRASHBARRIER_VERSION}")

add_library(CrashBarrier SHARED
    src/dllmain.cpp
    src/class_factory.cpp
    src/asio_proxy.cpp
    src/callback_shim.cpp
    src/real_driver.cpp
    src/seh_guard.cpp
    src/veh.cpp
    src/logger.cpp
    src/exports.def
    ${CMAKE_BINARY_DIR}/version.rc)
target_include_directories(CrashBarrier PUBLIC include PRIVATE src)
target_link_libraries(CrashBarrier PRIVATE dbghelp ole32 advapi32 user32 version uuid)
set_target_properties(CrashBarrier PROPERTIES OUTPUT_NAME CrashBarrier_x64 PREFIX "")

# Test tools (not shipped)
add_executable(asiohost_test tools/asiohost_test.cpp)
target_include_directories(asiohost_test PRIVATE include src)
target_link_libraries(asiohost_test PRIVATE ole32 uuid user32)

add_library(FaultyDriver SHARED tools/faulty_driver.cpp)
target_include_directories(FaultyDriver PRIVATE include)
set_target_properties(FaultyDriver PROPERTIES OUTPUT_NAME FaultyDriver_x64 PREFIX "")
