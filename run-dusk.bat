@echo off
setlocal

set "ROOT=%~dp0"
set "DUSK_EXE=%ROOT%build\windows-msvc-relwithdebinfo\dusk.exe"
set "DISC=%ROOT%game.ciso"

if not exist "%DUSK_EXE%" (
    echo Dusk executable not found: "%DUSK_EXE%"
    echo Build it with:
    echo   cmake --build --preset windows-msvc-relwithdebinfo
    exit /b 1
)

if not exist "%DISC%" (
    echo Game disc image not found: "%DISC%"
    exit /b 1
)

start "" "%DUSK_EXE%" "%DISC%"
