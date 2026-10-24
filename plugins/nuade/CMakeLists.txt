# Nuade Playback Plugin
#
# Plays the Amiga formats of UADE's players, ported to C by nuade.
# Based on nuade (https://github.com/emoon/nuade).

cmake_minimum_required(VERSION 3.16)
project(playback_nuade C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PlaybackPlugin)

# Download nuade, pinned to a commit (it has no releases yet)
download_library(
    NAME nuade
    URL "https://github.com/emoon/nuade/archive/b59fcd55567f891b25555c9cf67ce45efd4977c9.tar.gz"
    FILENAME "nuade-b59fcd5.tar.gz"
    HASH "af82e5fc322477e95fcedbcf6476cf52a3a013c87da84a3d7288b63fee8f7798"
    EXTRACT_DIR "nuade-b59fcd55567f891b25555c9cf67ce45efd4977c9"
)

# nuade builds as a static library; it ends up inside the plugin's shared library.
set(NUADE_PLAYERS "all" CACHE STRING "" FORCE)
add_subdirectory("${nuade_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nuade" EXCLUDE_FROM_ALL)
set_target_properties(nuade PROPERTIES POSITION_INDEPENDENT_CODE ON)
suppress_external_warnings(nuade)

add_library(nuade_playback SHARED nuade_plugin.c nuade_viz_adapter.c)
target_link_libraries(nuade_playback PRIVATE nuade::nuade)
rv_set_plugin_properties(nuade_playback)
