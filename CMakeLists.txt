cmake_minimum_required(VERSION 3.16)
project(TetrisAutoLAN VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TETRIS_BUILD_GUI "Construire le jeu SDL2 (OFF pour CI reseau sans ecran)" ON)
option(TETRIS_SANITIZE "Activer AddressSanitizer et UBSan pour debug" OFF)

if(TETRIS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Aucune dependance SDL2 dans le domaine, l'IA ou les sockets TCP.
add_library(tetris_core
    src/core/Pieces.cpp
    src/core/Game.cpp
    src/ai/AutoPlayer.cpp
    src/net/Protocol.cpp
    src/net/LineFramer.cpp
    src/net/ConnectionMonitor.cpp
    src/net/LanPeer.cpp
)
target_include_directories(tetris_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tetris_core PRIVATE -Wall -Wextra -Wpedantic)

if(TETRIS_BUILD_GUI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    add_executable(tetris_lan src/main.cpp
        src/ui/Canvas.cpp
        src/ui/InputMap.cpp
        src/ui/App.cpp
        src/ui/AppNetwork.cpp
        src/ui/AppInput.cpp
        src/ui/AppRender.cpp
    )
    target_link_libraries(tetris_lan PRIVATE tetris_core PkgConfig::SDL2)
    target_compile_options(tetris_lan PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(tetris_tests tests/tests.cpp)
    target_link_libraries(tetris_tests PRIVATE tetris_core Threads::Threads)
    target_compile_options(tetris_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME game_protocol_network_tests COMMAND tetris_tests)
    add_executable(tetris_network_tests tests/network_tests.cpp)
    target_link_libraries(tetris_network_tests PRIVATE tetris_core Threads::Threads)
    target_compile_options(tetris_network_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME protocol_and_tcp_resilience COMMAND tetris_network_tests)
    if(TETRIS_BUILD_GUI)
        add_executable(tetris_sdl_tests tests/sdl_tests.cpp src/ui/Canvas.cpp)
        target_include_directories(tetris_sdl_tests PRIVATE src)
        target_link_libraries(tetris_sdl_tests PRIVATE PkgConfig::SDL2)
        target_compile_options(tetris_sdl_tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME sdl2_canvas_smoke_test COMMAND tetris_sdl_tests)
        set_tests_properties(sdl2_canvas_smoke_test PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
endif()
