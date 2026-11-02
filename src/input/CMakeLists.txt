# Xbox Input → XInput Compatibility Layer, and the host-side input rules

# XBOXRECOMP_SDL3 (root CMakeLists.txt) selects SDL3 for pads, and off
# Windows for the whole host side.

add_library(xbox_input STATIC
    xinput_device.c
    keyboard.c          # the key table and the one key map (every host)
    input_edges.c
)

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The mapping rules (bindings, remapping, deadzones), also buildable alone;
# keyboard.c resolves its key map through them.
add_subdirectory(mapping)
target_link_libraries(xbox_input PUBLIC xbox_input_map)

if(WIN32)
    # Mouse+keyboard pad (RECOMP_MKB) reads Win32 raw input; the host input
    # layer (input_host.c) reads XInput pads, or SDL3's with XBOXRECOMP_SDL3.
    target_sources(xbox_input PRIVATE mkb_input.c input_host.c)
    # mingw-w64's libxinput.a imports XINPUT1_3.dll, the DirectX June 2010
    # redistributable: stock Windows 10/11 lacks it and the exe dies with
    # 0xC0000135 before main. Proton ships it, so only native Windows shows
    # this. XINPUT1_4.dll is part of Windows since 8. The Windows SDK's
    # xinput.lib already imports 1_4 and the SDK has no xinput1_4.lib, so
    # MSVC keeps the plain name. tests/input_xinput_imports checks the result.
    target_link_libraries(xbox_input PUBLIC platform
        $<IF:$<BOOL:${MINGW}>,xinput1_4,xinput> winmm)
    if(XBOXRECOMP_SDL3)
        list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake)
        include(xbox_sdl3)
        target_link_libraries(xbox_input PUBLIC SDL3::SDL3-static)
        target_compile_definitions(xbox_input PUBLIC XBOXRECOMP_HAVE_SDL3=1)
    endif()
elseif(XBOXRECOMP_SDL3)
    # Off Windows SDL3 is the system's, shared: the program's window and
    # sound use it as well, and a process cannot hold SDL2 and SDL3 at once
    # (they export the same names). Pads go through the host input layer, and
    # the program's window feeds keyboard.c's table itself.
    find_package(SDL3 REQUIRED CONFIG)
    target_sources(xbox_input PRIVATE input_host.c)
    target_link_libraries(xbox_input PUBLIC platform SDL3::SDL3)
    target_compile_definitions(xbox_input PUBLIC XBOXRECOMP_HAVE_SDL3=1)
else()
    find_package(SDL2 REQUIRED)
    # The SDL window's key and focus events into that table. The host input
    # layer (input_host.c) builds here too, with no pad backend: the USB
    # model asks it whether a port has started it.
    target_sources(xbox_input PRIVATE keyboard_sdl.c input_host.c)
    target_link_libraries(xbox_input PUBLIC platform SDL2::SDL2)
endif()

if(MSVC)
    target_compile_options(xbox_input PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
    target_link_libraries(xbox_input PUBLIC m)
endif()
