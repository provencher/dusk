# OpenXR Dawn Interop Decision - 2026-05-11

## Decision

Do not proceed with active Windows OpenXR eye rendering through the current prebuilt Dawn/WebGPU Vulkan path.

Aurora can continue to probe OpenXR and report a clear `AURORA_XR_BLOCKED` status, but it should not create a partial OpenXR session or attempt to render to HMD swapchains until the renderer has an approved Vulkan interop path.

Recommended next path: patch or fork the Dawn integration first, with a hard timebox.

This keeps Dusk on Aurora's existing WebGPU/GX renderer if the patch succeeds. The proof target is deliberately small: expose/verify `VkInstance`, `VkPhysicalDevice`, `VkDevice`, graphics queue family/index, `VkQueue`, and Windows-compatible handling for OpenXR runtime-owned `VkImage` swapchains, then clear both eye swapchain images through Dawn's Vulkan device before touching full Dusk rendering.

Only after that proof succeeds should Aurora's EFB target seam be parameterized so `aurora_xr_begin_eye()` installs per-eye render targets for the existing Dusk stereo render loop. If Windows Vulkan image interop fails, stop at this gate and choose between a deeper Dawn fork and a native Vulkan XR renderer path.

## Evidence

Local Dawn package inspected:

- `build/windows-msvc-relwithdebinfo/_deps/dawn_prebuilt-src/include/dawn/native/VulkanBackend.h`
- `build/windows-msvc-relwithdebinfo/_deps/dawn_prebuilt-src/include/dawn/native/DawnNative.h`
- `build/windows-msvc-relwithdebinfo/_deps/dawn_prebuilt-src/include/dawn/webgpu.h`

Findings:

- `dawn::native::vulkan::GetInstance(WGPUDevice)` exposes the `VkInstance`.
- The inspected public native headers do not expose the Dawn-owned `VkPhysicalDevice`, `VkDevice`, graphics `VkQueue`, queue family index, or queue index.
- Dawn's legacy `WrapVulkanImage()` path imports external Vulkan memory. The concrete descriptor exposed for Vulkan image memory import is `ExternalImageDescriptorOpaqueFD`, which is Linux/Fuchsia-only in the inspected header.
- Dawn's newer `SharedTextureMemoryOpaqueFDDescriptor` similarly imports Vulkan memory from an opaque file descriptor, not a runtime-owned `VkImage`.
- Windows shared texture descriptors in the inspected WebGPU header are DXGI/D3D descriptors, not Vulkan `VkImage` descriptors.
- OpenXR Vulkan swapchains expose runtime-owned `XrSwapchainImageVulkanKHR::image` handles. They do not provide the opaque FD memory import path required by this Dawn package on Windows.

## Consequence

Phase 3 from `docs/plans/openxr-windows-carry-home-2026-05-11.md` is blocked for the current Dawn package on Windows.

The viable next implementation choices are:

- Patch/fork Dawn or Aurora's Dawn integration so it can create/import the exact Vulkan device/images required by OpenXR on Windows.
- Build a larger native Vulkan XR renderer path for eye targets.
- Keep active OpenXR rendering blocked while preserving Optional flat fallback and clear Required-mode failure.

## Dawn Handle Patch

The matching Dawn source tag contains the internal accessors needed for the first half of the Dawn-first proof:

- `dawn::native::vulkan::Device::GetVkInstance()`
- `dawn::native::vulkan::Device::GetVkDevice()`
- `dawn::native::vulkan::Device::GetGraphicsQueueFamily()`
- `dawn::native::vulkan::PhysicalDevice::GetVkPhysicalDevice()`
- `dawn::native::vulkan::Queue::GetVkQueue()`

Dusk now carries `extern/aurora/patches/dawn-openxr-vulkan-handles.patch`, which adds two exported helpers to `dawn/native/VulkanBackend.h` and `src/dawn/native/vulkan/VulkanBackend.cpp`:

- `dawn::native::vulkan::GetDeviceHandles()`, exposing Dawn's borrowed Vulkan instance, physical device, device, queue family/index, and queue.
- `dawn::native::vulkan::WrapVulkanSwapchainImage()`, wrapping a runtime-owned `VkImage` as a `WGPUTexture` without taking ownership.

Aurora's Dawn provider can apply it only for source-built Dawn:

```sh
cmake --preset windows-msvc-relwithdebinfo -DAURORA_ENABLE_OPENXR=ON -DAURORA_FETCH_OPENXR_SDK=ON -DAURORA_DAWN_PROVIDER=vendor -DAURORA_DAWN_LINKAGE=static -DAURORA_DAWN_APPLY_OPENXR_PATCH=ON
```

