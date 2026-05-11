# Agent Notes

This repo is Dusk, a reverse-engineered reimplementation of Twilight Princess.
Do not add or commit copyrighted game dumps or extracted proprietary assets.

## Local Setup

This workspace is used on both Windows and macOS. Keep platform-specific local
launchers and disc images untracked.

## Local Windows Setup

This workspace has been set up and built on Windows using Visual Studio 2026
Build Tools.

Important local paths:

- Repo: `C:\Users\eprov\OneDrive\Documentos\git\dusk`
- VS dev shell: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat`
- 7-Zip: `C:\Program Files\7-Zip\7z.exe`
- Local disc image: `C:\Users\eprov\OneDrive\Documentos\git\dusk\game.ciso`
- Built executable: `C:\Users\eprov\OneDrive\Documentos\git\dusk\build\windows-msvc-relwithdebinfo\dusk.exe`
- Local launcher: `C:\Users\eprov\OneDrive\Documentos\git\dusk\run-dusk.bat`

`game.ciso` came from the user's legal dump archive:

```powershell
& "C:\Program Files\7-Zip\7z.exe" e "E:\Games\TwilightPrincess\Rom\Legend of Zelda, The - Twilight Princess (USA).7z" "Legend of Zelda, The - Twilight Princess (USA).ciso" -o"C:\Users\eprov\OneDrive\Documentos\git\dusk" -y
Move-Item -LiteralPath "C:\Users\eprov\OneDrive\Documentos\git\dusk\Legend of Zelda, The - Twilight Princess (USA).ciso" -Destination "C:\Users\eprov\OneDrive\Documentos\git\dusk\game.ciso"
```

Dusk supports ISO/GCM/RVZ/WIA/WBFS/CISO/GCZ as runtime disc-image formats.
The `.7z` archive itself is not passed to Dusk.

## Git And Submodules

Plain PowerShell may not have Git's Unix helper tools on PATH. Use Git Bash for
submodule operations:

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc "cd /c/Users/eprov/OneDrive/Documentos/git/dusk && git submodule update --init --recursive"
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

Important local paths:

- Repo: `/Users/pvncher/Documents/Git/dusk`
- 7-Zip archive with the user's legal dump: `/Users/pvncher/Library/CloudStorage/GoogleDrive-eprovencher92@gmail.com/My Drive/Games/Roms/GC/Legend of Zelda, The - Twilight Princess (USA) NGC.7z`
- Local disc image after extraction: `/Users/pvncher/Documents/Git/dusk/game.ciso`
- Built app bundle: `/Users/pvncher/Documents/Git/dusk/build/macos-default-relwithdebinfo/Dusk.app`
- App binary: `/Users/pvncher/Documents/Git/dusk/build/macos-default-relwithdebinfo/Dusk.app/Contents/MacOS/Dusk`
- Local launcher: `/Users/pvncher/Documents/Git/dusk/run-dusk.sh`

Install local build prerequisites with Homebrew if needed:

```sh
brew install cmake ninja p7zip
```

Extract the local disc image from the user's legal archive with:

```sh
7z e "/Users/pvncher/Library/CloudStorage/GoogleDrive-eprovencher92@gmail.com/My Drive/Games/Roms/GC/Legend of Zelda, The - Twilight Princess (USA) NGC.7z" \
  "Legend of Zelda, The - Twilight Princess (USA).ciso" -o"/Users/pvncher/Documents/Git/dusk" -y
mv "/Users/pvncher/Documents/Git/dusk/Legend of Zelda, The - Twilight Princess (USA).ciso" \
  "/Users/pvncher/Documents/Git/dusk/game.ciso"
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
