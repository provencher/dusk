# OpenXR Windows Carry-Home Plan — 2026-05-11

# 1. **Summary**

Carry Windows OpenXR from the current scaffold to a working desktop Vulkan OpenXR launch by making Aurora’s XR layer real and keeping Dusk’s existing VR/camera scaffolding mostly intact. This should be a targeted Aurora-first implementation, not a Dusk-wide refactor: Dusk already has `backend.xrMode`, startup policy, per-eye camera composition, world-projected reticle routing, and the `mDoGph_Painter()` stereo loop. The blocking work is inside Aurora: create a persistent OpenXR runtime/session, bridge Dawn/WebGPU’s Vulkan device/images to OpenXR swapchain images, parameterize Aurora’s EFB render-target seam, and make `aurora_xr_begin_eye()` install per-eye targets that Dusk can render into. If Dawn cannot expose/import the required Vulkan handles/images on Windows, stop at a hard decision gate and choose either a Dawn fork/patch or a larger native Vulkan renderer path.

---

# 2. **Current-state analysis**

## Existing ownership and data flow

### Settings and startup policy

- Persistent setting:
  - `include/dusk/settings.h`
    - `enum class XrMode { Disabled, Optional, Required }`
    - `UserSettings::backend.xrMode`
  - `src/dusk/settings.cpp`
    - registers `backend.xrMode`
- UI:
  - `src/dusk/ui/settings.cpp`
    - `add_xr_mode_control()` exposes **OpenXR Mode**
    - messaging currently says active XR rendering is blocked until Vulkan/OpenXR swapchain interop exists.
- Startup:
  - `src/m_Do/m_Do_main.cpp`
    - `ResolveStartupXrPolicy()`:
      - if `backend.xrMode != Disabled`, validates Vulkan availability.
      - forces `config.desiredBackend = BACKEND_VULKAN`.
      - sets `AuroraConfig.enableOpenXR` and `AuroraConfig.requireOpenXR`.
    - after `aurora_initialize()`, current Required behavior checks `aurora_xr_is_active()`.
      - This is too strict for real OpenXR because a real session may initialize successfully as `READY` before becoming focused/visible/actively rendering.

### Aurora startup/frame lifecycle

- Public API:
  - `extern/aurora/include/aurora/aurora.h`
    - `AuroraConfig::enableOpenXR`
    - `AuroraConfig::requireOpenXR`
    - `AuroraXRStatus`
    - `AuroraXRView`
    - `AuroraXRFrameState`
    - `aurora_xr_begin_eye()`, `aurora_xr_end_eye()`, `aurora_xr_begin_flat_ui()`, `aurora_xr_end_flat_ui()`
- Aurora orchestration:
  - `extern/aurora/lib/aurora.cpp`
    - `initialize()` creates SDL window and WebGPU/Dawn backend, then calls `xr::initialize(g_config, selectedBackend)`.
    - `begin_frame()`:
      1. calls `xr::on_aurora_frame_start()`;
      2. acquires the flat window surface texture;
      3. starts ImGui frame;
      4. calls `gfx::begin_frame()`;
      5. calls `xr::begin_frame()`.
    - `end_frame()`:
      1. drains GX FIFO;
      2. calls `gfx::end_frame()`;
      3. calls `gfx::render()`;
      4. copies Aurora’s normal EFB present source to the window surface;
      5. renders RmlUi/ImGui to the window surface;
      6. submits the command buffer;
      7. calls `gfx::after_submit()`;
      8. calls `xr::end_frame_after_submit()`;
      9. presents the flat surface.

### Current XR implementation

- `extern/aurora/lib/xr/xr.cpp`
  - owns process-global `g_state`.
  - tracks requested/required/status/message/frameState/views.
  - `begin_frame()` increments frame index but forces `frameState.shouldRender = false`.
  - `begin_eye()` always returns `false`.
  - `begin_flat_ui()` always returns `false`.
- `extern/aurora/lib/xr/openxr_probe.cpp`
  - compiled only with `AURORA_HAS_OPENXR`.
  - verifies:
    - selected backend is Vulkan;
    - OpenXR loader/runtime can enumerate extensions;
    - runtime exposes `XR_KHR_vulkan_enable2`;
    - instance/system/stereo view configuration exist.
  - currently returns `AURORA_XR_BLOCKED` because Aurora cannot yet bridge Dawn/WebGPU Vulkan handles to OpenXR swapchain images.

### WebGPU/Dawn ownership

- `extern/aurora/lib/webgpu/gpu.cpp`
  - owns global Dawn/WebGPU objects:
    - `g_instance`
    - `g_adapter`
    - `g_device`
    - `g_queue`
    - `g_surface`
    - `g_graphicsConfig`
    - `g_frameBuffer`
    - `g_frameBufferResolved`
    - `g_depthBuffer`
  - creates the SDL-backed `wgpu::Surface`.
  - requests Dawn adapter/device.
  - configures the window swapchain.
  - creates normal render/depth textures.
- `extern/aurora/lib/dawn/BackendBinding.cpp`
  - Windows path uses SDL HWND/HINSTANCE to build `wgpu::SurfaceSourceWindowsHWND`.
- Current blocker:
  - no Aurora API exposes the underlying Vulkan `VkInstance`, `VkPhysicalDevice`, `VkDevice`, queue family/index, or queue.
  - no Aurora path wraps OpenXR-owned `VkImage` swapchain images as renderable `wgpu::Texture`s.

### Aurora GX render target seam

- `extern/aurora/lib/gfx/common.cpp`
  - `RenderPass` stores:
    - `colorView`
    - `resolveView`
    - `depthView`
    - `copySourceTexture`
    - `copySourceView`
    - `copySourceDepthView`
    - `targetSize`
    - `msaaSamples`
  - `set_efb_targets(RenderPass&)` currently hardcodes:
    - `webgpu::g_frameBuffer`
    - `webgpu::g_frameBufferResolved`
    - `webgpu::g_depthBuffer`
  - `gfx::begin_frame()` creates `g_renderPasses[0]` and immediately calls `set_efb_targets()`.
  - `gfx::end_offscreen()` resumes EFB rendering and also calls `set_efb_targets()`.
- This is the smallest practical render seam for XR:
  - parameterize the active EFB target source;
  - make XR eye begin/end install and clear target overrides;
  - ensure each eye records into a distinct `RenderPass`.

