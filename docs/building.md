### Building
#### Prerequisites
* [CMake 3.25+](https://cmake.org)
    * Windows: Install `CMake Tools` in Visual Studio
    * macOS: `brew install cmake`
* [Python 3+](https://python.org)
    * Windows: [Microsoft Store](https://go.microsoft.com/fwlink?linkID=2082640)
        * Verify it's added to `%PATH%` by typing `python` in `cmd`.
    * macOS: `brew install python@3`
* **[Windows]** [Visual Studio 2026 Community](https://www.visualstudio.com/en-us/products/visual-studio-community-vs.aspx)
    * Select `C++ Development` and verify the following packages are included:
        * `Windows 11 SDK`
        * `CMake Tools`
        * `C++ Clang Compiler`
        * `C++ Clang-cl`
* **[macOS]** [Xcode 16.4+](https://developer.apple.com/xcode/download/)
* **[Linux]** Actively tested on Ubuntu 24.04, Arch Linux & derivatives.
    * Ubuntu 24.04+ packages
      ```
      build-essential curl git ninja-build clang lld zlib1g-dev libcurl4-openssl-dev \
      libglu1-mesa-dev libdbus-1-dev libvulkan-dev libxi-dev libxrandr-dev libasound2-dev libpulse-dev \
      libudev-dev libpng-dev libncurses5-dev cmake libx11-xcb-dev python3 python-is-python3 \
      libclang-dev libfreetype-dev libxinerama-dev libxcursor-dev python3-markupsafe libgtk-3-dev \
      libxss-dev libxtst-dev
      ```
     * Arch Linux packages
       ```
       base-devel cmake ninja llvm vulkan-headers python python-markupsafe clang lld alsa-lib libpulse libxrandr freetype2
       ```
     * Fedora packages
       ```
       cmake vulkan-headers ninja-build clang-devel llvm-devel libpng-devel
       ```
         * It's also important that you install the developer tools and libraries
           ```
           sudo dnf groupinstall "Development Tools" "Development Libraries"
           ```
#### Optional OpenXR prerequisites
OpenXR support is experimental and currently targets desktop Vulkan only. It is a presentation/session mode layered on top of the Vulkan backend; there is no separate `openxr` graphics backend.

To build Aurora's OpenXR runtime probe, install an OpenXR loader/development package before configuring and enable the optional CMake hook:

* **Windows**: install an OpenXR runtime for your headset (for example SteamVR, Oculus/Meta, or Windows Mixed Reality where applicable) and an OpenXR SDK/loader package discoverable by CMake.
* **Linux**: install a Vulkan-capable OpenXR runtime such as Monado or SteamVR, plus loader/development files. Distribution package names vary; look for OpenXR loader/headers packages in addition to the Vulkan packages above.
* **macOS**: Vulkan/OpenXR launch is not currently an expected runtime target. Builds may still compile the XR stubs, but active desktop XR rendering is blocked.

Configure with the normal preset plus:

```sh
cmake --preset <preset> -DAURORA_ENABLE_OPENXR=ON
```

If no OpenXR loader target is found, Aurora still builds the XR API stubs and Dusk can run normally, but XR requests will report unavailable.

#### Setup
Clone and initialize the Dusk repository
```sh
git clone --recursive https://github.com/TwilitRealm/dusk.git
cd dusk
git pull
git submodule update --init --recursive
```

#### Building

**CLion (Windows / macOS / Linux)**

Open the project directory in CLion. Enable the appropriate presets for your platform:

![CLion](../assets/clion.png)

**Visual Studio (Windows)**

Open the project directory in Visual Studio. The CMake configuration will be loaded automatically.

**ninja (macOS)**

```sh
cmake --preset macos-default-relwithdebinfo
cmake --build --preset macos-default-relwithdebinfo
```

Alternate presets available:
- `macos-default-debug`: Clang, Debug

**ninja (Linux)**

```sh
cmake --preset linux-default-relwithdebinfo
cmake --build --preset linux-default-relwithdebinfo
```

Alternate presets available:
- `linux-default-debug`: GCC, Debug
- `linux-clang-relwithdebinfo`: Clang, RelWithDebInfo
- `linux-clang-debug`: Clang, Debug

**ninja (Windows)**

```sh
cmake --preset windows-msvc-relwithdebinfo
cmake --build --preset windows-msvc-relwithdebinfo
```

Alternate presets available:
- `windows-msvc-debug`: MSVC, Debug
- `windows-clang-relwithdebinfo`: Clang-cl, RelWithDebInfo
- `windows-clang-debug`: Clang-cl, Debug

#### Running
Pass the disc image as a positional argument. Supported formats: ISO (GCM), RVZ, WIA, WBFS, CISO, GCZ
```sh
build/{preset}/dusk /path/to/game.rvz
```
If no path is specified, Dusk defaults to `game.iso` in the current working directory.

#### OpenXR configuration and validation
OpenXR is controlled by the persistent `backend.xrMode` setting and by the prelaunch **OpenXR Mode** control:

* `Disabled` / **Off**: normal flat startup. This is the default.
* `Optional` / **Enabled**: Dusk requests XR, validates/forces Vulkan, and continues with normal flat startup if Vulkan, the OpenXR loader/runtime, or Aurora XR activation is unavailable.
* `Required`: Dusk requests XR, validates/forces Vulkan, and fails startup if Vulkan or OpenXR activation is unavailable.

The graphics backend setting remains `backend.graphicsBackend`; use `auto` or `vulkan` for XR testing. Do not set an `openxr` backend ID. Changing OpenXR mode or backend settings requires restart from the prelaunch UI.

Current implementation notes:

* Aurora exposes XR config/status/frame/eye APIs and can probe an OpenXR runtime when built with `AURORA_ENABLE_OPENXR=ON` and an OpenXR loader target.
* Active eye rendering is still blocked until Aurora's Dawn/WebGPU path exposes native Vulkan image handles and OpenXR swapchain interop. Until that is implemented, XR requests should gracefully remain flat in Optional mode.
* Aurora does not yet provide a separate OpenXR quad-layer/flat UI render target. Ordinary HUD/menu/RmlUi/ImGui UI stays in the flat path for launch; world-projected targeting packets are queued for per-eye drawing when active eye rendering becomes available.
* `F8` recenters the Dusk XR app-space origin from the latest valid HMD pose when XR tracking is active. It logs whether XR is inactive or tracking is unavailable. State Share uses `Shift+F8`.

Suggested validation after building:

1. With `backend.xrMode=Disabled`, launch normally and verify flat rendering/input are unchanged.
2. With `backend.xrMode=Optional` and no usable OpenXR runtime, launch and verify Dusk falls back to flat startup with an XR unavailable/blocked log message.
3. With `backend.xrMode=Required` and no usable OpenXR runtime, verify startup fails with the required-XR error instead of silently falling back.
4. On a future build with active Vulkan/OpenXR swapchains, verify both eyes render, world-targeted reticles such as player sight and boomerang lock cursors remain attached in stereo, and `F8` recenters physical drift without changing game camera/orbit/free-camera state.
