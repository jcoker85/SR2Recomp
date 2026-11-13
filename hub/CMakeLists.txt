# SR2Recomp hub: builds SR2Recomp.exe into the SR2Recomp folder (beside
# the toolkit folder) and copies games.txt there. settings.ini and
# covers/ are created beside it at run time. Use build.cmd, or:
#   cmake -S hub -B hub/build -G Ninja && cmake --build hub/build
cmake_minimum_required(VERSION 3.20)
project(SR2RecompHub C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(_root "${CMAKE_CURRENT_SOURCE_DIR}/..")
# No icon is built in: the launcher downloads it on its first start (hub.c, fetch_icon); it is not ours to redistribute.
set(_sources hub.c)
add_executable(SR2Recomp WIN32 ${_sources})
target_compile_definitions(SR2Recomp PRIVATE UNICODE _UNICODE)
target_link_options(SR2Recomp PRIVATE -municode -static)
target_link_libraries(SR2Recomp PRIVATE gdiplus msimg32 urlmon comdlg32)
set_target_properties(SR2Recomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_root}")

# The game list; edit hub/games.txt and rebuild to add a game.
configure_file(games.txt "${_root}/games.txt" COPYONLY)
