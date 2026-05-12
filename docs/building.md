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

On Windows, a local Khronos OpenXR SDK build can be installed under `build/` and passed to Dusk's configure step:

```bat
git clone --depth 1 --branch release-1.1.59 https://github.com/KhronosGroup/OpenXR-SDK.git build\openxr-sdk-src

cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -G Ninja -S build\openxr-sdk-src -B build\openxr-sdk-build-dll -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=build\openxr-sdk-install-dll -DBUILD_TESTS=OFF -DBUILD_API_LAYERS=OFF -DBUILD_CONFORMANCE_TESTS=OFF -DBUILD_CONFORMANCE_CLI=OFF -DDYNAMIC_LOADER=ON && cmake --build build\openxr-sdk-build-dll --target install"

cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --preset windows-msvc-relwithdebinfo -DAURORA_ENABLE_OPENXR=ON -DOpenXR_DIR=build/openxr-sdk-install-dll/cmake"
```

Use `DYNAMIC_LOADER=ON` for this local SDK build. The SDK's static-loader target uses the dynamic MSVC runtime on Windows, while this Dusk preset currently links the static runtime; mixing them fails with MSVC runtime-library link errors. The dynamic loader build installs `openxr_loader.dll`, and Dusk's existing runtime-DLL copy step places it next to `dusk.exe`.

Configure with the normal preset plus:

```sh
cmake --preset <preset> -DAURORA_ENABLE_OPENXR=ON
```

If an OpenXR package is not installed, Aurora can fetch the Khronos SDK during configure:

```sh
cmake --preset <preset> -DAURORA_ENABLE_OPENXR=ON -DAURORA_FETCH_OPENXR_SDK=ON
```

Aurora currently builds against OpenXR SDK `release-1.1.59` but requests OpenXR API `1.0.0` when creating the runtime instance. This keeps newer headers available at build time while remaining compatible with runtimes that reject a 1.1 application request.

The Dawn-first OpenXR proof needs a source-built Dawn because the current prebuilt Dawn package does not export the Vulkan device, physical device, or queue handles. For that experiment, configure with the vendored static Dawn provider and apply Dusk's Dawn handle patch:

```sh
cmake --preset <preset> -DAURORA_ENABLE_OPENXR=ON -DAURORA_FETCH_OPENXR_SDK=ON -DAURORA_DAWN_PROVIDER=vendor -DAURORA_DAWN_LINKAGE=static -DAURORA_DAWN_APPLY_OPENXR_PATCH=ON
```

That configuration also builds `dusk_openxr_probe`, a small standalone OpenXR/Dawn harness. It initializes Aurora with the Vulkan backend, runs the OpenXR runtime and patched-Dawn swapchain clear proof, then initializes the persistent OpenXR session/swapchain path used by `aurora_xr_begin_eye()`. It prints the XR status/views and exits without loading a game disc. A healthy patched-Dawn proof exits `0`; unavailable or blocked XR exits `2`. For build/test smoke on machines without a headset, run `cmd.exe /c build\windows-msvc-relwithdebinfo\dusk_openxr_probe.exe --allow-unavailable`; that mode still prints the XR gate as `xr_proof_gate=...` but exits `0` for unavailable/blocked runtime states.

The probe status message includes a `Dawn interop:` diagnostic and a structured `xr_dawn_interop=` line. Patched vendor builds should report `xr_dawn_interop=ready`, meaning Dawn exposes the Vulkan instance, physical device, device, queue family, queue, and runtime-owned `VkImage` wrapping. Prebuilt Dawn package builds should instead report `xr_dawn_interop=missing`. The patched vendor build registers `dusk_openxr_probe_dawn_interop`, an `openxr`-labeled CTest that runs the probe with `--require-dawn-interop`; unpatched builds register `dusk_openxr_probe_dawn_interop_missing` to assert the expected gate. All OpenXR probe builds also register `dusk_openxr_probe_invalid_runtime`, which sets `XR_RUNTIME_JSON` to a missing file and verifies the unavailable-runtime diagnostic path.

When a headset/runtime is available, configure the patched vendor build with `-DDUSK_OPENXR_ENABLE_LIVE_TESTS=ON` to register `dusk_openxr_probe_live_clear`. That live test runs without `--allow-unavailable`, requires `xr_dawn_interop=ready`, and passes only when the Dawn/OpenXR proof reaches `xr_proof_gate=cleared` and the harness drives `aurora_xr_begin_eye()` far enough to report `xr_eye_target_gate=submitted`.

For no-headset development, the preferred mock runtime is Monado's simulated HMD on Windows. Build an in-process Monado runtime with the simulated driver and use the null compositor for headless proof runs. The current tested Monado snapshot also needs `docs/patches/monado-openxr-enable2-app-owned-vkinstance.patch` when using Dawn's app-owned `VkInstance` with `XR_KHR_vulkan_enable2`. Apply `docs/patches/monado-null-compositor-90hz.patch` as well so the null compositor advertises a 90 Hz frame interval instead of Monado's default 20 Hz CI pacing:

```powershell
git clone --depth 1 https://gitlab.freedesktop.org/monado/monado.git build\monado-src
git -C build\monado-src apply ..\..\docs\patches\monado-openxr-enable2-app-owned-vkinstance.patch
git -C build\monado-src apply ..\..\docs\patches\monado-null-compositor-90hz.patch

cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S build\monado-src -B build\monado-inprocess -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"" -DXRT_FEATURE_SERVICE=OFF -DXRT_BUILD_DRIVER_SIMULATED=ON"
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build build\monado-inprocess --target openxr_monado --config RelWithDebInfo"

$env:XR_RUNTIME_JSON = "$PWD\build\monado-inprocess\openxr_monado-dev.json"
$env:SIMULATED_ENABLE = "TRUE"
$env:XRT_COMPOSITOR_NULL = "TRUE"
$env:XRT_COMPOSITOR_NULL_REFRESH_RATE_HZ = "90"
$env:XRT_DEBUG_GUI = "FALSE"
$env:OXR_DEBUG_GUI = "FALSE"
$env:PATH = "$PWD\build\monado-inprocess\src\xrt\targets\openxr;$PWD\build\monado-inprocess\vcpkg_installed\x64-windows\bin;$env:PATH"
.\build\windows-msvc-dawn-vendor-openxr\dusk_openxr_probe.exe --require-dawn-interop
```

The same runtime manifest can be wired into CTest by configuring the patched Dawn build with `-DDUSK_OPENXR_ENABLE_LIVE_TESTS=ON`, `-DDUSK_OPENXR_TEST_RUNTIME_JSON=C:/path/to/monado-inprocess/openxr_monado-dev.json`, `-DDUSK_OPENXR_TEST_ENVIRONMENT=SIMULATED_ENABLE=TRUE;XRT_COMPOSITOR_NULL=TRUE;XRT_COMPOSITOR_NULL_REFRESH_RATE_HZ=90;XRT_DEBUG_GUI=FALSE;OXR_DEBUG_GUI=FALSE`, and `-DDUSK_OPENXR_TEST_PATH_DIRS=C:/path/to/monado-inprocess/src/xrt/targets/openxr;C:/path/to/monado-inprocess/vcpkg_installed/x64-windows/bin`. Then run `ctest -R dusk_openxr_probe_live_clear --output-on-failure`; a passing test prints `xr_live_gate=cleared_submitted` after creating an OpenXR session with Dawn's Vulkan device, clearing both mock eye swapchain images, and driving a frame through `aurora_xr_begin_eye()`/`aurora_xr_end_eye()`. `ctest -R dusk_openxr_probe_mock_hmd_motion --output-on-failure` validates simulated-HMD 6DoF motion and writes first/last/delta SBS images under the build tree. With Monado's null compositor enabled, Aurora suppresses projection-layer submission after releasing the acquired images because that mock compositor is used for CI-style target lifetime testing rather than display. SteamVR's null driver can also advertise a fake headset, but Monado is a better fit for this gate because it is a native OpenXR runtime with a simulated HMD driver and a generated `XR_RUNTIME_JSON` manifest.

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
* `Optional` / **Enabled**: Dusk requests XR, validates/forces Vulkan, and continues with normal flat startup if Vulkan, the OpenXR loader/runtime, or Aurora XR ready/active initialization is unavailable.
* `Required`: Dusk requests XR, validates/forces Vulkan, and fails startup if Vulkan is unavailable or OpenXR cannot initialize to a ready/active state.

The graphics backend setting remains `backend.graphicsBackend`; use `auto` or `vulkan` for XR testing. Do not set an `openxr` backend ID. Changing OpenXR mode or backend settings requires restart from the prelaunch UI.

Current implementation notes:

* Aurora exposes XR config/status/frame/eye APIs and can probe an OpenXR runtime when built with `AURORA_ENABLE_OPENXR=ON` and an OpenXR loader target.
* The normal prebuilt Dawn package is still blocked because it does not expose the Vulkan device/queue or Windows-compatible OpenXR swapchain image interop. The patched source-built Dawn experiment adds those hooks and runs a small session/swapchain clear proof before the persistent session/EFB target path is enabled.
* If startup reports `No OpenXR head-mounted-display system available`, the loader/runtime was reachable but `xrGetSystem()` did not expose an HMD. Confirm the active runtime is the intended one and that the headset or streamer app is connected before treating the Dawn proof as failed.
* After the patched-Dawn proof succeeds, Aurora creates persistent OpenXR eye swapchains, wraps the acquired Vulkan images as Dawn textures, and `aurora_xr_begin_eye()` installs those per-eye render targets into the existing GX/EFB render-pass path.
* Aurora does not yet provide a separate OpenXR quad-layer/flat UI render target. Ordinary HUD/menu/RmlUi/ImGui UI stays in the flat path for launch; world-projected targeting packets can now be drawn through the per-eye world pass.
* `F8` recenters the Dusk XR app-space origin from the latest valid HMD pose when XR tracking is active. It logs whether XR is inactive or tracking is unavailable. State Share uses `Shift+F8`.

Suggested validation after building:

1. With `backend.xrMode=Disabled`, launch normally and verify flat rendering/input are unchanged.
2. With `backend.xrMode=Optional` and no usable OpenXR runtime, launch and verify Dusk falls back to flat startup with an XR unavailable/blocked log message.
3. With `backend.xrMode=Required` and no usable OpenXR runtime, verify startup fails with the required-XR error instead of silently falling back.
4. On a future build with active Vulkan/OpenXR swapchains, verify both eyes render, world-targeted reticles such as player sight and boomerang lock cursors remain attached in stereo, and `F8` recenters physical drift without changing game camera/orbit/free-camera state.