### Dusk VR service and rendering scaffold

- `include/dusk/vr/vr.hpp`, `src/dusk/vr/vr.cpp`
  - `dusk::vr::begin_frame()` reads:
    - `aurora_xr_get_frame_state()`
    - `aurora_xr_is_requested()`
    - `aurora_xr_is_active()`
    - `aurora_xr_should_render()`
    - `aurora_xr_get_view_count()`
    - `aurora_xr_get_view()`
  - `begin_eye_view(view_class&, eyeIndex, EyeViewToken&)`:
    - saves `view_class`;
    - composes recentered XR eye pose over the existing game camera;
    - builds asymmetric projection from `AuroraXRView::fov`;
    - writes temporary per-eye matrices;
    - restores via `end_eye_view()`.
  - recenter:
    - `F8` calls `dusk::vr::recenter_from_latest_hmd_pose()`.
- `src/m_Do/m_Do_graphic.cpp`
  - `mDoGph_Painter()` already has an XR branch:
    - checks `dusk::vr::should_render()` and eye count;
    - loops eyes;
    - calls `aurora_xr_begin_eye(eyeIndex)`;
    - calls `dusk::vr::begin_eye_view()`;
    - draws world/3D;
    - draws world-projected packets;
    - calls `dusk::vr::end_eye_view()`;
    - calls `aurora_xr_end_eye()`.
  - Today this never renders XR because Aurora never sets `shouldRender=true` and `begin_eye()` always returns false.
- World-projected reticles:
  - `include/d/d_com_inf_game.h`
    - `dComIfGd_setWorldProjected2DXlu()` queues through `dusk::vr` while XR active.
  - `src/d/actor/d_a_player.cpp`
    - player sight reticle recomputes projection during draw while XR active.
  - `src/d/actor/d_a_boomerang.cpp`
    - boomerang lock cursor recomputes projection during draw while XR active.

## Reusable code

Reuse these existing pieces rather than duplicating them:

- `ResolveStartupXrPolicy()` for backend forcing and Optional/Required policy.
- `dusk::vr::begin_frame()` and `begin_eye_view()` for all per-eye camera math.
- `mDoGph_Painter()` eye loop and world/3D partition.
- `dComIfGd_setWorldProjected2DXlu()` queue seam.
- `gfx::RenderPass` command recorder and existing GX/WebGPU pipeline.
- `openxr_probe.cpp` extension/system/view configuration checks, but convert from “probe only” into real runtime initialization.

## Blocking constraints

- OpenXR active rendering requires:
  - real Windows OpenXR loader/runtime;
  - runtime exposing `XR_KHR_vulkan_enable2`;
  - Vulkan-capable runtime;
  - valid `XrGraphicsBindingVulkanKHR`;
  - OpenXR color swapchain images renderable by Aurora’s graphics path.
- Current Dawn/WebGPU path may not expose:
  - native Vulkan handles;
  - required OpenXR Vulkan instance/device extension control;
  - wrapping/importing runtime-owned `VkImage` as renderable `wgpu::Texture`.
- If Dawn cannot support these on Windows, a working implementation requires either:
  1. a Dawn fork/patch; or
  2. a larger native Vulkan XR eye-target renderer path.

---

# 3. **Design**

## Decision: targeted Aurora refactor, not broad Dusk refactor

Use a targeted Aurora-focused implementation because Dusk’s side already has the needed XR settings, camera composition, reticle routing, and stereo render loop. The only Dusk changes should be startup semantics for `READY` vs `ACTIVE`, validation/messaging, and optionally duplicating native 2D HUD into eye passes. A broad Dusk renderer refactor should be avoided unless the Dawn/WebGPU Vulkan interop decision gate fails.

---

## Phase 0 — Windows environment and baseline verification

### Goals

Confirm the Windows machine can build the current scaffold with OpenXR enabled and can run Vulkan flat mode before touching runtime code.

### Required environment

Use the Windows setup documented in `AGENTS.md`:

- Repo:
  - `<repo>`
- VS dev shell:
  - `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat`
- Preset:
  - `windows-msvc-relwithdebinfo`
- Executable:
  - `build\windows-msvc-relwithdebinfo\dusk.exe`
- Local launcher:
  - `run-dusk.bat`
- Local disc:
  - `game.ciso`
  - Must remain untracked and must not be committed.

### Commands

From PowerShell or `cmd.exe`:

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --preset windows-msvc-relwithdebinfo -DAURORA_ENABLE_OPENXR=ON"
```

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --build --preset windows-msvc-relwithdebinfo"
```

Flat Vulkan smoke test:

```bat
cd /d <repo>
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan .\game.ciso
```

OpenXR runtime registry check:

```bat
reg query HKLM\SOFTWARE\Khronos\OpenXR\1 /v ActiveRuntime
```

Vulkan runtime check, if Vulkan SDK tools are installed:

```bat
where vulkaninfo
vulkaninfo --summary
```

### Decision gate 0

Proceed only if:

- `AURORA_ENABLE_OPENXR=ON` configures successfully.
- `aurora_core` links with an OpenXR loader target.
- flat `--backend vulkan` launch works.
- the active OpenXR runtime is known and Vulkan-capable.

If CMake reports no OpenXR loader target:

- Optional XR should still build stubs.
- Do not attempt active XR runtime work until the loader SDK/package is installed and discoverable by CMake.

---

## Phase 1 — Replace probe/stub XR with persistent OpenXR lifecycle

### Component: Aurora XR runtime state

Modify `extern/aurora/lib/xr/xr.cpp` and the OpenXR-compiled implementation currently in `extern/aurora/lib/xr/openxr_probe.cpp`.

Keep `xr.cpp` as the API-facing state owner that compiles in every build. Move OpenXR SDK-dependent code behind `AURORA_HAS_OPENXR`.

Recommended internal state shape:

```cpp
// Illustrative only.
struct State {
  bool requested;
  bool required;

  AuroraXRStatus status;
  std::string statusMessage;
  AuroraXRFrameState frameState;

  std::vector<AuroraXRView> views;

  bool sessionRunning;
  bool frameBegun;
  bool frameShouldRender;
  bool eyeActive;
  uint32_t activeEyeIndex;
  bool flatUiActive;

  // OpenXR-backed runtime object exists only when AURORA_HAS_OPENXR.
  std::unique_ptr<OpenXRRuntime> runtime;
};
```

