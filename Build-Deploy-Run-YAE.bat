@echo off
setlocal EnableExtensions

set "REPO=%~dp0"
set "GAME=H:\YAE\Original\You Are Empty"
set "PROJECT=%REPO%msvc\QindieGL-src.vcxproj"
set "BUILT_DLL=%REPO%bin\ReleaseNoRemixMods\opengl32.dll"
set "BUILT_INI=%REPO%bin\ReleaseNoRemixMods\QindieGL.ini"
set "TARGET_DLL=%GAME%\QindieGL-traced.dll"
set "TARGET_INI=%GAME%\QindieGL.ini"
set "GAME_EXE=%GAME%\YOU_ARE_EMPTY.exe"

if not exist "%GAME_EXE%" (
    echo ERROR: game executable was not found:
    echo   %GAME_EXE%
    pause
    exit /b 1
)

rem GLIntercept owns opengl32.dll. Never replace it with the QindieGL build.
if not exist "%GAME%\opengl32.dll" (
    echo ERROR: GLIntercept loader is missing:
    echo   %GAME%\opengl32.dll
    pause
    exit /b 1
)

tasklist /FI "IMAGENAME eq YOU_ARE_EMPTY.exe" 2>NUL | find /I "YOU_ARE_EMPTY.exe" >NUL
if not errorlevel 1 (
    echo ERROR: YOU_ARE_EMPTY.exe is already running.
    echo Exit the game before deploying a new DLL.
    pause
    exit /b 1
)

if /I "%~1"=="--no-build" goto deploy

set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
if exist "%MSBUILD%" goto build
set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if exist "%MSBUILD%" goto build
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if exist "%MSBUILD%" goto build
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
if exist "%MSBUILD%" goto build
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
if exist "%MSBUILD%" goto build

echo ERROR: MSBuild was not found.
pause
exit /b 1

:build
echo Building QindieGL ReleaseNoRemixMods Win32...
"%MSBUILD%" "%PROJECT%" /t:Build /m /p:Configuration=ReleaseNoRemixMods /p:Platform=Win32 /p:PlatformToolset=v145 /verbosity:minimal
if errorlevel 1 (
    echo ERROR: build failed.
    pause
    exit /b 1
)

:deploy
if not exist "%BUILT_DLL%" (
    echo ERROR: built DLL was not found:
    echo   %BUILT_DLL%
    pause
    exit /b 1
)

echo Deploying QindieGL-traced.dll...
copy /Y "%BUILT_DLL%" "%TARGET_DLL%" >NUL
if errorlevel 1 (
    echo ERROR: could not deploy %TARGET_DLL%
    pause
    exit /b 1
)

if exist "%BUILT_INI%" copy /Y "%BUILT_INI%" "%TARGET_INI%" >NUL

echo Launching You Are Empty...
start "" /D "%GAME%" "%GAME_EXE%"
if errorlevel 1 (
    echo ERROR: game launch failed.
    pause
    exit /b 1
)

echo Done. The GLIntercept opengl32.dll loader was left untouched.
exit /b 0
