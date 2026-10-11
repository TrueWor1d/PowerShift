cmake_minimum_required(VERSION 3.20)
project(PowerShift VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "PowerShift requires Windows or a Windows cross-compilation toolchain.")
endif()

# Use the Microsoft static runtime when built with Visual Studio.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(powershift_core STATIC
    src/power_plans.cpp
    src/startup.cpp
    src/win32_util.cpp
)
target_compile_features(powershift_core PUBLIC cxx_std_17)
target_include_directories(powershift_core PUBLIC src)
target_compile_definitions(powershift_core PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_link_libraries(powershift_core PUBLIC powrprof advapi32 user32 ole32)

add_executable(PowerShift WIN32 src/main.cpp resources/PowerShift.rc)
target_include_directories(PowerShift PRIVATE resources)
target_link_libraries(PowerShift PRIVATE powershift_core shell32 comctl32 uuid)

function(powershift_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8;/EHsc>")
    else()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion>")
    endif()
endfunction()
powershift_warnings(powershift_core)
powershift_warnings(PowerShift)

if(MSVC)
    # The application manifest is supplied by the resource file.
    target_link_options(PowerShift PRIVATE /MANIFEST:NO)
elseif(MINGW)
    target_link_options(PowerShift PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

option(POWERSHIFT_BUILD_TESTS "Build Windows API integration tests" OFF)
if(POWERSHIFT_BUILD_TESTS)
    enable_testing()
    add_executable(PowerShiftTests tests/windows_tests.cpp)
    target_link_libraries(PowerShiftTests PRIVATE powershift_core)
    powershift_warnings(PowerShiftTests)
    if(MINGW)
        target_link_options(PowerShiftTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME PowerShiftWindowsIntegration COMMAND PowerShiftTests)
endif()

install(TARGETS PowerShift RUNTIME DESTINATION .)