Patch application runs through `extern/aurora/cmake/ApplyGitPatch.cmake`, which applies the patch when clean, accepts already-patched Dawn source trees, and fails only when the patch neither applies nor reverse-checks.

Aurora now has a patched-Dawn proof path in `extern/aurora/lib/xr/openxr_probe.cpp`:

- query Dawn's borrowed `VkInstance`, `VkPhysicalDevice`, `VkDevice`, queue family/index, and `VkQueue`;
- load Vulkan command entry points through Dawn's Vulkan instance/device;
- call `xrGetVulkanGraphicsRequirements2KHR()`;
- create an `XrSession` with `XrGraphicsBindingVulkanKHR`;
- create one color swapchain per stereo eye;
- acquire, wait, clear, release, and destroy one `XrSwapchainImageVulkanKHR::image` per eye with raw Vulkan commands submitted on Dawn's queue.
- wrap each acquired `XrSwapchainImageVulkanKHR::image` through Dawn's new runtime-owned `VkImage` wrapper before releasing it.

That proof can also be driven without the game loop through the `dusk_openxr_probe` executable built when `AURORA_ENABLE_OPENXR=ON`. It initializes Aurora with Vulkan/OpenXR, prints the XR status and view sizes, exits `0` when the proof reaches the "cleared both eye swapchain images successfully" gate, and exits `2` when XR remains unavailable or blocked. `dusk_openxr_probe --allow-unavailable` keeps the same output but exits `0` for unavailable/blocked runtime states, so CTest can smoke the startup path on machines without a connected headset.

The proof still does not parameterize Aurora's EFB targets or submit Dusk rendering to the wrapped images. If the proof works on a healthy runtime, the next step is Aurora-side EFB target injection rather than more Dawn image-import work.

## Local validation

