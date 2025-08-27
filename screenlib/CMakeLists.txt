add_library(SDLscreen STATIC
    SDL_FrameBuf.cpp
    SDL_FrameBuf.h
    pixel.cpp
    pixel.h
)
target_include_directories(SDLscreen PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(SDLscreen PRIVATE SDL3::SDL3)
target_include_directories(SDLscreen PUBLIC $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
