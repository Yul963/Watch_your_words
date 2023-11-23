@echo off

set VC_PKG_DIR=%CD%\vcpkg
set GOOGLE_CLOUD_INSTALLED=%VC_PKG_DIR%\packages\google-cloud-cpp_x64-windows

if exist %VC_PKG_DIR% (
    echo vcpkg 폴더가 이미 존재합니다. 설치를 진행하지 않습니다.
    goto :EOF
)

if exist %GOOGLE_CLOUD_INSTALLED% (
    echo google-cloud-cpp가 이미 설치되었습니다. 설치를 진행하지 않습니다.
    goto :EOF
)

git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
vcpkg install google-cloud-cpp