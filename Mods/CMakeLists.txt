cmake_minimum_required(VERSION 3.16)
project(nfsmw_mods LANGUAGES C CXX)

# Build our mods against the SDK clone without going through its root
# CMakeLists (we only want the plugin helper, not the examples/install rules).
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/../Tools/nfsmw-2005-sdk")
set(NFSMW_SDK_INCLUDE_DIR  "${SDK}/include")
set(NFSMW_SDK_ENTRY_SOURCE "${SDK}/src/entry.c")
set(NFSMW_SDK_MINHOOK_DIR  "${SDK}/extern/minhook")
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Same runtime linkage as the SDK's own toolchain file: the game folder has no MinGW DLLs.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -Wl,--kill-at")
include("${SDK}/cmake/nfsmw_sdkHelpers.cmake")

# Header-only helpers shared by the mods.
set(COMMON "${CMAKE_CURRENT_SOURCE_DIR}/common")

nfsmw_add_plugin(nfsmw_custom_menus SOURCES custom_menus/main.cpp custom_menus/title_icon.cpp)  # the Options menu framework
nfsmw_add_plugin(nfsmw_camera_settings SOURCES camera_settings/main.cpp)          # camera presets (a page of nfsmw_custom_menus)
nfsmw_add_plugin(nfsmw_controller_support SOURCES controller_support/main.cpp controller_support/settings.cpp controller_support/button_icons.cpp controller_support/menu_buttons.cpp controller_support/input.cpp controller_support/bindings.cpp controller_support/controls_menu.cpp controller_support/look.cpp controller_support/rumble.cpp controller_support/rumble_page.cpp)          # controller input and rumble (a page of nfsmw_custom_menus)
foreach(mod nfsmw_custom_menus nfsmw_camera_settings nfsmw_controller_support)
    target_include_directories(${mod} PRIVATE "${COMMON}")
endforeach()
