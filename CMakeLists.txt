cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
elseif(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
project(AttemptSync VERSION 1.4.1)
add_library(${PROJECT_NAME} SHARED src/main.cpp)
# Optional writer token baked into the binary as a first-run default. CI passes
# it from a repository secret so it never appears in this public repository.
# An empty value ships viewer-only behaviour with no token.
set(ATTEMPT_SYNC_SHARED_TOKEN "" CACHE STRING "GitHub token baked in as the first-run default; empty disables it")
target_compile_definitions(${PROJECT_NAME} PRIVATE ATTEMPT_SYNC_SHARED_TOKEN="${ATTEMPT_SYNC_SHARED_TOKEN}")
if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK to the Geode SDK directory")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
# Sign the final iOS library BEFORE Geode's POST_BUILD packaging command.
# Device dyld refuses an unsigned dylib even when compilation succeeds.
if(GEODE_TARGET_PLATFORM STREQUAL "iOS")
    find_program(ATTEMPT_SYNC_CODESIGN codesign REQUIRED)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${ATTEMPT_SYNC_CODESIGN}" --force --sign - --timestamp=none "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMAND "${ATTEMPT_SYNC_CODESIGN}" --verify --strict "$<TARGET_FILE:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
setup_geode_mod(${PROJECT_NAME})