OpenXR runtime object owns:

```cpp
// Illustrative only.
struct OpenXRRuntime {
  XrInstance instance;
  XrSystemId systemId;
  XrSession session;

  XrSpace localSpace;
  XrSpace viewSpace;

  XrViewConfigurationType viewConfigType; // PRIMARY_STEREO
  std::vector<XrViewConfigurationView> configViews;
  std::vector<XrView> locatedViews;

  XrFrameState xrFrameState;
  XrSessionState sessionState;

  PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR;
  PFN_xrCreateVulkanInstanceKHR xrCreateVulkanInstanceKHR;
  PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR;
  PFN_xrGetVulkanGraphicsDevice2KHR xrGetVulkanGraphicsDevice2KHR;

  std::vector<EyeSwapchain> eyeSwapchains;
};
```

### OpenXR initialization behavior

Replace the current probe-only behavior with real initialization:

1. Validate selected backend:
   - if not `BACKEND_VULKAN`, set `AURORA_XR_BLOCKED`.
2. Enumerate instance extensions.
3. Require:
   - `XR_KHR_vulkan_enable2`.
4. Create `XrInstance`.
5. Load `XR_KHR_vulkan_enable2` function pointers.
6. Get `XrSystemId` for `XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY`.
7. Query `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO`.
8. Store `recommendedWidth`, `recommendedHeight`, and `recommendedSampleCount` in `AuroraXRView`.
9. Create `LOCAL` reference space.
10. Create `VIEW` reference space for future flat UI.
11. Delay or complete session creation based on the Vulkan bridge decision in Phase 2.

### Startup status semantics

Use these status meanings:

- `AURORA_XR_DISABLED`
  - `enableOpenXR=false`.
- `AURORA_XR_UNAVAILABLE`
  - loader/runtime/instance/system unavailable.
- `AURORA_XR_BLOCKED`
  - runtime found but requirements are unmet:
    - non-Vulkan backend;
    - missing `XR_KHR_vulkan_enable2`;
    - Dawn/Vulkan bridge unsupported;
    - no compatible swapchain format.
- `AURORA_XR_READY`
  - runtime/session/swapchains are initialized, but session is not currently rendering/focused.
- `AURORA_XR_ACTIVE`
  - session is running and frame rendering is possible.
- `AURORA_XR_LOST`
  - session/runtime loss occurred after initialization.

### Important Dusk startup change

Modify `src/m_Do/m_Do_main.cpp` after `aurora_initialize()`:

Current behavior:

- Required XR fatal-errors unless `aurora_xr_is_active()` is true immediately.

Required real lifecycle behavior:

- Treat `AURORA_XR_READY` and `AURORA_XR_ACTIVE` as successful startup.
- Fatal for Required only when status is:
  - `DISABLED`
  - `UNAVAILABLE`
  - `BLOCKED`
  - `LOST`

Reason: a real OpenXR session may not become active until the runtime transitions through session states during frame/event processing.

### Frame/event handling

In `xr::begin_frame()`:

1. Poll all OpenXR events.
2. React to session state changes:
   - `XR_SESSION_STATE_READY`
     - call `xrBeginSession()`;
     - mark session running.
   - `XR_SESSION_STATE_SYNCHRONIZED`, `VISIBLE`, `FOCUSED`
     - session remains running.
   - `XR_SESSION_STATE_STOPPING`
     - call `xrEndSession()`;
     - mark session not running.
   - `XR_SESSION_STATE_EXITING`, `LOSS_PENDING`
     - set `AURORA_XR_LOST`.
3. If not session running:
   - `frameState.shouldRender=false`;
   - status should be `READY` unless lost.
4. If session running:
   - call `xrWaitFrame()`;
   - call `xrBeginFrame()`;
   - call `xrLocateViews()` using:
     - `localSpace`;
     - predicted display time from `XrFrameState`.
5. Populate `AuroraXRView` for each eye:
   - `pose`
   - `fov`
   - `orientationValid`
   - `positionValid`
   - `orientationTracked`
   - `positionTracked`
   - `fovValid=true` when locate succeeded.
6. Set:
   - `frameState.shouldRender = xrFrameState.shouldRender && swapchainsReady && viewsLocated`.
   - `status = ACTIVE` when session running and visible/focused enough to render.
   - `status = READY` otherwise.

### `xr::end_frame_after_submit()`

This becomes the place where OpenXR frame submission completes.

Required behavior:

1. If no OpenXR frame was begun, do nothing.
2. If eye swapchain images were acquired for this frame:
   - release them after WebGPU command submission.
3. Build `XrCompositionLayerProjectionView` only if all required stereo eyes rendered successfully.
4. Call `xrEndFrame()` exactly once for every successful `xrBeginFrame()`.
5. If not all eyes rendered:
   - end the frame with zero layers.
   - do not submit a partial one-eye projection layer.
6. Reset per-frame eye acquisition/render flags.

### Error handling

- `xrWaitFrame()` failure:
  - set `shouldRender=false`;
  - if recoverable, keep status `READY`;
  - if session loss, set `LOST`.
- `xrBeginFrame()` failure:
  - set `shouldRender=false`;
  - do not call `xrEndFrame()` unless `xrBeginFrame()` succeeded.
- `xrLocateViews()` failure:
  - set `shouldRender=false`;
  - keep session state.
- Event loss/runtime loss:
  - release/destroy swapchains/session/spaces in safe order;
  - Optional mode degrades to flat;
  - Required mode should only fatal at startup, not necessarily on runtime loss unless the existing app fatal policy is explicitly extended.

---

## Phase 2 — Dawn/WebGPU Vulkan-native investigation

This is a hard decision gate before implementing the render bridge.

### Files to inspect

- `extern/aurora/lib/webgpu/gpu.cpp`
- `extern/aurora/lib/webgpu/gpu.hpp`
- `extern/aurora/cmake/AuroraDawnProvider.cmake`
- generated/downloaded Dawn package/source under:
  - `build\windows-msvc-relwithdebinfo\_deps\dawn_prebuilt-src`
  - or vendor Dawn source if configured with `AURORA_DAWN_PROVIDER=vendor`
