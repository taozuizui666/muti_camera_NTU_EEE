@echo off

mkdir log
cd msvc
set param=%1

if "%param%"=="x86" (
echo compile Release_x86
devenv uart_cmd.vcxproj /ReBuild "Release|x86" /project uart_cmd.vcxproj -> ..\log\release_x86_log.txt && echo build target sample succeeded! && copy /Y ..\libs\Win32\dll\*.dll ..\Release\Win32 || echo build target sample failed!
) else (
echo compile Release_x64
devenv uart_cmd.vcxproj /ReBuild "Release|x64" /project uart_cmd.vcxproj -> ..\log\release_x64_log.txt && echo build target sample succeeded! && copy /Y ..\libs\x64\dll\*.dll ..\Release\x64 || echo build target sample failed!
)
cd ..

pause