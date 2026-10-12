cmake_minimum_required(VERSION 3.20)
project(PowerShift VERSION 1.2.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "PowerShift requires Windows or a Windows cross-compilation toolchain.")
endif()

# Use the Microsoft static runtime when built with Visual Studio.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(POWERSHIFT_DISPLAY_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if(PROJECT_VERSION_PATCH GREATER 0)
    string(APPEND POWERSHIFT_DISPLAY_VERSION ".${PROJECT_VERSION_PATCH}")
endif()
set(POWERSHIFT_NUMERIC_VERSION "${PROJECT_VERSION}.0")
set(POWERSHIFT_BUILD_REVISION "source-archive")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE POWERSHIFT_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE POWERSHIFT_DIRTY ERROR_QUIET)
    if(POWERSHIFT_DIRTY)
        string(APPEND POWERSHIFT_BUILD_REVISION "-dirty")
    endif()
endif()
set(POWERSHIFT_RELEASE_NOTES "版本修改详见 GitHub Release。")
set(POWERSHIFT_NOTES_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.github/release-notes/v${POWERSHIFT_DISPLAY_VERSION}.md")
if(EXISTS "${POWERSHIFT_NOTES_FILE}")
    file(READ "${POWERSHIFT_NOTES_FILE}" POWERSHIFT_RELEASE_NOTES)
    string(STRIP "${POWERSHIFT_RELEASE_NOTES}" POWERSHIFT_RELEASE_NOTES)
endif()
configure_file(src/powershift_version.h.in generated/powershift_version.h @ONLY)
configure_file(resources/PowerShift.manifest.in generated/PowerShift.manifest @ONLY)
configure_file(resources/PowerShift.rc.in generated/PowerShift.rc @ONLY)
configure_file(使用说明.txt.in generated/使用说明.txt @ONLY)

add_library(powershift_core STATIC
    src/power_plans.cpp
    src/startup.cpp
    src/win32_util.cpp
)
target_compile_features(powershift_core PUBLIC cxx_std_17)
target_include_directories(powershift_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(powershift_core PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_link_libraries(powershift_core PUBLIC powrprof advapi32 user32 ole32)

add_executable(PowerShift WIN32 src/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/PowerShift.rc")
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
    add_executable(PowerShiftTrayTests tests/tray_tests.cpp)
    target_include_directories(PowerShiftTrayTests PRIVATE resources)
    target_link_libraries(PowerShiftTrayTests PRIVATE powershift_core shell32 comctl32 uuid)
    powershift_warnings(PowerShiftTrayTests)
    if(MINGW)
        target_link_options(PowerShiftTrayTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME PowerShiftTrayRecovery COMMAND PowerShiftTrayTests)
endif()

install(TARGETS PowerShift RUNTIME DESTINATION .)