- Dawn headers to search:
  - `dawn/native/DawnNative.h`
  - `dawn/native/VulkanBackend.h`
  - WebGPU/Dawn shared texture memory headers
  - any `ExternalImage`, `SharedTextureMemory`, `VkImage`, or `WrapVulkan` APIs.

### Questions to answer

1. Can Aurora retrieve the native Vulkan handles used by Dawn?
   - `VkInstance`
   - `VkPhysicalDevice`
   - `VkDevice`
   - graphics `VkQueue`
   - queue family index
   - queue index
2. Can Aurora influence Dawn’s Vulkan instance/device extensions before device creation?
   - Needed because OpenXR runtimes may require Vulkan extensions.
3. Can Aurora wrap/import an OpenXR-owned `VkImage` as a renderable `wgpu::Texture`?
   - Required usage:
     - render attachment
     - texture binding if used for mirror copy
     - copy source if mirror copy uses copy path
4. Can wrapped textures use a format matching Aurora GX pipelines?
   - First target should match `webgpu::g_graphicsConfig.surfaceConfiguration.format`.
5. Can WebGPU command submission safely render to these imported images before `xrEndFrame()`?

### Investigation commands

Configure with prebuilt Dawn first:

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --preset windows-msvc-relwithdebinfo -DAURORA_ENABLE_OPENXR=ON -DAURORA_DAWN_PROVIDER=package"
```

If package headers do not expose required native APIs, configure vendor Dawn for source/header inspection:

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --preset windows-msvc-relwithdebinfo -DAURORA_ENABLE_OPENXR=ON -DAURORA_DAWN_PROVIDER=vendor"
```

### Decision gate 2

Proceed to Phase 3 only if Dawn supports all of:

- native Vulkan handle access;
- OpenXR-compatible Vulkan instance/device extension handling or proof that current Dawn-created device satisfies the active runtime;
- wrapping/importing OpenXR `VkImage` as renderable `wgpu::Texture`.

If any answer is “no”:

- Do not partially implement active XR rendering.
- Choose Phase 4 fallback:
  - Dawn fork/patch; or
  - larger native Vulkan XR eye-target renderer decision.

---

## Phase 3 — Primary render bridge: Dawn supports Vulkan/OpenXR interop

## 3.1 Expose Vulkan handles from Aurora WebGPU

### Component: `webgpu::VulkanDeviceInfo`

Add an internal Aurora WebGPU interface in `extern/aurora/lib/webgpu/gpu.hpp`.

Illustrative shape:

```cpp
struct VulkanDeviceInfo {
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue queue;
  uint32_t queueFamilyIndex;
  uint32_t queueIndex;
  uint32_t apiVersion;
};
```

Contract:

- Valid only when:
  - `g_backendType == wgpu::BackendType::Vulkan`;
  - Dawn backend is Vulkan;
  - device initialization succeeded.
- Ownership:
  - borrowed handles;
  - Aurora must not destroy them directly.
- Failure:
  - return `false` or `std::optional` empty if not Vulkan or unavailable.

### Extension requirements

If Dawn can accept required Vulkan extensions before device creation:

1. Add a small pre-WebGPU XR query step in `extern/aurora/lib/aurora.cpp`.
2. `xr` creates OpenXR instance/system early enough to query Vulkan requirements/extensions.
3. Pass requirements into `webgpu::initialize()`.

If Dawn cannot accept extra extensions:

- Validate the created Dawn Vulkan device against OpenXR graphics requirements.
- Attempt session creation.
- If session creation fails due to graphics binding/extension mismatch, report `AURORA_XR_BLOCKED`.

## 3.2 Bind OpenXR to Dawn’s Vulkan device

After `webgpu::initialize(BACKEND_VULKAN)` succeeds:

1. Obtain `webgpu::VulkanDeviceInfo`.
2. Populate `XrGraphicsBindingVulkanKHR`.
3. Call `xrCreateSession()`.
4. Create `LOCAL` and `VIEW` spaces if not already created.
5. Create eye swapchains.

Validation:

- `xrGetVulkanGraphicsRequirements2KHR()` must pass.
- Dawn Vulkan API/device must satisfy runtime min/max API version.
- `xrCreateSession()` must succeed.

## 3.3 Swapchain format selection

In the OpenXR runtime:

1. Call `xrEnumerateSwapchainFormats()`.
2. Prefer a Vulkan format matching Aurora’s current WebGPU surface format:
   - `VK_FORMAT_B8G8R8A8_UNORM` for `wgpu::TextureFormat::BGRA8Unorm`.
   - `VK_FORMAT_R8G8B8A8_UNORM` for `wgpu::TextureFormat::RGBA8Unorm`.
3. For first Windows launch, do not add separate GX pipelines for XR-only formats.
4. If no exact compatible format exists:
   - set `AURORA_XR_BLOCKED`;
   - message should list the required WebGPU format and available OpenXR formats.

Rationale: current GX pipeline creation likely assumes `webgpu::g_graphicsConfig.surfaceConfiguration.format`; supporting a different XR color format is a larger pipeline-variant change.

## 3.4 Swapchain sample count

For first launch:

- Force XR color swapchains to `sampleCount = 1`.
- Create XR depth targets with sample count `1`.
- Ignore `g_graphicsConfig.msaaSamples` for XR eye targets.

Rationale: this avoids ambiguous OpenXR multisample swapchain support and WebGPU resolve behavior. MSAA can be reintroduced after stable eye rendering.

## 3.5 Wrap OpenXR swapchain images as WebGPU textures

Each `EyeSwapchain` owns:

```cpp
struct EyeSwapchainImage {
  XrSwapchainImageVulkanKHR xrImage;
  webgpu::TextureWithSampler texture;
  bool wrapped;
};

struct EyeSwapchain {
  XrSwapchain handle;
  uint32_t width;
  uint32_t height;
  int64_t xrFormat;
  wgpu::TextureFormat wgpuFormat;

  std::vector<EyeSwapchainImage> images;
  webgpu::TextureWithSampler depth;

  bool acquired;
  uint32_t acquiredIndex;
  bool renderedThisFrame;
};
```

Behavior:

- Enumerate swapchain images once after swapchain creation.
- Wrap each `VkImage` into `webgpu::TextureWithSampler`.
- The wrapper must use:
  - `wgpu::TextureUsage::RenderAttachment`
  - `wgpu::TextureUsage::TextureBinding` if mirror sampling is used
  - `wgpu::TextureUsage::CopySrc` if mirror copy is used
