cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 14)
add_compile_options("-Wextra")

message(STATUS "ANDROID_ABI=${ANDROID_ABI}")

# hacky
include_directories(".")
add_definitions(-include always_assert.h)

set(target vio_main)
project(${target} CXX)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

if(EXISTS ${ROOT_DIR}/mobile-cv-suite)
    # if mobile-cv-suite exists in the root directory, use it
    set(MCS_TARGET_DIR ${ROOT_DIR}/mobile-cv-suite)
else()
    # otherwise download a pre-built release from Github
    set(MCS_VERSION 1.3.0)
    set(MCS_TARGET_DIR ${ROOT_DIR}/app/.cxx/mobile-cv-suite-${MCS_VERSION})
    if(NOT EXISTS ${MCS_TARGET_DIR})
        set(MCS_ARCHIVE_FN ${CMAKE_CURRENT_BINARY_DIR}/mobile-cv-suite.tar-${MCS_VERSION}.gz)
        file(DOWNLOAD https://github.com/AaltoML/mobile-cv-suite/releases/download/${MCS_VERSION}/mobile-cv-suite.tar.gz ${MCS_ARCHIVE_FN} SHOW_PROGRESS)
        file(MAKE_DIRECTORY ${MCS_TARGET_DIR})
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf ${MCS_ARCHIVE_FN} WORKING_DIRECTORY ${MCS_TARGET_DIR})
    endif()
endif()
set(mobile-cv-suite_DIR "${MCS_TARGET_DIR}")
#find_package(mobile-cv-suite REQUIRED PATHS "${MCS_TARGET_DIR}")

set(VIO_ANDROID_SRC
        camera_worker.cpp
        algorithm_worker.cpp
        algorithm_module.cpp
        algorithm_module_wrappers.cpp
        native_camera_session.cpp
        modules/camera_calibrator.cpp
        modules/recorder.cpp
        opengl/ar_renderer.cpp
        opengl/camera_renderer.cpp
        opengl/ext_ar_renderer.cpp
        opengl/gpu_camera_adapter.cpp
        opengl/util.cpp)

set(VIO_ANDROID_FLAGS "-DDUMMY_FLAG")

option(USE_GPU_EXAMPLES "Compile with GPU examples" OFF)
option(USE_CAMERA_CALIBRATOR "Compile with camera calibrator" ON)

if (USE_GPU_EXAMPLES)
    list(APPEND VIO_ANDROID_FLAGS "-DUSE_GPU_EXAMPLES")
    list(APPEND VIO_ANDROID_SRC modules/gpu_examples.cpp)
endif()

if (USE_CAMERA_CALIBRATOR)
    list(APPEND VIO_ANDROID_FLAGS "-DUSE_CAMERA_CALIBRATOR")
    list(APPEND VIO_ANDROID_SRC modules/camera_calibrator.cpp)
endif()

add_library(${target} SHARED ${VIO_ANDROID_SRC})

set(VIO_ANDROID_LIBS
        "GLESv3"
        mobile-cv-suite::core
        camera2ndk
        mediandk
        android
        log)

option(USE_CUSTOM_VIO "Compile with a custom VIO module" OFF)
if (USE_CUSTOM_VIO)
    # assumes the VIO module also uses mobile-cv-suite
    message(STATUS "Custom VIO build ${target}")
    add_subdirectory("${ROOT_DIR}/custom-vio/viotester-integration/android" build-custom-vio)
    list(APPEND VIO_ANDROID_LIBS ${CUSTOM_VIO_LIBS})
    list(APPEND VIO_ANDROID_FLAGS "-DUSE_CUSTOM_VIO")
else()
    find_package(mobile-cv-suite REQUIRED)
endif()
target_compile_definitions(${target} PRIVATE ${VIO_ANDROID_FLAGS})

target_link_libraries(${target} ${VIO_ANDROID_LIBS})
