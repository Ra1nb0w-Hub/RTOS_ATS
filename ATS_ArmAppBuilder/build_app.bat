@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SCRIPT_DIR%\build"
set "TOOLCHAIN_FILE=%SCRIPT_DIR%\tools\arm-none-eabi-toolchain.cmake"
set "ARM_TOOLCHAIN_BIN_DIR=%SCRIPT_DIR%\tools\gcc-arm-none-eabi-lite\bin"

if /I "%~1"=="clean" goto :clean
if /I "%~1"=="-clean" goto :clean
if /I "%~1"=="--clean" goto :clean
if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "MSP_STACK_KB=%~1"
set "RTOS_RAM_KB=%~2"
set "BOARD_RAM_KB=%~3"
set "BOARD_ROM_KB=%~4"

if not defined MSP_STACK_KB set "MSP_STACK_KB=4"
if not defined RTOS_RAM_KB set "RTOS_RAM_KB=256"
if not defined BOARD_RAM_KB set "BOARD_RAM_KB=1024"
if not defined BOARD_ROM_KB set "BOARD_ROM_KB=1024"

if %MSP_STACK_KB%==0 (
    echo ERROR: Parameter `MSP_STACK_KB` must not be 0
    exit /b 1
)
if %RTOS_RAM_KB%==0 (
    echo ERROR: Parameter `RTOS_RAM_KB` must not be 0
    exit /b 1
)
if %BOARD_RAM_KB%==0 (
    echo ERROR: Parameter `BOARD_RAM_KB` must not be 0
    exit /b 1
)
if %BOARD_ROM_KB%==0 (
    echo ERROR: Parameter `BOARD_ROM_KB` must not be 0
    exit /b 1
)

echo Parameter `MSP_STACK_KB`: %MSP_STACK_KB%
echo Parameter `RTOS_RAM_KB`: %RTOS_RAM_KB%
echo Parameter `BOARD_RAM_KB`: %BOARD_RAM_KB%
echo Parameter `BOARD_ROM_KB`: %BOARD_ROM_KB%

set /a "SUM_RAM=RTOS_RAM_KB + MSP_STACK_KB"
if %SUM_RAM% gtr %BOARD_RAM_KB% (
    echo ERROR: MSP_STACK_KB^(%MSP_STACK_KB%K^) + RTOS_RAM_KB^(%RTOS_RAM_KB%K^) = %SUM_RAM%K exceeds BOARD_RAM_KB^(%BOARD_RAM_KB%K^)
    exit /b 1
)

set /a "MAX_RAM=4096"
if %BOARD_RAM_KB% gtr %MAX_RAM% (
    echo ERROR: BOARD_RAM_KB^(%BOARD_RAM_KB%K^) exceeds MPS2 AN385 maximum RAM of %MAX_RAM%K ^(4MB^)
    exit /b 1
)

call :ResolveTool CMAKE_EXE ATS_CMAKE "%SCRIPT_DIR%\tools\cmake\bin\cmake.exe" cmake.exe
if errorlevel 1 exit /b 1
call :ResolveTool MAKE_EXE ATS_MAKE "%SCRIPT_DIR%\tools\make.exe" make.exe
if errorlevel 1 exit /b 1
if not exist "%ARM_TOOLCHAIN_BIN_DIR%\arm-none-eabi-gcc.exe" (
    echo ARM GCC not found: %ARM_TOOLCHAIN_BIN_DIR%\arm-none-eabi-gcc.exe
    exit /b 1
)
set "ATS_ARM_TOOLCHAIN_BIN_DIR=%ARM_TOOLCHAIN_BIN_DIR%"

@REM echo Resolved cmake: %CMAKE_EXE%
@REM echo Resolved make: %MAKE_EXE%
@REM echo Resolved ARM GCC bin: %ATS_ARM_TOOLCHAIN_BIN_DIR%

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%" >nul 2>&1
if errorlevel 1 (
    echo Failed to create build directory: %BUILD_DIR%
    exit /b 1
)

"%CMAKE_EXE%" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM:FILEPATH=%MAKE_EXE% -DCMAKE_TOOLCHAIN_FILE:FILEPATH=%TOOLCHAIN_FILE% -DATS_MSP_STACK_KB=%MSP_STACK_KB% -DATS_RTOS_RAM_KB=%RTOS_RAM_KB% -DATS_BOARD_RAM_KB=%BOARD_RAM_KB% -DATS_BOARD_ROM_KB=%BOARD_ROM_KB%
if errorlevel 1 exit /b %errorlevel%

"%CMAKE_EXE%" --build "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

echo Build OK: %BUILD_DIR%\ats_app.elf
exit /b 0

:clean
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
echo Clean OK: %BUILD_DIR%
exit /b 0

:help
echo Usage:
echo   build_app.bat [MSPStackKb] [RTOSRamKb] [BoardRamKb] [BoardRomKb]
echo   build_app.bat clean
echo.
echo Defaults:
echo   MSPStackKb=4  RTOSRamKb=256  BoardRamKb=1024  BoardRomKb=1024
exit /b 0

:ResolveTool
setlocal
set "RESOLVED="
call set "ENV_VALUE=%%%~2%%"
if defined ENV_VALUE set "ENV_VALUE=%ENV_VALUE:"=%"
if defined ENV_VALUE if exist "%ENV_VALUE%" set "RESOLVED=%ENV_VALUE%"
if not defined RESOLVED if exist "%~3" set "RESOLVED=%~3"
if not defined RESOLVED for %%I in (%~4) do if not "%%~$PATH:I"=="" set "RESOLVED=%%~$PATH:I"
if not defined RESOLVED (
    echo %~4 not found. Add it to PATH or set %~2.
    endlocal
    exit /b 1
)
endlocal & set "%~1=%RESOLVED%"
exit /b 0
