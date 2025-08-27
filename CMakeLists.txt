cmake_minimum_required(VERSION 3.0...4.0)
project(Maelstrom C CXX)

include(GNUInstallDirs)

find_package(SDL3 REQUIRED)
find_package(SDL2_net REQUIRED)

if(WIN32)
    set(GAME_INSTALLDIR ".")
else()
    set(GAME_INSTALLDIR "${CMAKE_INSTALL_PREFIX}/games/${PROJECT_NAME}")
endif()
add_compile_definitions("-DLIBDIR=\"${GAME_INSTALLDIR}\"")


if(NOT TARGET SDL2_net::on-SDL3)
    message(FATAL_ERROR "Maelstrom is WIP, and requires SDL2_net on top of SDL3")
endif()

if(MSVC)
    add_compile_options(-W3)
else()
    add_compile_options(-Wall)
    add_compile_options(-Wextra)
    add_compile_options(-Wno-unused-parameter)
endif()

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/maclib")

add_subdirectory(netlogic)
add_subdirectory(screenlib)
add_subdirectory(maclib)

set(MAELSTROM_SOURCES
    Maelstrom.h
    Maelstrom_Globals.h
    buttonlist.h
    checksum.cpp
    checksum.h
    colortable.h
    controls.cpp
    controls.h
    dialog.cpp
    dialog.h
    fastrand.cpp
    fastrand.h
    init.cpp
    load.cpp
    load.h
    logic.h
    main.cpp
    myerror.cpp
    myerror.h
    netscore.cpp
    netscore.h
    public_key.h
    rect.cpp
    rect.h
    scores.cpp
    scores.h

    Maelstrom.rc
)

add_executable(Maelstrom WIN32 ${MAELSTROM_SOURCES})
target_link_libraries(Maelstrom PRIVATE netlogic SDLscreen SDLmac)
target_link_libraries(Maelstrom PRIVATE SDL3::SDL3)
target_link_libraries(Maelstrom PRIVATE SDL2_net::SDL2_net)

file(GLOB images "Images/*.bmp" "Images/*.cicn")
install(TARGETS Maelstrom DESTINATION "${GAME_INSTALLDIR}")
install(
    FILES
        Maelstrom_Fonts
        Maelstrom_Sounds
        Maelstrom_Sprites
        icon.bmp
        icon.xpm
    DESTINATION "${GAME_INSTALLDIR}")
install(FILES ${images} DESTINATION "${GAME_INSTALLDIR}/Images")

file(GLOB docs "Docs/*.txt" "README*")
install(FILES ${docs} "COPYING.txt" "COPYING.GPL" DESTINATION "${GAME_INSTALLDIR}/Docs")
