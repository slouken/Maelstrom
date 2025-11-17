cmake_minimum_required(VERSION 3.0...4.0)
project(Maelstrom C CXX)

include(GNUInstallDirs)

if(WIN32)
    set(GAME_INSTALLDIR ".")
else()
    set(GAME_INSTALLDIR "${CMAKE_INSTALL_PREFIX}/games/${PROJECT_NAME}")
endif()
add_compile_definitions("-DLIBDIR=\"${GAME_INSTALLDIR}\"")

if(MSVC)
    add_compile_options(-W3)
else()
    add_compile_options(-Wall)
    add_compile_options(-Wextra)
    add_compile_options(-Wno-unused-parameter)
endif()

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/maclib")

add_subdirectory(netlogic)
add_subdirectory(screenlib)
add_subdirectory(maclib)
add_subdirectory(external/SDL EXCLUDE_FROM_ALL)
add_subdirectory(external/SDL_net EXCLUDE_FROM_ALL)

set(MAELSTROM_SOURCES
    Maelstrom.h
    Maelstrom_Globals.h
    buttonlist.h
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
target_link_libraries(Maelstrom PRIVATE SDL2_net)

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
