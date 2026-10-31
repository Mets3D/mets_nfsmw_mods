cmake_minimum_required(VERSION 3.16)
project(nfsmw_mods LANGUAGES C CXX)

# Build our mods against the SDK clone without going through its root
# CMakeLists (we only want the plugin helper, not the examples/install rules).
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/../Tools/nfsmw-2005-sdk")
set(NFSMW_SDK_INCLUDE_DIR  "${SDK}/include")
set(NFSMW_SDK_ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/common/entry.c")  # the SDK's entry.c plus an early entry
set(NFSMW_SDK_MINHOOK_DIR  "${SDK}/extern/minhook")
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Same runtime linkage as the SDK's own toolchain file: the game folder has no MinGW DLLs.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -Wl,--kill-at")
include("${SDK}/cmake/nfsmw_sdkHelpers.cmake")

# Header-only helpers shared by the mods.
set(COMMON "${CMAKE_CURRENT_SOURCE_DIR}/common")

nfsmw_add_plugin(nfsmw_custom_menus SOURCES custom_menus/src/main.cpp custom_menus/src/title_icon.cpp custom_menus/src/text_guard.cpp custom_menus/src/controls_rows.cpp custom_menus/src/skip_dialogs.cpp custom_menus/src/simplify_confirmations.cpp custom_menus/src/cycle_strips.cpp custom_menus/src/resume_career.cpp custom_menus/src/skip_movies.cpp custom_menus/src/translations.cpp custom_menus/src/language.cpp custom_menus/src/slider_meter.cpp)  # the Options menu framework
nfsmw_add_plugin(nfsmw_camera_settings SOURCES camera_settings/src/main.cpp camera_settings/src/previous_camera.cpp camera_settings/src/camera_cycle.cpp camera_settings/src/mirror.cpp camera_settings/src/smoothing.cpp camera_settings/src/acceleration.cpp camera_settings/src/tilt.cpp)          # camera presets (a page of nfsmw_custom_menus)
nfsmw_add_plugin(mets_controller_support SOURCES controller_support/src/main.cpp controller_support/src/settings.cpp controller_support/src/button_icons.cpp controller_support/src/menu_buttons.cpp controller_support/src/input.cpp controller_support/src/bindings.cpp controller_support/src/controls_menu.cpp controller_support/src/pad_sets.cpp controller_support/src/extra_device.cpp controller_support/src/look.cpp controller_support/src/look_options.cpp controller_support/src/rumble.cpp controller_support/src/controls_options.cpp controller_support/src/default_layout.cpp controller_support/src/controller_page.cpp controller_support/src/device_settings.cpp controller_support/src/profile.cpp controller_support/src/steam_pads.cpp controller_support/src/confirm_changes.cpp controller_support/src/menu_pages.cpp controller_support/src/world_map.cpp controller_support/src/showroom.cpp controller_support/src/new_controllers.cpp)          # controller input and rumble (a page of nfsmw_custom_menus)
nfsmw_add_plugin(nfsmw_dev_tools SOURCES dev_tools/src/main.cpp dev_tools/src/diagnostics.cpp dev_tools/src/quick_start.cpp dev_tools/src/free_camera.cpp dev_tools/src/game_speed.cpp dev_tools/src/background.cpp dev_tools/src/automation.cpp dev_tools/src/screenshot.cpp)  # for making mods: logs, quicker start-up
target_link_libraries(nfsmw_dev_tools PRIVATE shell32)
nfsmw_add_plugin(nfsmw_example_options_page SOURCES custom_menus/example_options_page/src/main.cpp)  # an example of a page for nfsmw_custom_menus (not part of set_mods.py)
nfsmw_add_plugin(nfsmw_split_screen_support SOURCES split_screen_support/src/main.cpp split_screen_support/src/menu_entry.cpp split_screen_support/src/fixes.cpp split_screen_support/src/state_log.cpp split_screen_support/src/events.cpp split_screen_support/src/players.cpp split_screen_support/src/split_view.cpp split_screen_support/src/reflections.cpp split_screen_support/src/hud_views.cpp split_screen_support/src/hud_packages.cpp split_screen_support/src/hud_players.cpp split_screen_support/src/minimap.cpp split_screen_support/src/minimap_option.cpp split_screen_support/src/player_count.cpp split_screen_support/src/more_players.cpp split_screen_support/src/extra_streaming.cpp split_screen_support/src/extra_cameras.cpp split_screen_support/src/start_grid.cpp split_screen_support/src/after_race.cpp split_screen_support/src/ai_in_split_screen.cpp split_screen_support/src/player_settings.cpp split_screen_support/src/view_api.cpp split_screen_support/src/race_intro.cpp split_screen_support/src/streaming_pool.cpp split_screen_support/src/streaming_log.cpp split_screen_support/src/skin_slots.cpp split_screen_support/src/engine_sounds.cpp split_screen_support/src/split_volume.cpp)  # two player split screen: the Quick Race menu entry and the fixes it needs
nfsmw_add_plugin(nfsmw_improved_ai_opponents SOURCES improved_ai_opponents/src/main.cpp improved_ai_opponents/src/ai_control.cpp improved_ai_opponents/src/settings.cpp improved_ai_opponents/src/menu.cpp improved_ai_opponents/src/car_stats.cpp improved_ai_opponents/src/path_cost.cpp improved_ai_opponents/src/overlay.cpp improved_ai_opponents/src/lines.cpp improved_ai_opponents/src/segment_draw.cpp improved_ai_opponents/src/road_inspector.cpp improved_ai_opponents/src/road_geometry.cpp improved_ai_opponents/src/nav_landing.cpp improved_ai_opponents/src/race_area.cpp improved_ai_opponents/src/route_watch.cpp improved_ai_opponents/src/route_extend.cpp improved_ai_opponents/src/obstacles.cpp improved_ai_opponents/src/nitro.cpp improved_ai_opponents/src/perfect_launch.cpp)  # the racing AI for players (an improved one, per player) and what it does drawn in the picture (needs nfsmw_split_screen_support)
foreach(mod nfsmw_custom_menus nfsmw_camera_settings mets_controller_support nfsmw_dev_tools nfsmw_example_options_page nfsmw_split_screen_support nfsmw_improved_ai_opponents)
    target_include_directories(${mod} PRIVATE "${COMMON}")
endforeach()
