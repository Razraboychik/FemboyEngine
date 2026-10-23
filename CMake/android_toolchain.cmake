# Android toolchain for FemboyEngine
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 24) # Android 7.0+
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK})
set(CMAKE_ANDROID_STL_TYPE c++_shared)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_ANDROID_NDK)
    message(FATAL_ERROR "Set ANDROID_NDK environment variable to your NDK path")
endif()

