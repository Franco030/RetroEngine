cmake_minimum_required(VERSION 3.20)

project(RetroEngine
    VERSION 0.2.0
    DESCRIPTION "Motor retro estilo PS1 sobre Raylib, con editor"
    LANGUAGES C CXX
)

if(WIN32)
    enable_language(RC) # el icono de los ejecutables (games/<id>/icon.ico)
endif()

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
# Para repartir un juego: raylib y el runtime del compilador (libstdc++, libgcc, winpthread) van dentro
# del ejecutable, sin DLL aparte. Con esto, '<exe>Dist' arma una carpeta que corre en cualquier Windows.
option(RETRO_STATIC_RUNTIME "Ejecutables sin DLL externas (para repartir el juego)" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

if(RETRO_STATIC_RUNTIME)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
else()
    set(BUILD_SHARED_LIBS ON  CACHE BOOL "" FORCE)
endif()
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
list(REMOVE_ITEM RETRO_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/GameMain.cpp)
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
 
# Los assets del motor (shaders, fuentes, los sonidos del propio runtime): junto a los ejecutables,
# en engine/. Lo que un juego no trae se busca ahi.
add_custom_target(CopyEngineAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/assets
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/engine
    COMMENT "Sincronizando los assets del motor"
)

# retro_add_game(<id>): el juego de games/<id>/ (su game.json y sus assets/). El nombre del
# ejecutable sale de "exe" en el game.json. Crea dos, los dos del runtime del motor:
#  - <exe>: el juego tal cual (el producto). Sin teclas ni opciones de depuracion y, en Windows,
#    sin ventana de consola.
#  - <exe>Debug: lo mismo con las herramientas (F1-F10, Tab, 1-6, M, --scene, --pos, --flags,
#    --prueba, --assets) y con consola. El editor lo usa para "Probar aqui".
# Y copia games/<id>/ junto a ellos (bin/<id>/game.json y bin/<id>/assets/).
function(retro_add_game id)
    set(game_dir ${CMAKE_CURRENT_SOURCE_DIR}/games/${id})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${game_dir}/game.json)
    file(READ ${game_dir}/game.json game_json)
    string(JSON exe ERROR_VARIABLE no_exe GET "${game_json}" exe)
    if(no_exe)
        message(FATAL_ERROR "games/${id}/game.json: falta \"exe\" (el nombre del ejecutable)")
    endif()

    add_custom_target(${exe}Assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${game_dir}/assets ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${id}/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${game_dir}/game.json ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${id}/game.json
        COMMENT "Sincronizando ${id}"
    )
    # el icono del ejecutable (Explorador, barra de tareas): games/<id>/icon.ico, si lo trae
    set(icon_rc "")
    if(WIN32 AND EXISTS ${game_dir}/icon.ico)
        set(icon_rc ${CMAKE_BINARY_DIR}/${id}_icon.rc)
        file(WRITE ${icon_rc} "1 ICON \"${game_dir}/icon.ico\"\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${game_dir}/icon.ico)
    endif()
    foreach(target ${exe} ${exe}Debug)
        add_executable(${target} ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/GameMain.cpp ${icon_rc})
        target_link_libraries(${target} PRIVATE retro_core)
        target_compile_definitions(${target} PRIVATE RETRO_GAME_DIR="${id}")
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_dependencies(${target} ${exe}Assets CopyEngineAssets)
        if(UNIX)
            set_target_properties(${target} PROPERTIES BUILD_RPATH "${RETRO_RPATH}" INSTALL_RPATH "${RETRO_RPATH}")
        endif()
    endforeach()
    target_compile_definitions(${exe}Debug PRIVATE RETRO_DEBUG_TOOLS)
    if(RETRO_STATIC_RUNTIME AND MINGW)
        target_link_options(${exe} PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_options(${exe}Debug PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    # <exe>Dist: la carpeta para repartir el juego (dist/<exe>/: el ejecutable, engine/ y <id>/) y su
    # .zip. Sin el ejecutable de depuracion ni el editor. Con RETRO_STATIC_RUNTIME no lleva DLL; si no,
    # copia las que el ejecutable trae al lado (raylib).
    set(dist_dir ${CMAKE_BINARY_DIR}/dist/${exe})
    add_custom_target(${exe}Dist
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${dist_dir}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dist_dir}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${exe}> ${dist_dir}/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/engine/assets ${dist_dir}/engine
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${game_dir}/assets ${dist_dir}/${id}/assets
        COMMAND ${CMAKE_COMMAND} -E copy ${game_dir}/game.json ${dist_dir}/${id}/game.json
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:${RETRO_STATIC_RUNTIME}>,true,copy_if_different> $<$<NOT:$<BOOL:${RETRO_STATIC_RUNTIME}>>:$<TARGET_FILE:raylib>> $<$<NOT:$<BOOL:${RETRO_STATIC_RUNTIME}>>:${dist_dir}/>
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/dist ${CMAKE_COMMAND} -E tar cf ${exe}.zip --format=zip ${exe}
        DEPENDS ${exe}
        COMMENT "Armando dist/${exe} y dist/${exe}.zip"
        VERBATIM
    )
    if(WIN32)
        set_target_properties(${exe} PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC) # con WIN32_EXECUTABLE MSVC busca WinMain; el juego entra por main
            target_link_options(${exe} PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
endfunction()

if(UNIX)
    set(RETRO_RPATH "$ORIGIN")
    if(APPLE)
        set(RETRO_RPATH "@executable_path")
    endif()
endif()

# Los juegos: cada carpeta de games/ con su game.json es uno. CONFIGURE_DEPENDS hace que un juego
# nuevo (por ejemplo, creado con "Nuevo juego" del editor) entre en la siguiente compilacion.
file(GLOB RETRO_GAME_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/games/*/game.json)
foreach(game_file ${RETRO_GAME_FILES})
    get_filename_component(game_dir ${game_file} DIRECTORY)
    get_filename_component(game_id ${game_dir} NAME)
    retro_add_game(${game_id})
endforeach()

if(RETRO_BUILD_EDITOR)
    file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp)
    file(GLOB EDITOR_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.hpp)

    add_executable(RetroEditor ${EDITOR_SOURCES} ${EDITOR_HEADERS})
    target_include_directories(RetroEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
    target_link_libraries(RetroEditor PRIVATE retro_core rlimgui_lib)

    # El editor trabaja sobre las carpetas del repo: games/<id>/ (el juego que abre, por defecto
    # RETRO_DEFAULT_GAME) y engine/assets.
    target_compile_definitions(RetroEditor PRIVATE
        RETRO_GAMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/games"
        RETRO_ENGINE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
        RETRO_DEFAULT_GAME="festum_venti")
    add_dependencies(RetroEditor CopyEngineAssets)

    if(MSVC)
        target_compile_options(RetroEditor PRIVATE /W4 /permissive-)
    else()
        target_compile_options(RetroEditor PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(UNIX AND RETRO_BUILD_EDITOR)
    set_target_properties(RetroEditor PROPERTIES BUILD_RPATH "${RETRO_RPATH}" INSTALL_RPATH "${RETRO_RPATH}")
endif()
