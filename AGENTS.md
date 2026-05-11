# Agent Notes

This repo is Dusk, a reverse-engineered reimplementation of Twilight Princess.
Do not add or commit copyrighted game dumps or extracted proprietary assets.

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

## Current Local State

Expected untracked local setup files:

- `game.ciso`
- `run-dusk.bat`

Do not remove these unless the user asks. Do not commit `game.ciso`.