- Create one WGPU-owned depth texture per eye.

## 3.6 Parameterize Aurora EFB target source

### Component: `gfx::EfbTargetSet`

Add internal type in `extern/aurora/lib/gfx/common.hpp`.

Illustrative shape:

```cpp
struct EfbTargetSet {
  const webgpu::TextureWithSampler* color;
  const webgpu::TextureWithSampler* resolve;
  const webgpu::TextureWithSampler* depth;
  uint32_t sampleCount;
};
```

Modify `set_efb_targets(RenderPass&)` in `common.cpp`:

- If an XR override is active:
  - bind target from override.
- Otherwise:
  - use current default:
    - `webgpu::g_frameBuffer`
    - `webgpu::g_frameBufferResolved`
    - `webgpu::g_depthBuffer`

Add internal functions:

```cpp
bool begin_efb_target_override(const EfbTargetSet& target, const char* debugLabel);
void end_efb_target_override();
bool efb_target_override_active() noexcept;
```

Required behavior:

- `begin_efb_target_override()` must start a distinct `RenderPass` for the eye.
- If the current pass is empty and still the initial default pass, it may retarget that pass.
- If commands already exist, append a new pass.
- Reset cached viewport/scissor into the new pass so subsequent GX commands are valid.
- `end_efb_target_override()` must clear the override and append/resume a normal default EFB pass for any subsequent flat/mirror UI commands.

Do not rely on simply changing a global pointer after `gfx::begin_frame()`; pass 0 has already been initialized.

## 3.7 Implement `aurora_xr_begin_eye()` and `aurora_xr_end_eye()`

### `xr::begin_eye(uint32_t eyeIndex)`

Before:

```cpp
bool begin_eye(uint32_t eyeIndex) noexcept {
  ...
  return false;
}
```

After behavior:

1. Validate:
   - OpenXR requested;
   - status active/ready enough for current frame;
   - `frameState.shouldRender == true`;
   - no eye already active;
   - no flat UI active;
   - valid eye index;
   - swapchain exists.
2. Acquire image:
   - `xrAcquireSwapchainImage()`.
3. Wait image:
   - `xrWaitSwapchainImage()`.
4. Install EFB override:
   - build `gfx::EfbTargetSet` from acquired image texture and eye depth texture;
   - call `gfx::begin_efb_target_override()`.
5. Mark:
   - `eyeActive=true`;
   - `activeEyeIndex=eyeIndex`;
   - `acquired=true`.

Return `true` only after all steps succeed.

### `xr::end_eye()`

Do not release the OpenXR swapchain image here.

Reason: Aurora only records GX commands during `mDoGph_Painter()`; actual WebGPU command encoding/submission happens later in `aurora::end_frame()`.

Behavior:

1. Validate an eye is active.
2. Call `gfx::end_efb_target_override()`.
3. Mark the eye rendered.
4. Clear active eye state.
5. Queue the acquired image for release in `xr::end_frame_after_submit()`.

## 3.8 OpenXR frame submission after WebGPU submit

Modify `extern/aurora/lib/aurora.cpp::end_frame()`:

- Keep command order:
  1. `gfx::end_frame(encoder)`
  2. `gfx::render(encoder)`
  3. optional mirror/window render
  4. `g_queue.Submit(...)`
  5. `gfx::after_submit()`
  6. `xr::end_frame_after_submit()`

`xr::end_frame_after_submit()` then:

1. releases all acquired XR swapchain images;
2. calls `xrEndFrame()` with:
   - projection layer if all eyes rendered;
   - no layers otherwise.

This ordering prevents releasing OpenXR images before WebGPU has submitted commands that reference them.

## 3.9 Mirror window behavior

Decision for first Windows launch:

- HMD rendering is primary.
- Mirror window should be best-effort and must not block HMD rendering.

Preferred mirror path if wrapped XR textures can be sampled:

1. Use the first rendered eye as a mirror source.
2. In `aurora.cpp::end_frame()`, before the existing EFB copy pass, ask XR for a mirror `TextureWithSampler`.
3. Use existing `webgpu::create_copy_bind_group()` / copy pipeline to render the eye texture to the window surface.
4. Render ImGui on top as today.
5. Release XR swapchain images after queue submit.

If this cannot be done safely:

- Present the normal flat EFB if it has content.
- Otherwise allow the mirror window to be blank/flat while HMD rendering works.
- Log once:
  - “OpenXR mirror window unavailable; rendering to HMD only.”

Do not make mirror failure disable XR in Optional mode.

---

## Phase 4 — Fallback render bridge if Dawn does not support required interop

This phase is a decision plan, not an implementation to start automatically.

### Option A — Dawn fork/patch

Prefer this if the missing pieces are narrow:

- expose Vulkan handles;
- allow required Vulkan extensions;
- expose/import `VkImage` as renderable WGPU texture.

Impacted area:

- Dawn provider/version in:
  - `extern/aurora/cmake/AuroraDawnProvider.cmake`
  - `extern/aurora/CMakeLists.txt`
- Aurora WebGPU bridge in:
  - `extern/aurora/lib/webgpu/gpu.cpp`
  - `extern/aurora/lib/webgpu/gpu.hpp`

Exit criteria:

- Aurora can create an OpenXR Vulkan session using Dawn’s device.
- Aurora can render a simple clear into an OpenXR swapchain image before involving GX.

### Option B — Native Vulkan XR eye-target path

This is a larger renderer decision.

Potential approaches:

1. Reuse Aurora GX recorded command data:
   - `extern/aurora/lib/gfx/common.cpp::RenderPass`
   - `Command`
   - `ShaderDrawCommand`
   - `gx::DrawData`
2. Add a Vulkan encoder parallel to the current WGPU encoder.
3. Reimplement enough of:
   - GX pipeline creation;
   - bind groups/descriptors;
   - uniform/storage/vertex/index buffer uploads;
   - texture binding;
   - render pass execution.

This is not a small bridge. It risks duplicating the renderer.

Decision gate:

- Do not begin Option B without an explicit renderer architecture decision document.
- If Option B is chosen, create a separate plan before coding.

### Option C — Stop and keep XR blocked

If neither Dawn patch nor native Vulkan renderer is approved:

