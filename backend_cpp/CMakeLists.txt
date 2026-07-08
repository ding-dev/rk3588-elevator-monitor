cmake_minimum_required(VERSION 3.10)
project(ElevatorTest C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

option(ENABLE_LVGL_UI "Enable LVGL UI rendering in my_test" ON)
option(ENABLE_RKNN_DETECTION "Enable RKNN object detection in camera stream" ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0)

set(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LOCAL_OPENCV_INCLUDE_DIR ${PROJECT_ROOT_DIR}/vedio_ai/opencv/include/opencv4)
set(LOCAL_OPENCV_LIB_DIR ${PROJECT_ROOT_DIR}/vedio_ai/opencv/lib)
set(LOCAL_RKNN_INCLUDE_DIR ${PROJECT_ROOT_DIR}/vedio_ai/rknn/include)
set(LOCAL_RKNN_LIBRARY ${PROJECT_ROOT_DIR}/vedio_ai/rknn/lib/librknnrt.so)

set(LOCAL_OPENCV_LIBS
    ${LOCAL_OPENCV_LIB_DIR}/libopencv_core.so
    ${LOCAL_OPENCV_LIB_DIR}/libopencv_imgproc.so
)

set(MEDIA_OPENCV_INCLUDE_DIRS "")
set(MEDIA_OPENCV_LIBS "")
set(ACTUAL_ENABLE_RKNN_DETECTION ${ENABLE_RKNN_DETECTION})

if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(MEDIA_OPENCV_INCLUDE_DIRS ${LOCAL_OPENCV_INCLUDE_DIR})
    set(MEDIA_OPENCV_LIBS ${LOCAL_OPENCV_LIBS})
else()
    find_package(OpenCV QUIET COMPONENTS core imgproc)
    if(OpenCV_FOUND)
        set(MEDIA_OPENCV_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
        set(MEDIA_OPENCV_LIBS ${OpenCV_LIBS})
    else()
        message(WARNING "Host OpenCV not found; falling back to bundled target OpenCV libraries.")
        set(MEDIA_OPENCV_INCLUDE_DIRS ${LOCAL_OPENCV_INCLUDE_DIR})
        set(MEDIA_OPENCV_LIBS ${LOCAL_OPENCV_LIBS})
    endif()

    if(ENABLE_RKNN_DETECTION)
        message(WARNING "ENABLE_RKNN_DETECTION requested on non-target architecture; compiling with RKNN disabled.")
        set(ACTUAL_ENABLE_RKNN_DETECTION OFF)
    endif()
endif()

set(GSTREAMER_RESOLVED_INCLUDE_DIRS "")
if(CMAKE_CROSSCOMPILING
   AND DEFINED CMAKE_SYSROOT
   AND NOT EXISTS "${CMAKE_SYSROOT}/usr/include/gstreamer-1.0/gst/gst.h")
    set(GSTREAMER_RESOLVED_INCLUDE_DIRS
        "/usr/include/gstreamer-1.0"
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/glib-2.0/include")

    if(EXISTS "/usr/include/orc-0.4")
        list(APPEND GSTREAMER_RESOLVED_INCLUDE_DIRS "/usr/include/orc-0.4")
    endif()
else()
    foreach(dir IN LISTS GSTREAMER_INCLUDE_DIRS)
        if(IS_DIRECTORY "${dir}")
            list(APPEND GSTREAMER_RESOLVED_INCLUDE_DIRS "${dir}")
        endif()
    endforeach()
endif()

if(NOT GSTREAMER_RESOLVED_INCLUDE_DIRS)
    message(FATAL_ERROR "No usable GStreamer include directories were found for this build.")
endif()

set(GSTREAMER_RESOLVED_LIBRARY_DIRS "")
if(CMAKE_CROSSCOMPILING AND DEFINED CMAKE_SYSROOT)
    foreach(dir IN LISTS GSTREAMER_LIBRARY_DIRS)
        if(dir MATCHES "^${CMAKE_SYSROOT}/.*" AND IS_DIRECTORY "${dir}")
            list(APPEND GSTREAMER_RESOLVED_LIBRARY_DIRS "${dir}")
        endif()
    endforeach()
else()
    set(GSTREAMER_RESOLVED_LIBRARY_DIRS ${GSTREAMER_LIBRARY_DIRS})
endif()

set(GSTREAMER_RESOLVED_LIBRARIES "")
foreach(lib IN LISTS GSTREAMER_LIBRARIES)
    if(CMAKE_CROSSCOMPILING AND DEFINED CMAKE_SYSROOT)
        set(resolved_lib "")
        foreach(base_dir
                "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
                "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu")
            if(EXISTS "${base_dir}/lib${lib}.so")
                set(resolved_lib "${base_dir}/lib${lib}.so")
                break()
            endif()
            if(EXISTS "${base_dir}/lib${lib}.so.0")
                set(resolved_lib "${base_dir}/lib${lib}.so.0")
                break()
            endif()
        endforeach()

        if(resolved_lib)
            list(APPEND GSTREAMER_RESOLVED_LIBRARIES "${resolved_lib}")
        else()
            list(APPEND GSTREAMER_RESOLVED_LIBRARIES "${lib}")
        endif()
    else()
        list(APPEND GSTREAMER_RESOLVED_LIBRARIES "${lib}")
    endif()
endforeach()

add_library(elevator_core
    src/protocol/ElevatorFrameParser.cpp
)
target_include_directories(elevator_core PUBLIC include)

add_library(elevator_services
    src/media/CameraStreamService.cpp
    src/media/MediaManager.cpp
    src/media/RknnDetector.cpp
    src/media/VideoAdPlayer.cpp
    src/network/UdpCommandServer.cpp
    src/network/UdpTelemetryClient.cpp
)
target_include_directories(elevator_services PUBLIC include)
target_include_directories(elevator_services PRIVATE
    ${GSTREAMER_RESOLVED_INCLUDE_DIRS}
    ${MEDIA_OPENCV_INCLUDE_DIRS}
)
if(ACTUAL_ENABLE_RKNN_DETECTION)
    target_include_directories(elevator_services PRIVATE ${LOCAL_RKNN_INCLUDE_DIR})
    target_compile_definitions(elevator_services PRIVATE ENABLE_RKNN_DETECTION=1)
endif()
target_compile_options(elevator_services PRIVATE ${GSTREAMER_CFLAGS_OTHER})
target_link_directories(elevator_services PRIVATE ${GSTREAMER_RESOLVED_LIBRARY_DIRS})
target_link_libraries(elevator_services
    PUBLIC Threads::Threads
    PRIVATE
        ${GSTREAMER_RESOLVED_LIBRARIES}
        ${MEDIA_OPENCV_LIBS}
)
if(ACTUAL_ENABLE_RKNN_DETECTION)
    target_link_libraries(elevator_services PRIVATE ${LOCAL_RKNN_LIBRARY})
endif()

add_library(elevator_sim
    src/simulation/ElevatorEnvSimulator.cpp
)
target_include_directories(elevator_sim PUBLIC include)
target_link_libraries(elevator_sim PUBLIC elevator_core Threads::Threads)

add_library(elevator_app
    src/app/ApplicationRuntime.cpp
)
target_include_directories(elevator_app PUBLIC include)
target_link_libraries(elevator_app PUBLIC elevator_core elevator_services elevator_sim Threads::Threads)

if(ENABLE_LVGL_UI)
    find_package(SDL2 REQUIRED)

    set(LV_BUILD_CONF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode CACHE PATH "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/lvgl
                     ${CMAKE_CURRENT_BINARY_DIR}/lvgl_build)

    add_library(elevator_gui
        src/ui/ElevatorUiController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/src/hal/hal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/src/mouse_cursor_icon.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/lvgl_ui/ui.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/lvgl_ui/ui_helpers.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/lvgl_ui/screens/ui_Screen1.c
    )

    target_compile_definitions(elevator_gui PUBLIC LV_CONF_INCLUDE_SIMPLE)

    target_include_directories(elevator_gui PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/src/hal
        ${CMAKE_CURRENT_SOURCE_DIR}/../lv_port_pc_vscode/lvgl_ui
        ${SDL2_INCLUDE_DIRS}
    )

    target_link_libraries(elevator_gui PUBLIC
        elevator_core
        lvgl
        ${SDL2_LIBRARIES}
        Threads::Threads
        m
    )
endif()

add_executable(my_test src/main.cpp)
target_include_directories(my_test PUBLIC include)
target_link_libraries(my_test PRIVATE elevator_app Threads::Threads)

if(ENABLE_LVGL_UI)
    target_link_libraries(my_test PRIVATE elevator_gui)
endif()
