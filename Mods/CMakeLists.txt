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

nfsmw_add_plugin(nfsmw_custom_menus SOURCES custom_menus/src/main.cpp custom_menus/src/title_icon.cpp custom_menus/src/controls_rows.cpp custom_menus/src/skip_dialogs.cpp custom_menus/src/resume_career.cpp custom_menus/src/skip_movies.cpp custom_menus/src/translations.cpp custom_menus/src/language.cpp custom_menus/src/slider_meter.cpp)  # the Options menu framework
nfsmw_add_plugin(nfsmw_camera_settings SOURCES camera_settings/src/main.cpp camera_settings/src/previous_camera.cpp camera_settings/src/camera_cycle.cpp camera_settings/src/mirror.cpp camera_settings/src/smoothing.cpp camera_settings/src/acceleration.cpp camera_settings/src/tilt.cpp)          # camera presets (a page of nfsmw_custom_menus)
nfsmw_add_plugin(nfsmw_controller_support SOURCES controller_support/src/main.cpp controller_support/src/settings.cpp controller_support/src/button_icons.cpp controller_support/src/menu_buttons.cpp controller_support/src/input.cpp controller_support/src/bindings.cpp controller_support/src/controls_menu.cpp controller_support/src/pad_sets.cpp controller_support/src/extra_device.cpp controller_support/src/look.cpp controller_support/src/look_options.cpp controller_support/src/rumble.cpp controller_support/src/controls_options.cpp controller_support/src/default_layout.cpp controller_support/src/controller_page.cpp controller_support/src/device_settings.cpp controller_support/src/menu_pages.cpp controller_support/src/world_map.cpp controller_support/src/new_controllers.cpp)          # controller input and rumble (a page of nfsmw_custom_menus)
nfsmw_add_plugin(nfsmw_dev_tools SOURCES dev_tools/src/main.cpp dev_tools/src/diagnostics.cpp dev_tools/src/quick_start.cpp)  # for making mods: logs, quicker start-up
target_link_libraries(nfsmw_dev_tools PRIVATE shell32)
nfsmw_add_plugin(nfsmw_example_options_page SOURCES custom_menus/example_options_page/src/main.cpp)  # an example of a page for nfsmw_custom_menus (not part of set_mods.py)
nfsmw_add_plugin(nfsmw_split_screen_support SOURCES split_screen_support/src/main.cpp split_screen_support/src/menu_entry.cpp split_screen_support/src/fixes.cpp split_screen_support/src/state_log.cpp split_screen_support/src/events.cpp split_screen_support/src/players.cpp split_screen_support/src/split_view.cpp split_screen_support/src/minimap.cpp split_screen_support/src/minimap_option.cpp)  # two player split screen: the Quick Race menu entry and the fixes it needs
foreach(mod nfsmw_custom_menus nfsmw_camera_settings nfsmw_controller_support nfsmw_dev_tools nfsmw_example_options_page nfsmw_split_screen_support)
    target_include_directories(${mod} PRIVATE "${COMMON}")
endforeach()