- Keep `AURORA_XR_BLOCKED`.
- Improve status message:
  - “OpenXR runtime detected, but active rendering is blocked because this Dawn/WebGPU build cannot expose/import Vulkan images required for OpenXR swapchains.”
- Preserve Optional/Required fallback behavior.

---

## Phase 5 — Dusk integration and flat UI behavior

## 5.1 Keep Dusk VR/camera scaffold intact

No changes should be needed in:

- `include/dusk/vr/vr.hpp`
- `src/dusk/vr/vr.cpp`
- `include/f_op/f_op_view.h`
- `src/d/d_camera.cpp`
- player/boomerang reticle conversions

except if validation finds a pose-space or projection bug.

Expected data flow after Aurora work:

1. `aurora::xr::begin_frame()` locates views.
2. `dusk::vr::begin_frame()` copies `AuroraXRView` data.
3. `mDoGph_Painter()` sees `dusk::vr::should_render() == true`.
4. For each eye:
   - `aurora_xr_begin_eye()` installs eye render target.
   - `dusk::vr::begin_eye_view()` installs eye camera.
   - Dusk draws world and world-projected packets.
   - `dusk::vr::end_eye_view()` restores camera.
   - `aurora_xr_end_eye()` ends target override.
5. `aurora::end_frame()` submits GPU work and ends the OpenXR frame.

## 5.2 Native 2D HUD in XR

For first launch, implement one of these explicit behaviors.

### Preferred: draw native flat HUD into both eyes

Modify `src/m_Do/m_Do_graphic.cpp` locally:

- Factor the native 2D draw-list tail into a reusable lambda/section.
- Call it inside each successful eye after:
  - world/3D;
  - world-projected 2D reticles.
- Keep RmlUi and ImGui mirror-window only for this phase.

Native 2D section includes existing calls such as:

- `dComIfGp_particle_draw2Dback`
- `dComIfGp_particle_draw2DmenuBack`
- `dComIfGd_draw2DOpa`
- `drawItem3D`
- `dComIfGd_draw2DOpaTop`
- `dComIfGd_draw2DXlu`
- pause/menu 2D particles/fade as appropriate

Boundary:

- Do not draw world-projected queue in the generic 2D section when XR eye rendering already drew it per eye.
- Keep mono screen-space post effects skipped for XR eye passes.

### Fallback: mirror/monitor-only UI

If duplicating native 2D draw lists into both eyes causes state corruption or visual instability:

- Keep current mirror/flat UI behavior.
- Log/document:
  - HMD receives world + world-projected reticles.
  - ordinary RmlUi/ImGui/native flat UI remains on mirror window for this milestone.
- Create a follow-up for head-locked quad-layer or per-eye flat UI.

## 5.3 RmlUi/ImGui

Do not implement a separate OpenXR quad layer in this phase.

Current state:

- RmlUi and ImGui render in `aurora.cpp::end_frame()` to the flat window surface.
- `aurora_xr_begin_flat_ui()` currently returns false.

For this plan:

- Leave `begin_flat_ui()` returning false unless a separate quad-layer swapchain is implemented later.
- Update documentation so users know Dusk menu/ImGui overlays may remain mirror-window only in the first working HMD build.

---

# 4. **File-by-file impact**

## Documentation

### `docs/plans/openxr-windows-carry-home-2026-05-11.md`

- Add this implementation plan.
- Purpose: preserve Windows-specific OpenXR carry-home details and decision gates.

### `docs/building.md`

- Update after implementation:
  - Windows OpenXR is no longer described as universally blocked if Phase 3 succeeds.
  - Add Windows validation commands.
  - Document `READY` vs `ACTIVE`.
  - Document mirror/UI limitations.
- Depends on completion of Phases 1–5.

---

## Build/CMake

### `extern/aurora/CMakeLists.txt`

- Inspect/change OpenXR/Vulkan dependency discovery.
- If Vulkan headers are required directly by Aurora XR runtime:
  - add `find_package(Vulkan QUIET)` or require Vulkan only when `AURORA_ENABLE_OPENXR=ON` and the bridge path needs it.
- Do not make Vulkan SDK mandatory for non-OpenXR builds.
- Depends on Phase 2 findings.

### `extern/aurora/cmake/aurora_core.cmake`

- Replace or supplement `lib/xr/openxr_probe.cpp` with OpenXR runtime implementation source.
- Link Vulkan target if direct Vulkan symbols/headers require it.
- Keep `AURORA_HAS_OPENXR` compile definition only when loader target exists.
- Depends on Phase 1.

### `extern/aurora/cmake/AuroraDawnProvider.cmake`

- Inspect Dawn provider behavior.
- Only change if:
  - required Dawn native Vulkan headers are absent from package provider;
  - a vendor/fork/patch provider is needed;
  - additional Dawn compile flags are required for Vulkan native interop.
- Depends on Decision gate 2.

### `CMakePresets.json`

- No required change.
- Optional follow-up only:
  - add a Windows OpenXR configure preset if repeated validation needs it.
- Avoid adding this unless it materially reduces validation errors.

---

## Aurora public API

### `extern/aurora/include/aurora/aurora.h`

- Prefer no public API changes.
- Existing structs are sufficient for Dusk:
  - `AuroraXRStatus`
  - `AuroraXRView`
  - `AuroraXRFrameState`
  - eye begin/end APIs.
- Only change if validation proves Dusk needs additional public frame data.
- Backward compatibility:
  - keep all existing symbols.

---

## Aurora XR

### `extern/aurora/lib/xr/xr.hpp`

- Add internal declarations for:
  - XR preinitialization/runtime creation if needed;
  - Vulkan graphics binding handoff;
  - frame lifecycle helpers;
  - mirror texture query if implemented.
- Add no public C API here.
- Depends on Phases 1–3.

### `extern/aurora/lib/xr/xr.cpp`

- Replace stub lifecycle behavior:
  - `begin_frame()` no longer forces `shouldRender=false`.
  - `begin_eye()` no longer always returns false.
  - `end_frame_after_submit()` submits OpenXR frame.
- Preserve no-OpenXR build behavior:
  - no loader target => requested XR reports unavailable, flat fallback works.
- Depends on Phases 1 and 3.

### `extern/aurora/lib/xr/openxr_probe.cpp`

