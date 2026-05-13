@echo off
setlocal

set "ROOT=%~dp0"
set "DUSK_EXE=%ROOT%build\windows-msvc-dawn-vendor-openxr\dusk.exe"
set "DISC=%ROOT%game.ciso"
set "STEAMVR_RUNTIME=C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json"

if not exist "%DUSK_EXE%" (
    echo Dusk OpenXR executable not found: "%DUSK_EXE%"
    echo Build it with:
    echo   cmake --build --preset windows-msvc-dawn-vendor-openxr --target dusk
    exit /b 1
)

if not exist "%DISC%" (
    echo Game disc image not found: "%DISC%"
    exit /b 1
)

if "%XR_RUNTIME_JSON%"=="" (
    if exist "%STEAMVR_RUNTIME%" (
        set "XR_RUNTIME_JSON=%STEAMVR_RUNTIME%"
    )
)

echo Launching Dusk VR with Vulkan/OpenXR required mode.
if not "%XR_RUNTIME_JSON%"=="" (
    echo XR_RUNTIME_JSON=%XR_RUNTIME_JSON%
)

start "" "%DUSK_EXE%" --backend vulkan --cvar backend.xrMode=2 %* "%DISC%"
