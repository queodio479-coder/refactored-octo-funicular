cmake_minimum_required(VERSION 3.20)
project(SoundSkinPanel CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21)
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master)
FetchContent_MakeAvailable(imgui miniaudio stb)

add_library(SoundSkinPanel SHARED main.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(SoundSkinPanel PRIVATE
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${miniaudio_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(SoundSkinPanel PRIVATE d3d9)
target_compile_definitions(SoundSkinPanel PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
set_target_properties(SoundSkinPanel PROPERTIES SUFFIX ".asi" PREFIX "")
