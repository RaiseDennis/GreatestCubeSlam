cmake_minimum_required(VERSION 3.24)
project(GreatestCubeSlam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- SFML 3 (fetched + built from source, static) ----
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SFML
    URL https://github.com/SFML/SFML/archive/refs/tags/3.1.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)

# ---- Game ----
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(CubeSlam ${GAME_SOURCES})
target_include_directories(CubeSlam PRIVATE src)
target_link_libraries(CubeSlam PRIVATE SFML::Graphics SFML::Audio SFML::Window SFML::System)

if(WIN32)
    target_link_libraries(CubeSlam PRIVATE opengl32)
    # No console window in release builds.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(CubeSlam PROPERTIES WIN32_EXECUTABLE ON)
        target_link_libraries(CubeSlam PRIVATE SFML::Main)
    endif()
endif()

if(MINGW)
    # Produce a single self-contained .exe (no libstdc++/libgcc DLLs needed).
    target_link_options(CubeSlam PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(CubeSlam PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
