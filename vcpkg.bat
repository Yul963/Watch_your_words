@echo off

git clone https://github.com/microsoft/vcpkg %~dp0\vcpkg
call %~dp0\vcpkg\bootstrap-vcpkg.bat
%~dp0\vcpkg\vcpkg install google-cloud-cpp[core,speech]
timeout /t 5