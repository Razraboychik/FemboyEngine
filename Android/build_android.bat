@echo off
if "%ANDROID_NDK%"=="" set ANDROID_NDK=C:\Android\ndk
if not exist "%ANDROID_NDK%" (
    echo ANDROID_NDK not found: %ANDROID_NDK%
    echo Set ANDROID_NDK env var to your NDK path.
    pause
    exit /b 1
)
cd /d %~dp0\..
if not exist build-android mkdir build-android
cd build-android
cmake .. -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=..\cmake\android_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build . --config Release
echo Build done. Output in build-android/
pause