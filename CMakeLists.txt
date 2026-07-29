cmake_minimum_required(VERSION 3.16)
project(DiagnoseLinux CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Dependencies
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(SDL2 REQUIRED sdl2)

# ImGui Source files
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)

# Linux Source Files
set(SOURCES
    main.cpp
    gui.cpp
    hardware_monitor.cpp
    battery_service.cpp
    tool_launcher.cpp
    ram_test.cpp
    stress_test.cpp
    pdf_writer.cpp
    system_repair.cpp
    ${IMGUI_SOURCES}
)

add_executable(DiagnoseLinux ${SOURCES})

target_link_libraries(DiagnoseLinux PRIVATE
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    Threads::Threads
    dl
)
