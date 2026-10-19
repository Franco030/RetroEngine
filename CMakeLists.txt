cmake_minimum_required(VERSION 3.20)

project(RetroEngine
    VERSION 0.2.0
    DESCRIPTION "Motor retro estilo PS1 sobre Raylib, con editor"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(RETRO_BUILD_EDITOR "Compilar el editor (RetroEditor)" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(BUILD_SHARED_LIBS ON  CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(raylib nlohmann_json)

if(RETRO_BUILD_EDITOR)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        docking
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _sin_cmake
    )
    FetchContent_Declare(
        rlimgui
        GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _sin_cmake
    )
    FetchContent_MakeAvailable(imgui rlimgui)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
    )
    target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR})

    add_library(rlimgui_lib STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
    target_include_directories(rlimgui_lib PUBLIC ${rlimgui_SOURCE_DIR})
    target_link_libraries(rlimgui_lib PUBLIC imgui_lib raylib)
    target_compile_definitions(rlimgui_lib PUBLIC NO_FONT_AWESOME)
endif()

file(GLOB_RECURSE RETRO_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM RETRO_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
file(GLOB_RECURSE RETRO_CORE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(retro_core STATIC ${RETRO_CORE_SOURCES} ${RETRO_CORE_HEADERS})
target_include_directories(retro_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retro_core PUBLIC raylib nlohmann_json::nlohmann_json)
target_compile_features(retro_core PUBLIC cxx_std_20)

target_compile_definitions(retro_core PUBLIC
    RETRO_INTERNAL_WIDTH=320
    RETRO_INTERNAL_HEIGHT=240
    RETRO_WINDOW_WIDTH=1280
    RETRO_WINDOW_HEIGHT=960
)

if(MSVC)
    target_compile_options(retro_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(retro_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
 
add_executable(RetroEngine src/main.cpp)
target_link_libraries(RetroEngine PRIVATE retro_core)
if(MSVC)
    target_compile_options(RetroEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(RetroEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RETRO_BUILD_EDITOR)
    file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp)
    file(GLOB EDITOR_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.hpp)

    add_executable(RetroEditor ${EDITOR_SOURCES} ${EDITOR_HEADERS})
    target_include_directories(RetroEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
    target_link_libraries(RetroEditor PRIVATE retro_core rlimgui_lib)

    target_compile_definitions(RetroEditor PRIVATE
        RETRO_PROJECT_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

    if(MSVC)
        target_compile_options(RetroEditor PRIVATE /W4 /permissive-)
    else()
        target_compile_options(RetroEditor PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:RetroEngine>/assets
    COMMENT "Sincronizando assets"
)
add_dependencies(RetroEngine CopyAssets)
 
if(UNIX)
    set(RETRO_RPATH "$ORIGIN")
    if(APPLE)
        set(RETRO_RPATH "@executable_path")
    endif()
    set_target_properties(RetroEngine PROPERTIES BUILD_RPATH "${RETRO_RPATH}" INSTALL_RPATH "${RETRO_RPATH}")
    if(RETRO_BUILD_EDITOR)
        set_target_properties(RetroEditor PROPERTIES BUILD_RPATH "${RETRO_RPATH}" INSTALL_RPATH "${RETRO_RPATH}")
    endif()
endif()