- Convert from probe-only into reusable runtime implementation, or replace with a new `openxr_runtime.cpp`.
- Reuse:
  - extension enumeration;
  - `XR_KHR_vulkan_enable2` check;
  - view configuration enumeration;
  - error message helpers.
- Remove final unconditional BLOCKED result once render bridge is implemented.
- Depends on Phase 1.

---

## Aurora WebGPU/Dawn

### `extern/aurora/lib/webgpu/gpu.hpp`

- Add internal Vulkan-handle query interface if Dawn supports it.
- Add external Vulkan image wrapping interface if Dawn supports it.
- Add helper for XR depth texture creation if needed.
- Depends on Decision gate 2.

### `extern/aurora/lib/webgpu/gpu.cpp`

- Capture native Vulkan handles during `initialize(BACKEND_VULKAN)`.
- Validate backend is Vulkan before exposing handles.
- Implement OpenXR image wrapping if supported.
- Potentially accept XR-required Vulkan extensions before device creation.
- Depends on Decision gate 2 and Phase 3.

### `extern/aurora/lib/dawn/BackendBinding.cpp`

- Inspect only.
- No expected change for XR eye rendering.
- Windows surface handling remains for mirror window.

---

## Aurora GX render recorder

### `extern/aurora/lib/gfx/common.hpp`

- Add internal EFB target override types/functions.
- These are Aurora-internal; do not expose through `aurora.h`.

### `extern/aurora/lib/gfx/common.cpp`

- Parameterize `set_efb_targets(RenderPass&)`.
- Add begin/end EFB target override behavior.
- Ensure each eye target becomes a distinct `RenderPass`.
- Ensure default EFB resumes after XR eye pass.
- Preserve mono behavior when no override is active.
- Depends on Phase 3.

---

## Aurora frame orchestration

### `extern/aurora/lib/aurora.cpp`

- Adjust initialization order if OpenXR Vulkan requirements must be known before WebGPU device creation.
- In `begin_frame()`:
  - process XR frame lifecycle without requiring flat surface success when XR HMD rendering can continue.
  - keep flat fallback behavior unchanged when XR inactive.
- In `end_frame()`:
  - submit WebGPU commands before releasing XR swapchain images;
  - call `xr::end_frame_after_submit()` after `g_queue.Submit()`;
  - optionally mirror first eye to window.
- Depends on Phases 1–3.

---

## Dusk startup and UI

### `src/m_Do/m_Do_main.cpp`

- Update post-`aurora_initialize()` XR Required check:
  - accept `AURORA_XR_READY` and `AURORA_XR_ACTIVE`;
  - fatal only for unavailable/blocked/lost initialization states.
- Preserve Optional fallback:
  - Optional logs warning and continues flat when blocked/unavailable.
- Depends on Phase 1 status semantics.

### `src/dusk/ui/settings.cpp`

- Update OpenXR help text after implementation:
  - remove “rendering remains limited until interop exists” if Phase 3 succeeds;
  - add Windows-only current limitations:
    - Vulkan required;
    - RmlUi/ImGui may be mirror-only;
    - restart required.
- Depends on Phase 5.

### `src/m_Do/m_Do_graphic.cpp`

- Keep existing XR stereo world path.
- Optional/preferred change:
  - factor native 2D draw-list tail into callable section;
  - draw native flat HUD into both eyes after world and world-projected reticles.
- Do not rewrite camera composition here.
- Depends on Phase 5.

---

# 5. **Risks and migration**

## Persistence/migration

- No settings schema migration is required.
- `backend.xrMode` already exists.
- Existing users default to `Disabled`, so flat behavior remains unchanged.

## Dawn native API instability

Risk:

- Dawn native Vulkan APIs may differ between package/vendor versions or may not be installed in prebuilt packages.

Mitigation:

- Gate implementation on Phase 2.
- Prefer narrow wrapper code inside `webgpu/gpu.cpp`.
- Avoid leaking Dawn-native types into Dusk or public Aurora API.

## OpenXR runtime differences

Risk:

- SteamVR, Meta/Oculus, WMR, and Monado may expose different format lists, session timing, and required Vulkan extensions.

Mitigation:

- Log runtime name/version if available.
- Log all swapchain formats when format selection fails.
- Treat unsupported runtime requirements as `AURORA_XR_BLOCKED` in Optional mode.

## Vulkan extension/device mismatch

Risk:

- Dawn-created Vulkan device may not include OpenXR-required extensions.

Mitigation:

- Query OpenXR graphics requirements before session creation.
- If Dawn cannot accept required extensions, fail cleanly with `BLOCKED`.
- Do not create a half-working session.

## Swapchain format mismatch

Risk:

- OpenXR runtime may not expose the same UNORM format as Aurora’s WebGPU GX pipelines.

Mitigation:

- First launch requires exact compatible format.
- Defer alternate XR pipeline formats to follow-up.

## Synchronization and image release

Risk:

- Releasing OpenXR swapchain images before WebGPU command submission would race.

Mitigation:

- `aurora_xr_end_eye()` only ends command recording.
- actual `xrReleaseSwapchainImage()` occurs in `xr::end_frame_after_submit()` after `g_queue.Submit()`.

## Image layout transitions

Risk:

- Dawn/WebGPU may not expose control over Vulkan image layouts required by OpenXR.

Mitigation:

- Validate Dawn’s external image import contract.
- If layout ownership is not guaranteed, stop at Phase 2 and choose Dawn patch/native path.

## MSAA/depth handling

Risk:

- MSAA XR swapchains and depth resolve behavior are runtime-dependent.

Mitigation:

- Force XR eye sample count to `1` for first launch.
- Use WGPU-owned per-eye depth textures.
- Revisit MSAA after stable rendering.

## Mirror window behavior

Risk:

- HMD rendering may work while mirror window is blank or lacks world rendering.

Mitigation:

- Make mirror best-effort.
- Prefer first-eye mirror copy if wrapped texture can be sampled.
- Never let mirror failure disable HMD rendering.

## UI comfort/usability

Risk:

- RmlUi/ImGui mirror-only UI is not comfortable in HMD.

Mitigation:

- Draw native HUD into both eyes if stable.
- Document RmlUi/ImGui limitation.
- Plan quad-layer or head-locked UI as follow-up.

## Flat fallback preservation

Risk:

- XR changes could break normal flat launch.

Mitigation:

