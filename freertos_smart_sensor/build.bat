@echo off
setlocal enabledelayedexpansion
echo.
echo === FreeRTOS Smart Sensor Hub Build ===

set "MINGW=C:\mingw64"
if not exist "%MINGW%\bin\gcc.exe" (
    echo [ERROR] MinGW not found at %MINGW%
    pause & exit /b 1
)
echo MinGW: %MINGW%
set "PATH=%MINGW%\bin;%PATH%"

set "PROJECT=%~dp0"
set "KERNEL=%~dp0..\freertos_kernel"
set "BUILD=%~dp0build"

if not exist "%KERNEL%\tasks.c" (
    echo [ERROR] Kernel not found: %KERNEL%
    pause & exit /b 1
)
if not exist "%BUILD%" mkdir "%BUILD%"

echo Compiling...

set "SRC=%PROJECT%main.c"
set "SRC=%SRC% %KERNEL%\tasks.c %KERNEL%\list.c %KERNEL%\queue.c"
set "SRC=%SRC% %KERNEL%\timers.c %KERNEL%\event_groups.c %KERNEL%\stream_buffer.c"
set "SRC=%SRC% %KERNEL%\portable\MSVC-MingW\port.c %KERNEL%\portable\MemMang\heap_4.c"

set "INC=-I%PROJECT% -I%KERNEL%\include -I%KERNEL%\portable\MSVC-MingW"
set "OUT=-o %BUILD%\smart_sensor.exe"

gcc -O0 -g3 -D_WIN32_WINNT=0x0601 -DprojCOVERAGE_TEST=0 %INC% %OUT% %SRC% -lwinmm

if errorlevel 1 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo  BUILD SUCCESS! %BUILD%\smart_sensor.exe
echo ========================================

if /i "%1"=="run" (
    echo Running...
    "%BUILD%\smart_sensor.exe"
)
endlocal
