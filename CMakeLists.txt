cmake_minimum_required(VERSION 3.20)
project(MyCobotDraw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR "MyCobotDraw uses the Win32 API and can only be built for Windows.")
endif()

add_executable(MyCobotDraw WIN32
    src/main.cpp
    src/AppWindow.cpp
    src/Whiteboard.cpp
    src/SimView.cpp
    src/MyCobotSerial.cpp
    src/PathPlan.cpp
    src/DrawingSender.cpp
    src/Kinematics.cpp
)

target_include_directories(MyCobotDraw PRIVATE src)

# UNICODE Win32 API
target_compile_definitions(MyCobotDraw PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(MyCobotDraw PRIVATE
    user32
    gdi32
    comdlg32
    comctl32
    msimg32
)

if (MSVC)
    target_compile_options(MyCobotDraw PRIVATE /W4 /utf-8)
endif()