- Built the Khronos OpenXR SDK locally with `DYNAMIC_LOADER=ON`; the static loader conflicts with Dusk's current `/MT` Windows preset because the SDK target uses the dynamic MSVC runtime.
- Configured and built Dusk with `AURORA_ENABLE_OPENXR=ON` and the local SDK package.
- Configured and built Dusk with `AURORA_ENABLE_OPENXR=ON -DAURORA_FETCH_OPENXR_SDK=ON` after clearing the local package cache entry; Aurora fetched and linked the SDK loader.
- Verified `openxr_loader.dll` is copied next to `dusk.exe`.
- Verified Vulkan flat startup with `backend.xrMode=Disabled`.
- Verified Optional OpenXR fallback with an invalid `XR_RUNTIME_JSON`; Dusk logs the OpenXR loader failure and enters normal flat startup.
- Verified Required OpenXR failure with an invalid `XR_RUNTIME_JSON`; startup fails instead of silently falling back.
- Verified the registered Windows runtime path is `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`; early runs failed during `xrCreateInstance` until Aurora's requested OpenXR API version was lowered to 1.0.0.
- Verified `extern/aurora/patches/dawn-openxr-vulkan-handles.patch` applies cleanly to Dawn `v20260423.175430`.
- Configured a source-built Dawn tree with `AURORA_DAWN_PROVIDER=vendor`, `AURORA_DAWN_LINKAGE=static`, `AURORA_DAWN_APPLY_OPENXR_PATCH=ON`, `AURORA_ENABLE_OPENXR=ON`, and `AURORA_FETCH_OPENXR_SDK=ON`.
- Built `aurora_core` and `dusk` from that patched vendored Dawn tree after wiring Dawn's vendored `Vulkan::Headers` into `aurora_core` for the patched public Vulkan header.
- Verified the normal prebuilt Dawn package build still compiles and links with the proof path disabled.
- Verified Required OpenXR failure with an invalid `XR_RUNTIME_JSON` in the patched vendored Dawn executable.
- Re-tested the registered runtime in the patched vendored Dawn executable. Initial runs failed at `xrCreateInstance` with `XrResult(-4)`. Virtual Desktop's `C:\ProgramData\Virtual Desktop\OpenXR.log` showed `XR_ERROR_API_VERSION_UNSUPPORTED` because Aurora requested OpenXR API `1.1.59`.
- Changed Aurora's OpenXR instance creation to request API `1.0.0` while still building against SDK `release-1.1.59`. This gets past `xrCreateInstance` on the registered Virtual Desktop runtime.
- With Virtual Desktop Streamer running, required OpenXR now fails at `xrGetSystem` with `XrResult(-35)` because no head-mounted-display system is available. The Dawn/OpenXR session, swapchain clear, and runtime-owned `VkImage` wrapper proof still cannot execute locally until the headset/runtime exposes an HMD system.
- Inspected local runtime registration: only `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json` is registered under `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`; no SteamVR, Oculus/Meta, Monado, or WMR runtime install was found in `C:\Program Files`.
- Improved OpenXR unavailable diagnostics so loader/instance failures include an active-runtime/headset-streamer hint and the `XR_RUNTIME_JSON` override path when present.
- Built and ran patched-tree Aurora tests that are currently linkable: `gx_fifo_tests.exe` passed 163 tests and `os_alloc_tests.exe` passed 5 tests.
- `dvd_tests` is not currently usable as validation in this configuration because it fails to link with unresolved `OSPanic` and `aurora::g_gameName`, unrelated to OpenXR.
- Extended the Dawn patch with `WrapVulkanSwapchainImage()` around Dawn's internal Vulkan `SwapChainTexture::Create()` path and verified the patched vendored Dawn/Aurora/Dusk build still links.
- Verified `extern/aurora/patches/dawn-openxr-vulkan-handles.patch` reverse-applies cleanly to the locally patched Dawn source tree.
- Verified Optional OpenXR fallback still reaches flat startup when the runtime is present but no HMD system is available.
- Tightened the proof cleanup path so an acquired OpenXR swapchain image is released if `xrWaitSwapchainImage()` fails before the clear/wrap step.
- Expanded OpenXR diagnostics to print well-known `XrResult` names alongside numeric values, so gates like `XR_ERROR_API_VERSION_UNSUPPORTED`, `XR_ERROR_RUNTIME_UNAVAILABLE`, and `XR_ERROR_FORM_FACTOR_UNAVAILABLE` are readable in Dusk logs.
- Added `dusk_openxr_probe` as a standalone proof harness so the Dawn/OpenXR session and swapchain clear gate can be tested without loading Dusk's game disc or entering the game loop.
- Built and ran `build\windows-msvc-dawn-vendor-openxr\dusk_openxr_probe.exe`; on the current machine it reports `xr_status=unavailable`, `xr_message=No OpenXR head-mounted-display system available: XrResult(-35 XR_ERROR_FORM_FACTOR_UNAVAILABLE); check the active OpenXR runtime registration and confirm the runtime app/headset is running`, `xr_view_count=0`, and exits `2`.
- Added `dusk_openxr_probe --allow-unavailable` and a `dusk_openxr_probe_smoke` CTest so the no-disc OpenXR startup path can be exercised in builds without requiring an HMD. The CTest smoke also requires the probe output to include `backend=4` and fails if XR remains disabled.
- Extended the probe status message with a `Dawn interop:` diagnostic and a structured `xr_dawn_interop=` line. In the patched vendor build this now reports `xr_dawn_interop=ready`, confirming that Dawn exposes the Vulkan instance, physical device, device, queue family, queue, and runtime-owned `VkImage` wrapping even when the current OpenXR runtime stops at `XR_ERROR_FORM_FACTOR_UNAVAILABLE`.
- Added a structured `xr_proof_gate=` line to the probe. Current no-HMD local runs report `xr_proof_gate=no_hmd`; a successful Dawn/OpenXR eye-swapchain clear proof will report `xr_proof_gate=cleared`.
- The probe now strips probe-only flags such as `--allow-unavailable` and `--require-dawn-interop` before calling `aurora_initialize()`, keeping Aurora startup independent of harness flags.
- Tightened the Dawn handle readiness check so the patched path treats missing `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, or an invalid graphics queue family as incomplete interop before attempting `xrCreateSession`.
- Updated the proof swapchain creation to reject zero dimensions and require support for a single-sample proof swapchain. Dawn's current Vulkan image-wrapper validation only accepts `sampleCount == 1`, so the proof deliberately creates single-sample OpenXR images even when the runtime reports a higher recommended sample count.
- Added `dusk_openxr_probe_dawn_interop`, an `openxr`-labeled CTest registered only for `AURORA_DAWN_APPLY_OPENXR_PATCH=ON` builds. It runs `dusk_openxr_probe --allow-unavailable --require-dawn-interop`, so no-HMD smoke testing now verifies the Dawn-first build.
- Added opt-in live HMD testing through `DUSK_OPENXR_ENABLE_LIVE_TESTS=ON`. In patched Dawn builds this registers `dusk_openxr_probe_live_clear`, labeled `openxr-live`, which runs without `--allow-unavailable` and passes only when the proof reports `xr_proof_gate=cleared`.
- Added the symmetric unpatched-build test `dusk_openxr_probe_dawn_interop_missing`, which requires `xr_dawn_interop=missing` so prebuilt Dawn package builds are also checked at the Dawn interop gate.
- Added `dusk_openxr_probe_invalid_runtime`, which sets `XR_RUNTIME_JSON` to a missing file and requires `xr_status=unavailable`. This gives the OpenXR probe suite one deterministic loader-failure check that does not depend on the host's registered runtime or HMD state.
- Replaced the raw Dawn `git apply` FetchContent patch command with the idempotent CMake patch helper and verified it reports the current local Dawn source as already patched.
- Added the normal Windows icon, manifest, version resource, and explicit Windows subsystem/main entry-point link options to `dusk_openxr_probe`. Without that Windows executable metadata, the prebuilt-Dawn probe binary was blocked by local Device Guard policy after relinking even though `dusk.exe` in the same build could launch. On Windows, run the probe through `cmd.exe /c ...` or CTest so stdout/stderr are captured predictably.
- Re-ran `build\windows-msvc-dawn-vendor-openxr\dusk_openxr_probe.exe --allow-unavailable --require-dawn-interop` after the audit. It reports `backend=4`, `xr_status=unavailable`, `xr_dawn_interop=ready`, `xr_proof_gate=no_hmd`, and `xr_view_count=0`, confirming the current stop is the missing HMD system rather than Dawn handle/wrapper availability.
- Tightened the Dawn proof wrapper descriptor to match the OpenXR swapchain usage actually requested by the proof: `RenderAttachment | CopyDst` for an `XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT` swapchain. The proof failure messages now include the selected Vulkan format, per-eye dimensions, proof sample count, runtime recommended/max sample counts, and image count where that information is available.
- Rebuilt `dusk_openxr_probe` and `dusk` in both the patched vendored Dawn build and the normal prebuilt Dawn build after the proof diagnostic/usage changes. `ctest -L openxr` passes 3/3 tests in both build trees.
- Inspected the vendored Dawn source and confirmed `ValidateVulkanImageCanBeWrapped()` rejects wrapped Vulkan images with `sampleCount != 1`. The proof now keeps runtime-reported recommended sample counts as diagnostics only, creates single-sample OpenXR swapchains for the image-wrapper gate, and still prints each view's recommended sample count through `dusk_openxr_probe`.
- Rebuilt `dusk_openxr_probe` and `dusk` again in both build trees after the sample-count correction. `ctest -L openxr` still passes 3/3 tests in both build trees, and the direct patched probe still reports `xr_dawn_interop=ready` with `xr_proof_gate=no_hmd`.
- Added an `xrGetVulkanGraphicsDevice2KHR()` check to the live proof before `xrCreateSession`. The proof now verifies that OpenXR selects the same `VkPhysicalDevice` for Dawn's `VkInstance` that Dawn is already using, and reports both device names if the runtime requires a different GPU.
- Rebuilt `dusk_openxr_probe` and `dusk` in both build trees after the physical-device check. `ctest -L openxr` still passes 3/3 tests in both build trees, and the direct patched probe still reports `xr_dawn_interop=ready` with `xr_proof_gate=no_hmd`.
- Researched mock-HMD options. Monado is the preferred path for this proof because its Windows port is explicitly useful for simulated HMD/controller devices, it can run as an out-of-process OpenXR runtime via `monado-service.exe`, and clients can select it with `XR_RUNTIME_JSON` pointing at `openxr_monado-dev.json`. SteamVR's null driver can fake an HMD, but it is less direct for this Dawn/OpenXR gate because it routes through SteamVR rather than a purpose-built simulated OpenXR runtime.
- Added `DUSK_OPENXR_TEST_RUNTIME_JSON` so the opt-in `dusk_openxr_probe_live_clear` CTest can run against a mock runtime manifest such as Monado's generated `openxr_monado-dev.json` while leaving the host's registered runtime untouched.
- Built Monado locally as an in-process Windows runtime with `XRT_FEATURE_SERVICE=OFF` and `DRIVER_SIMULATED=ON`. The out-of-process service path reached startup and then exited early locally; the in-process runtime plus `SIMULATED_ENABLE=TRUE` and `XRT_COMPOSITOR_NULL=TRUE` is the working no-headset test path.
- Added `DUSK_OPENXR_TEST_ENVIRONMENT` and `DUSK_OPENXR_TEST_PATH_DIRS` so `dusk_openxr_probe_live_clear` can run under CTest against Monado's generated manifest and dependency DLL directories without changing the machine-wide active runtime.
- Found the first real Dawn/OpenXR interop failure with the mock runtime: Monado could create a simulated HMD and stereo view configuration, but `xrGetVulkanGraphicsDevice2KHR()` crashed/failed until Dawn's `VkInstance` enabled OpenXR's required Vulkan instance extensions. Extended `extern/aurora/patches/dawn-openxr-vulkan-handles.patch` to request `VK_KHR_external_fence_capabilities`, `VK_KHR_external_memory_capabilities`, `VK_KHR_external_semaphore_capabilities`, and `VK_KHR_get_physical_device_properties2`.
- Current Monado source also assumes `xrCreateVulkanInstanceKHR()` populated an internal `vkGetInstanceProcAddr` pointer before `xrGetVulkanGraphicsDevice2KHR()`. Dusk uses Dawn's app-owned `VkInstance`, so the local mock runtime carries `docs/patches/monado-openxr-enable2-app-owned-vkinstance.patch` to fall back to the exported Vulkan loader entry point for that enable2 query.
- With patched Dawn and the patched Monado mock runtime, the live proof now clears both mock eye swapchain images: `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure` passes 4/4 tests, including `dusk_openxr_probe_live_clear`, and the direct probe reports `xr_dawn_interop=ready`, `xr_proof_gate=cleared`, and two 320x240 views.

## Objective audit

The active Dawn-first OpenXR objective breaks down into these concrete gates:

| Requirement | Evidence | Status |
| --- | --- | --- |
| Patch or fork the Dawn integration first, within the timeboxed Dawn-first path. | `extern/aurora/cmake/AuroraDawnProvider.cmake` wires `AURORA_DAWN_APPLY_OPENXR_PATCH`; `extern/aurora/cmake/ApplyGitPatch.cmake` applies or reverse-checks the patch; `extern/aurora/patches/dawn-openxr-vulkan-handles.patch` contains the Dawn native Vulkan additions. | Done for a source-built vendored Dawn path. |
| Expose `VkInstance`, `VkPhysicalDevice`, `VkDevice`, graphics queue family/index, and `VkQueue`. | The Dawn patch exports `dawn::native::vulkan::GetDeviceHandles()` and Aurora calls it through `extern/aurora/lib/webgpu/dawn_vulkan_interop.cpp`. `dusk_openxr_probe_dawn_interop` requires `xr_dawn_interop=ready`. | Done and covered by patched-build CTest. |
| Add Windows-compatible OpenXR `VkImage` swapchain handling. | The Dawn patch exports `WrapVulkanSwapchainImage()` for runtime-owned `VkImage`; Aurora wraps each acquired `XrSwapchainImageVulkanKHR::image` in `run_dawn_openxr_vulkan_clear_proof()`. Dawn's Vulkan instance now also enables the OpenXR-required Vulkan instance extensions. | Implemented and locally verified against Monado's simulated HMD. |
| Build a tiny proof that creates an OpenXR session using Dawn's Vulkan device. | `tools/openxr_probe_main.cpp` drives Aurora without a game disc; the patched proof creates `XrSession` with `XrGraphicsBindingVulkanKHR` from Dawn handles. | Implemented and locally verified with `dusk_openxr_probe_live_clear`. |
| Clear both eye swapchain images through the proof. | The proof creates one swapchain per stereo eye, acquires/waits/clears/releases one Vulkan image per eye, and reports `xr_proof_gate=cleared` only on success. `DUSK_OPENXR_TEST_RUNTIME_JSON`, `DUSK_OPENXR_TEST_ENVIRONMENT`, and `DUSK_OPENXR_TEST_PATH_DIRS` point the live CTest at the patched Monado simulated HMD runtime. | Done for the no-hardware Monado runtime; physical headset runtime coverage is still pending. |
| Only after proof success, parameterize Aurora EFB targets so `aurora_xr_begin_eye()` installs per-eye render targets. | The proof-success path deliberately returns blocked with `Aurora EFB per-eye render target wiring is not implemented yet`; no EFB target injection was added. | Correctly deferred. |
| If Dawn image interop fails, stop at the gate and choose deeper Dawn fork or native Vulkan XR renderer. | Probe/test output separates `xr_dawn_interop=missing`, `incomplete`, `ready`, and `xr_proof_gate=proof_blocked`; live clear is opt-in through `DUSK_OPENXR_ENABLE_LIVE_TESTS=ON`. | Gate is explicit; no fork/native-renderer decision needed unless a live runtime reports interop failure. |

Current stop condition: the Dawn-first proof gate has cleared on a mock HMD runtime. The next implementation gate is Aurora EFB target parameterization so `aurora_xr_begin_eye()` installs per-eye render targets and the existing Dusk stereo loop can render into OpenXR swapchain-backed targets. Physical headset runtime coverage is still needed after the mock path, but the no-hardware Monado path is now available for regression testing.
