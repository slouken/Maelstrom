add_library(netlogic STATIC
	about.cpp
    blit.cpp
    game.cpp
    globals.h
    logic.cpp
    make.cpp
    make.h
    netlogic.h
    netplay.cpp
    netplay.h
    object.cpp
    object.h
    objects.cpp
    objects.h
    player.cpp
    player.h
    protocol.h
    shinobi.h
)
target_include_directories(netlogic PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(netlogic PRIVATE SDLscreen)
target_link_libraries(netlogic PRIVATE SDL2_net::SDL2_net)
