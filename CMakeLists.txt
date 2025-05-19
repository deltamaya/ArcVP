cmake_minimum_required(VERSION 3.28)
project(ArcVP)
set(CMAKE_CXX_STANDARD 20)
file(GLOB IMGUI_SRC "lib/imgui/*.cpp")
file(GLOB ArcVP_SRC "./src/*")
# Find packages


find_package(OpenGL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(FFMPEG_LIBRARIES avcodec avformat avutil swscale swresample)
set(FFMPEG_INCLUDE_DIRS "D:/FFmpeg/include")
set(FFMPEG_LIBRARY_DIRS "D:/FFmpeg/lib")
# Include directory
message(${FFMPEG_INCLUDE_DIRS} ${FFMPEG_LIBRARY_DIRS})
include_directories(${FFMPEG_INCLUDE_DIRS} "./include" "./imgui" )
add_compile_definitions(-DSDL_MAIN_HANDLED)

file(GLOB IMGUI_SRC "./imgui/*.cpp")

# Link directory
link_directories(${FFMPEG_LIBRARY_DIRS})
# Define Executable
add_executable(ArcVP main.cc
        src/openclose.cc
        src/packet_decode.cc
        src/seek.cc
        src/audio_decode.cc
        src/video_decode.cc
        include/sync_state.h
        include/media_context.h
        include/audio_device.h
        include/frame_queue.h
        include/decode_worker.h
        src/control-panel.cc
        src/control.cc
        imgui/backends/imgui_impl_sdl3.cpp
        imgui/backends/imgui_impl_sdlrenderer3.cpp
        ${IMGUI_SRC}

)
# Link Libraries
target_link_libraries(ArcVP OpenGL::GL ${FFMPEG_LIBRARIES} spdlog::spdlog SDL3::SDL3 SDL3_ttf::SDL3_ttf nlohmann_json::nlohmann_json)

add_custom_command(TARGET ArcVP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/bin/SDL3.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake-build-debug/SDL3.dll
)