# Agent Notes

This repo is Dusk, a reverse-engineered reimplementation of Twilight Princess.
Do not add or commit copyrighted game dumps or extracted proprietary assets.

## Local Setup

This workspace is used on both Windows and macOS. Keep platform-specific local
launchers and disc images untracked.

## Local Windows Setup

This workspace has been set up and built on Windows using Visual Studio 2026
Build Tools.

Useful local paths:

- VS dev shell: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat`
- Local disc image: `.\game.ciso` (untracked)
- Built executable: `.\build\windows-msvc-relwithdebinfo\dusk.exe`
- Local launcher: `.\run-dusk.bat`

Dusk supports ISO/GCM/RVZ/WIA/WBFS/CISO/GCZ as runtime disc-image formats.

## Git And Submodules

Plain PowerShell may not have Git's Unix helper tools on PATH. Use Git Bash for
submodule operations:

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc "git submodule update --init --recursive"
```

The Aurora submodule should be checked out at `extern/aurora`.

## Configure

Use the Visual Studio developer command environment before invoking CMake:

```powershell
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --preset windows-msvc-relwithdebinfo"
```

The first configure may download third-party dependencies through CMake
FetchContent, including Dawn, SDL3, Freetype, RmlUi, nod, libjpeg-turbo, and
others.

## Build

```powershell
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build --preset windows-msvc-relwithdebinfo"
```

A successful build produces:

```text
build\windows-msvc-relwithdebinfo\dusk.exe
```

Required runtime DLLs are copied next to the executable by the build, including
`SDL3.dll`, `nod.dll`, `webgpu_dawn.dll`, `dxcompiler.dll`, and `dxil.dll`.

## Run

Preferred local run command:

```powershell
.\run-dusk.bat
```

Equivalent direct command:

```powershell
.\build\windows-msvc-relwithdebinfo\dusk.exe .\game.ciso
```

If no disc argument is supplied, Dusk defaults to `game.iso` in the current
working directory, so pass `game.ciso` explicitly or use `run-dusk.bat`.

## Local macOS Setup

Useful local paths:

- Local disc image: `./game.ciso` (untracked)
- Built app bundle: `./build/macos-default-relwithdebinfo/Dusk.app`
- App binary: `./build/macos-default-relwithdebinfo/Dusk.app/Contents/MacOS/Dusk`
- Local launcher: `./run-dusk.sh`

Install local build prerequisites with Homebrew if needed:

```sh
brew install cmake ninja p7zip
```

Configure and build on macOS with:

```sh
cmake --preset macos-default-relwithdebinfo
cmake --build --preset macos-default-relwithdebinfo
```

A successful macOS build produces:

```text
build/macos-default-relwithdebinfo/Dusk.app
```

Run on macOS with:

```sh
./run-dusk.sh
```

Equivalent direct command:

```sh
./build/macos-default-relwithdebinfo/Dusk.app/Contents/MacOS/Dusk ./game.ciso
```

## Current Local State

Expected local setup files:

- `game.ciso` (untracked; ignored by `*.ciso`)
- `run-dusk.bat`
- `run-dusk.sh`

Do not remove these unless the user asks. Do not commit copyrighted disc images
or extracted proprietary assets.