- Keep all XR target overrides inactive unless `xrMode` requested and active frame is rendering.
- Validate `backend.xrMode=Disabled` regression before active XR validation.

---

# 6. **Implementation order**

## Step 1 — Save this plan

Create:

```text
docs/plans/openxr-windows-carry-home-2026-05-11.md
```

Commit only documentation if doing a planning-only change.

---

## Step 2 — Windows baseline build

Run:

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --preset windows-msvc-relwithdebinfo -DAURORA_ENABLE_OPENXR=ON"
```

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --build --preset windows-msvc-relwithdebinfo"
```

Validate flat Vulkan:

```bat
cd /d <repo>
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan .\game.ciso
```

---

## Step 3 — Implement real OpenXR lifecycle without eye rendering

Atomic with Step 4 if Required startup semantics would otherwise break.

Change:

- `extern/aurora/lib/xr/xr.cpp`
- `extern/aurora/lib/xr/openxr_probe.cpp` or new runtime file
- `extern/aurora/cmake/aurora_core.cmake`
- `src/m_Do/m_Do_main.cpp`

Target behavior:

- OpenXR instance/system/session setup reaches `READY`.
- `shouldRender` still remains false until swapchains/bridge are ready.
- Required mode accepts `READY`.

Validation:

```bat
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan --cvar backend.xrMode=1 .\game.ciso
```

Expected:

- With runtime: logs `READY` or a specific `BLOCKED`.
- Without runtime: Optional falls back flat.

Required no-runtime validation in PowerShell:

```powershell
$env:XR_RUNTIME_JSON="C:\does-not-exist\missing_openxr_runtime.json"
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan --cvar backend.xrMode=2 .\game.ciso
Remove-Item Env:XR_RUNTIME_JSON
```

Expected:

- startup fails with Required XR error;
- no crash from null runtime handles.

---

## Step 4 — Complete Dawn/WebGPU Vulkan interop investigation

Inspect Dawn package/vendor headers and decide.

If unsupported:

- update XR status message to `BLOCKED`;
- stop active rendering implementation;
- write a short follow-up decision document.

If supported:

- proceed.

---

## Step 5 — Add WebGPU Vulkan handle/image bridge

Change:

- `extern/aurora/lib/webgpu/gpu.hpp`
- `extern/aurora/lib/webgpu/gpu.cpp`
- possibly `extern/aurora/CMakeLists.txt`

Validation target before GX:

- create OpenXR swapchain;
- wrap images;
- perform a minimal clear/render test if feasible inside Aurora XR before Dusk eye rendering.

Do not proceed to Dusk eye path until swapchain images can be wrapped as renderable WGPU textures.

---

## Step 6 — Add GX EFB target override

Change:

- `extern/aurora/lib/gfx/common.hpp`
- `extern/aurora/lib/gfx/common.cpp`

Validation:

- with XR disabled, flat rendering must be byte-for-byte behaviorally unchanged.
- with a test override, commands must record into a non-default render target without corrupting subsequent default EFB rendering.

---

## Step 7 — Implement XR eye begin/end render target install

Change:

- `extern/aurora/lib/xr/xr.cpp`
- OpenXR runtime implementation file
- `extern/aurora/lib/aurora.cpp`

Behavior:

- `aurora_xr_begin_eye()` returns true only when:
  - frame begun;
  - runtime says should render;
  - swapchain image acquired/waited;
  - EFB override installed.
- `aurora_xr_end_eye()` ends override but defers image release.
- `xr::end_frame_after_submit()` releases images and calls `xrEndFrame()`.

Validation:

- Dusk logs/diagnostics show `dusk::vr::should_render()` true.
- `mDoGph_Painter()` enters the eye loop.
- Both eyes render before projection layer submission.

---

## Step 8 — Mirror window and native HUD pass

Change:

- `extern/aurora/lib/aurora.cpp` for mirror copy if supported.
- `src/m_Do/m_Do_graphic.cpp` for optional native 2D HUD-in-eyes.

Validation:

- HMD shows world in both eyes.
- Mirror window is either:
  - first-eye mirror plus ImGui; or
  - documented flat/blank fallback without disabling HMD.

---

## Step 9 — Full Windows validation matrix

### XR off regression

```bat
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan --cvar backend.xrMode=0 .\game.ciso
```

Expected:

- normal flat rendering;
- no XR frame loop;
- no reticle regressions.

### Optional no-runtime fallback

PowerShell:

```powershell
$env:XR_RUNTIME_JSON="C:\does-not-exist\missing_openxr_runtime.json"
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan --cvar backend.xrMode=1 .\game.ciso
Remove-Item Env:XR_RUNTIME_JSON
```

Expected:

- logs unavailable/blocked;
- continues flat.

### Required no-runtime failure

```powershell
$env:XR_RUNTIME_JSON="C:\does-not-exist\missing_openxr_runtime.json"
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan --cvar backend.xrMode=2 .\game.ciso
Remove-Item Env:XR_RUNTIME_JSON
```

Expected:

- startup fails intentionally with clear message.

### Vulkan backend confirmation

```bat
.\build\windows-msvc-relwithdebinfo\dusk.exe --backend vulkan --cvar backend.xrMode=1 .\game.ciso
```

Expected logs:

- selected backend Vulkan;
- OpenXR runtime detected;
- `XR_KHR_vulkan_enable2`;
- compatible swapchain format;
- session ready/active.

### Active XR both-eye render

Expected:

- HMD displays stereo world.
- Head rotation changes view through `dusk::vr::begin_eye_view()`.
- No mono post effects in eye passes.
- Frame does not submit one-eye-only projection layers.

### Reticle stereo attachment

In game scenarios with converted reticles:

- player sight reticle remains attached to target in stereo;
- boomerang lock cursor remains attached to targets in stereo.

### F8 recenter

Press `F8`.

Expected:

- logs success when tracking valid;
- physical yaw/lateral offset recenters;
- game orbit/free camera state is unchanged.
- `Shift+F8` still belongs to State Share.

### Flat UI behavior

Validate whichever Phase 5 behavior was chosen:

- native HUD in both eyes; or
- documented mirror-only flat UI.

### Final checks

```bat
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cd /d <repo> && cmake --build --preset windows-msvc-relwithdebinfo"
```

```bat
git diff --check
```

Ensure no copyrighted assets or local disc images are staged.
