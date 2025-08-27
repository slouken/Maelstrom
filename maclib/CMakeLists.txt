add_library(SDLmac STATIC
    Mac_Compat.h
    Mac_FontServ.cpp
    Mac_FontServ.h
    Mac_Resource.cpp
    Mac_Resource.h
    Mac_Sound.cpp
    Mac_Sound.h
    Mac_Wave.cpp
    Mac_Wave.h
    applefile.h
    macbinary.h
    bitesex.h
)
target_include_directories(SDLmac PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(SDLmac PRIVATE SDLscreen)
target_link_libraries(SDLmac PRIVATE SDL3::SDL3)

add_executable(macres macres.cpp)
target_link_libraries(macres PRIVATE SDLmac)
target_link_libraries(macres PRIVATE SDL3::SDL3)

add_executable(playwave playwave.cpp)
target_link_libraries(playwave PRIVATE SDLmac)
target_link_libraries(playwave PRIVATE SDL3::SDL3)

add_executable(snd2wav snd2wav.cpp)
target_link_libraries(snd2wav PRIVATE SDLmac)
target_link_libraries(snd2wav PRIVATE SDL3::SDL3)
