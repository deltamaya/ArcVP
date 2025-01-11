cmake_minimum_required(VERSION 3.28)
project(ArcVP)
set(CMAKE_CXX_STANDARD 20)
file(GLOB IMGUI_SRC "lib/imgui/*.cpp")
file(GLOB ArcVP_SRC "./src/*")
# Find packages
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(SDL2 REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)
# Include directory
message(${FFMPEG_INCLUDE_DIRS} ${FFMPEG_LIBRARY_DIRS})
include_directories(${FFMPEG_INCLUDE_DIRS} "lib/imgui" "./include")
add_compile_definitions(-DSDL_MAIN_HANDLED)
# Link directory
link_directories(${FFMPEG_LIBRARY_DIRS} )
# Define Executable
add_executable(ArcVP main.cc ${ArcVP_SRC} ${IMGUI_SRC} "lib/imgui/backends/imgui_impl_sdl2.cpp" "lib/imgui/backends/imgui_impl_opengl3.cpp")
# Link Libraries
target_link_libraries(ArcVP OpenGL::GL ${FFMPEG_LIBRARIES}  spdlog::spdlog SDL2::SDL2)