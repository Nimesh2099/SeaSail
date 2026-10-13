cmake_minimum_required(VERSION 3.16)
project(SeaSail LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

# --- GLFW (window + input): use a system install, otherwise download it ---
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)  # Linux: X11 only
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)
endif()

# --- glad: OpenGL 3.3 core loader (generated, checked in) ---
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

add_executable(SeaSail
    src/main.cpp
    src/core/Camera.cpp
    src/core/Noise.cpp
    src/core/Shader.cpp
    src/core/Screenshot.cpp
    src/core/Console.cpp
    src/world/IslandGenerator.cpp
    src/world/Erosion.cpp
    src/world/TerrainStreamer.cpp
    src/world/Ocean.cpp
    src/world/Props.cpp
    src/world/Landmarks.cpp
    src/game/Boat.cpp
    src/game/BoatModel.cpp
    src/game/ChaseCamera.cpp
    src/game/Environment.cpp
    src/render/Mesh.cpp
    src/render/SceneTarget.cpp
    src/render/Hud.cpp
    src/render/PropRenderer.cpp
    src/render/LandmarkModels.cpp
    src/render/Minimap.cpp
    src/render/TextRenderer.cpp
    src/render/ConsoleView.cpp
    src/render/Profiler.cpp)
target_include_directories(SeaSail PRIVATE src src/core src/world src/game src/render)
target_link_libraries(SeaSail PRIVATE glad glfw Threads::Threads)

if(MSVC)
    target_compile_options(SeaSail PRIVATE /W4 /permissive- /MP)
    target_compile_definitions(SeaSail PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT SeaSail)
else()
    target_compile_options(SeaSail PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Shaders are read at runtime from <exe dir>/shaders (also found from the
# repo root or the build folder, so running from Visual Studio just works).
add_custom_command(TARGET SeaSail POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:SeaSail>/shaders)

# --- Expressive range analysis (no window or GPU; see "Evaluation" in README.md) ---
add_executable(ExpressiveRange
    tools/expressive_range.cpp
    src/core/Noise.cpp
    src/world/IslandGenerator.cpp
    src/world/Erosion.cpp)
target_include_directories(ExpressiveRange PRIVATE src/core src/world)
target_link_libraries(ExpressiveRange PRIVATE Threads::Threads)
if(MSVC)
    target_compile_definitions(ExpressiveRange PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
