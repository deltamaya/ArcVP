cmake_minimum_required(VERSION 3.28)
project(ArcVP)
set(CMAKE_CXX_STANDARD 20)
file(GLOB IMGUI_SRC "lib/imgui/*")

set(FFMPEG_INCLUDE_DIRS "D:/FFmpeg/include")
set(FFMPEG_LIBRARY_DIRS "D:/FFmpeg/lib")

# Find packages
# find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
# find_package(glfw3 REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
# Include directory
include_directories(${FFMPEG_INCLUDE_DIRS} "lib/imgui"  "lib/imgui/backends"  "./include")

# Link directory
link_directories(${FFMPEG_LIBRARY_DIRS})

# Define Executable
add_executable(ArcVP main.cc
        ${IMGUI_SRC}
        "lib/imgui/backends/imgui_impl_glfw.cpp"
        "lib/imgui/backends/imgui_impl_opengl3.cpp"
        include/arcvp.h
        src/arcvp_openclose.cc
        src/arcvp_decoder.cc)
#target_compile_options(ArcVP PRIVATE -fsanitize=address)
target_link_directories(ArcVP PRIVATE "C:\\Program Files\\LLVM\\lib\\clang\\19\\lib\\windows")

#target_link_options(ArcVP PRIVATE /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
# Link Libraries
target_link_libraries(ArcVP PRIVATE OpenGL::GL avcodec avformat avutil swscale swresample spdlog::spdlog glfw)