cmake_minimum_required(VERSION 3.28)
project(ArcVP)
set(CMAKE_CXX_STANDARD 20)
file(GLOB IMGUI_SRC "lib/imgui/*.cpp")
file(GLOB ArcVP_SRC "./src/*")
# Find packages

set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};D:/SDL2_ttf-2.24.0/cmake")

message(${CMAKE_PREFIX_PATH})

find_package(OpenGL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

set(FFMPEG_LIBRARIES avcodec avformat avutil swscale swresample)
set(FFMPEG_INCLUDE_DIRS "D:/FFmpeg/include")
set(FFMPEG_LIBRARY_DIRS "D:/FFmpeg/lib")
# Include directory
message(${FFMPEG_INCLUDE_DIRS} ${FFMPEG_LIBRARY_DIRS})
include_directories(${FFMPEG_INCLUDE_DIRS} "./include")
add_compile_definitions(-DSDL_MAIN_HANDLED)
# Link directory
link_directories(${FFMPEG_LIBRARY_DIRS})
# Define Executable
add_executable(ArcVP main.cc
        include/Player.h
        src/openclose.cc
#        src/arcvp_decode_handler.cc
        src/packet_decode.cc
        src/seek.cc
        include/Channel.h
        src/audio_decode.cc
        src/video_decode.cc
        include/sync_state.h
        include/media_context.h
        include/audio_device.h)
# Link Libraries
target_link_libraries(ArcVP OpenGL::GL ${FFMPEG_LIBRARIES} spdlog::spdlog SDL2::SDL2 SDL2_ttf::SDL2_ttf)