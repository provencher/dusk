# Complete VR Implementation Audit - 2026-05-12

Objective: complete Dusk's Windows Vulkan OpenXR mode as described in
`docs/plans/complete-vr-implementation-2026-05-12.md`.

This audit maps the plan requirements to concrete artifacts and validation
evidence. It is intentionally conservative: simulated-HMD tests and successful
builds are accepted only for the requirements they actually cover.

## Current Verdict

Not complete.

The implementation now covers patched-Dawn direct image wrapping, persistent
eye and flat-UI swapchains, native/RmlUi/ImGui flat UI rendering, default and
SBS mirror paths, conservative culling, no-OpenXR builds, and automated Monado
simulated-HMD validation. Physical testing has started, but SteamVR currently
blocks before HMD presentation because Dawn creates a Vulkan 1.4.329 device
while the runtime reports a maximum compatible Vulkan API version of 1.2.0.
The remaining blockers are resolving that runtime/device compatibility issue,
then completing physical-headset and representative game-scene validation.

## Concrete Completion Criteria

The objective is complete only when all of these are true:

- A Windows Vulkan OpenXR build uses patched-Dawn direct OpenXR `VkImage`
  wrapping/import for eye and flat-UI swapchains.
- Aurora owns OpenXR runtime/session/frame/swapchain/layer submission and Dusk
  owns stereo camera/world redraws.
- Active XR renders stereo world, per-eye world-projected UI, stereo-compatible
  post effects, native game UI, RmlUi, ImGui, recentering, mirror preview, and
  required/optional fallback semantics.
- Automated validation covers build modes, target lifetime, flat UI target
  lifetime, runtime Vulkan extension compatibility, simulated head motion,
  simulated mirror-surface loss, view geometry, and no-OpenXR builds.
- Physical Windows OpenXR headset/runtime validation covers HMD display quality,
  compositor behavior, UI readability, recenter/IPD/FOV comfort, mirror
  resilience, and representative game scenes for post-effect parity.

## Latest Command Evidence

The latest local verification commands and outcomes:

- `cmake --build --preset windows-msvc-dawn-vendor-openxr --target dusk_openxr_probe dusk`
  passed through the VS 2026 developer environment; Ninja reported no work to
  do.
- `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure`
  passed 6/6 through the VS 2026 developer environment. Required gates include `xr_dawn_interop=ready`,
  `xr_proof_gate=cleared`, `xr_eye_target_gate=submitted`,
  `xr_flat_ui_target_gate=submitted`,
  `xr_live_gate=cleared_submitted_flat_ui_submitted`,
  `xr_vulkan_extension_gate=validated`,
  `xr_headless_mirror_gate=continued`,
  `xr_head_motion_gate=validated`, and
  `xr_view_geometry_gate=validated`.
- `cmake --build --preset windows-msvc-relwithdebinfo --target dusk` passed
  through the VS 2026 developer environment; Ninja reported no work to do.
- `ctest --test-dir build\windows-msvc-relwithdebinfo -L openxr --output-on-failure`
  passed 3/3 through the VS 2026 developer environment for the prebuilt-Dawn
  unavailable/missing-interop path.
- `cmake --build --preset windows-msvc-no-openxr --target dusk` passed through
  the VS 2026 developer environment; Ninja reported no work to do.
- `git diff --check` and the corresponding `extern/aurora` submodule
  `diff --check` passed with CRLF warnings only.
- `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -N -L openxr`
  through the VS 2026 developer environment listed 6 registered OpenXR tests:
  smoke, invalid-runtime, Dawn interop, live clear, headless mirror, and
  mock-HMD motion.
- `ctest --test-dir build\windows-msvc-relwithdebinfo -N -L openxr`
  through the VS 2026 developer environment listed 3 registered OpenXR tests:
  smoke, invalid-runtime, and Dawn interop missing.
- `tools/vr_physical_validation.ps1` now provides the remaining manual
  validation entry point: it builds and runs the patched-Dawn OpenXR baseline
  unless skipped, launches Dusk with the requested `backend.xrMode`, captures a
  timestamped transcript under `validation-logs/`, supports the SBS mirror
  path, can set a missing runtime manifest for negative required/optional
  startup tests, and redirects Dusk stdout/stderr into transcript-linked log
  files while waiting for the native process exit code.
- During physical testing on 2026-05-12, the system active OpenXR runtime was
  `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`;
  the default probe reported `xr_status=unavailable`,
  `XR_ERROR_FORM_FACTOR_UNAVAILABLE`, `xr_dawn_interop=ready`, and
  `xr_view_count=0`.
- With `XR_RUNTIME_JSON` set to
  `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`,
  `dusk_openxr_probe.exe --allow-unavailable --require-dawn-interop` detected
  two `2688x2880` stereo views and `xr_dawn_interop=ready`, but reported
  `xr_status=blocked` / `xr_proof_gate=proof_blocked` because Dawn's Vulkan API
  version `1.4.329` exceeds SteamVR's reported maximum `1.2.0`.
- `tools/vr_physical_validation.ps1 -Mode Required -Label steamvr-headset-required-blocked-capture -RuntimeJson "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json" -SkipBaseline`
  captured the required-XR fatal startup path in
  `validation-logs\20260512-194010-steamvr-headset-required-blocked-capture-required-defaultmirror-runtime.log`;
  Dusk exited with code `-1073740791` after logging the same Vulkan API
  compatibility blocker.

Generated CTest files also contain timeouts for every OpenXR probe test:
30 seconds for smoke/invalid-runtime/interop tests, 60 seconds for live clear
and headless mirror, and 90 seconds for mock-HMD motion.

## Evidence Summary

| Requirement | Evidence | Status |
| --- | --- | --- |
| Patched-Dawn direct OpenXR `VkImage` wrapping | `extern/aurora/lib/xr/xr.cpp`, `extern/aurora/lib/webgpu/dawn_vulkan_interop.cpp`; patched preset `windows-msvc-dawn-vendor-openxr` builds. | Implemented |
| Patched-Dawn build flags/provider path | `CMakePresets.json` preset `windows-msvc-dawn-vendor-openxr` sets `AURORA_ENABLE_OPENXR=ON`, `AURORA_FETCH_OPENXR_SDK=ON`, `AURORA_DAWN_PROVIDER=vendor`, `AURORA_DAWN_LINKAGE=static`, and `AURORA_DAWN_APPLY_OPENXR_PATCH=ON`; `extern/aurora/cmake/aurora_core.cmake` adds `AURORA_DAWN_OPENXR_HANDLES`, and `build/windows-msvc-dawn-vendor-openxr/build.ninja` contains that define for Aurora objects. | Verified |
| Persistent OpenXR runtime/session/frame state | `extern/aurora/lib/xr/xr.cpp` owns instance, system, session, spaces, views, frame state, swapchains, layers. | Implemented |
| Stereo eye render targets | `aurora_xr_begin_eye()` / `aurora_xr_end_eye()` wrap acquired runtime images and install EFB targets. | Implemented |
| Flat UI quad layer | `aurora_xr_begin_flat_ui()` / `aurora_xr_end_flat_ui()` acquire a flat UI swapchain and submit `XrCompositionLayerQuad`. | Implemented |
| Native HUD/menu into flat UI | `src/m_Do/m_Do_graphic.cpp` draws the flat 2D section into the OpenXR flat UI target after stereo world rendering. | Implemented |
| RmlUi/ImGui into flat UI | `extern/aurora/lib/aurora.cpp` renders RmlUi and ImGui into the acquired flat UI target; `extern/aurora/lib/imgui.cpp` permits draw-data reuse in the same frame. | Implemented |
| World-projected reticle path | Player sight and boomerang locks use `dComIfGd_setWorldProjected2DXlu()`; audit in `docs/plans/vr-world-projected-ui-audit-2026-05-12.md`. | Partially implemented |
| Post-effect split | World/depth-dependent mono post block runs per eye; screen-frame tail (`2Dgame` particles, `trimming()`, normal `calcFade()`) runs in flat composition; special `F_SP127`/`0x80` fade fallback also stays inside the XR flat UI target when stereo is active; framebuffer/depth capture scratch textures are selected per active XR eye, including motion-blur history. | Partially implemented |
| Post-effect call classification | `docs/plans/vr-post-effects-audit-2026-05-12.md` maps the current `m_Do_graphic.cpp` post-effect calls to per-eye world/depth work, flat composition work, and still-unvalidated scene cases. | Documented, needs scene validation |
| Mirror lifecycle and previews | XR frame can continue without mirror acquisition/presentation; active XR event polling stays non-blocking when the mirror surface is unavailable; default mirror composites eye 0 plus flat UI; SBS debug remains behind `AURORA_XR_MIRROR_SBS=1`; `dusk_openxr_probe --exercise-headless-mirror-frame` is registered as `dusk_openxr_probe_headless_mirror` and requires `xr_headless_mirror_gate=continued`. | Implemented, simulated probe verified, needs physical check |
| Conservative culling | `dusk::vr::conservative_culling_fovy()` replaces hardcoded XR culling widening in `src/d/d_camera.cpp`; `dusk_openxr_probe --validate-view-geometry` validates OpenXR FOV signs/ranges, recommended image metadata, and simulated-HMD IPD plausibility. | Implemented, needs physical check |
| Recenter | `F8` requests app-space recenter from latest valid HMD pose; setting text documents inactive/no-tracking behavior. | Implemented, needs physical check |
| Startup semantics | `backend.xrMode` defaults to `Disabled`; `docs/building.md` and `docs/plans/vr-physical-validation-matrix-2026-05-12.md` list copyable `--backend vulkan --cvar backend.xrMode=0|1|2` validation commands for Disabled, Optional, and Required startup. | Documented, needs runtime check |
| Queue synchronization | Aurora waits for Dawn's Vulkan queue to become idle before releasing acquired OpenXR swapchain images. | Implemented |
| Runtime Vulkan extension validation | `extern/aurora/lib/xr/xr.cpp` queries `xrGetVulkanInstanceExtensionsKHR` / `xrGetVulkanDeviceExtensionsKHR` when available, validates the required strings against the active Vulkan loader and Dawn-selected physical device before session creation, and exposes the result through `aurora_xr_get_vulkan_extension_validation()`; `dusk_openxr_probe_live_clear` requires `xr_vulkan_extension_gate=validated`. | Implemented |
| Default prebuilt-Dawn validation | `ctest --test-dir build\windows-msvc-relwithdebinfo -L openxr --output-on-failure` passed 3/3. | Verified |
| Patched-Dawn simulated-HMD validation | `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure --timeout 120` passed 6/6 after the typed extension-diagnostic API and headless-mirror probe changes. The passing gates include `xr_live_gate=cleared_submitted_flat_ui_submitted`, `xr_vulkan_extension_gate=validated`, `xr_headless_mirror_gate=continued`, `xr_head_motion_gate=validated`, and `xr_view_geometry_gate=validated`. | Verified |
| Bounded OpenXR probe tests | Generated `CTestTestfile.cmake` entries contain `TIMEOUT` for every OpenXR probe test: 30 seconds for smoke/invalid-runtime/interop, 60 seconds for live clear/headless mirror, and 90 seconds for mock-HMD motion. | Verified |
| No-OpenXR build | `cmake --build --preset windows-msvc-no-openxr --target dusk` passed through the VS 2026 developer environment; Ninja reported no work to do. | Verified |
| Physical headset validation | Tracked in `docs/plans/vr-physical-validation-matrix-2026-05-12.md`; SteamVR with the connected headset detects stereo views but blocks before session presentation on the Dawn/OpenXR Vulkan API-version mismatch. | Failed/blocking |

## Plan Item Checklist

### 1. Validate Eye Path on Real Windows OpenXR Runtimes

Implemented/verified:
- Patched-Dawn direct image path builds and passes Monado simulated-HMD tests.
- Runtime-required Vulkan extension strings are logged and validated against the
  active Vulkan loader and Dawn-selected physical device when queryable.
- Queue synchronization policy is implemented before image release.

Missing:
- Physical headset/runtime presentation on SteamVR, Meta, or WMR. The first
  SteamVR run is blocked by Dawn creating a Vulkan API version above the
  runtime's reported maximum.
- Confirmation that physical compositor display output matches the simulated
  target-lifetime proof.

### 2. Flat UI Swapchain and Quad Composition

Implemented/verified:
- Aurora owns `VIEW` space, flat UI swapchain, flat UI target wrapping, and
  `XrCompositionLayerQuad`.
- Probe live gate requires same-frame eye and flat-UI target submission.

Missing:
- Physical HMD readability/comfort validation.

### 3. Native Game 2D/HUD/Menu to Flat UI

Implemented/verified:
- Native flat 2D draw section runs in the OpenXR flat UI target after stereo
  world rendering.
- World-projected queue is excluded from the flat UI layer.

Missing:
- Physical HMD validation for representative HUD/menu screens.

### 4. RmlUi and ImGui into HMD Flat UI

Implemented/verified:
- Aurora renders RmlUi and ImGui into the flat UI image and mirrors them by
  alpha-compositing flat UI over eye 0.

Missing:
- Physical validation for prelaunch/settings UI and ImGui overlays.

### 5. Post Effects for Stereo Parity

Implemented/verified:
- The full mono post block is no longer skipped wholesale in XR; world/depth
  portions run in each eye.
- Screen-frame tail is factored into flat composition.
- Special `F_SP127` / `0x80` fade fallback is drawn before
  `aurora_xr_end_flat_ui()` when XR stereo rendering is active, even if the
  normal 2D draw section is disabled.
- Framebuffer and depth capture scratch textures are selected per active XR eye,
  preventing same-frame left/right capture overwrites for motion blur,
  depth-of-field, bloom, indirect-screen, and repeated capture passes.
- The current call-by-call classification is captured in
  `docs/plans/vr-post-effects-audit-2026-05-12.md`.

Missing:
- Scene validation for water/refraction, bloom, darkworld/filter effects, late
  3D lists, camera-dependent particles, and motion-blur comfort/correctness.
- Eye-indexed handling for any additional stateful history resources that prove
  to need per-eye separation during scene validation.
- Representative scene pass criteria are listed in
  `docs/plans/vr-physical-validation-matrix-2026-05-12.md`.

### 6. Mirror/Window Lifecycle

Implemented/verified:
- XR frame progression is not tied to successful mirror surface acquisition.
- Active XR event polling bypasses `SDL_WaitEvent()` so a paused/unavailable
  mirror surface cannot stall HMD frame progression.
- Default mirror and SBS debug paths exist.
- `dusk_openxr_probe_headless_mirror` is registered and passing; it simulates
  an unavailable native presentation surface and expects
  `xr_headless_mirror_gate=continued`.

Missing:
- Physical HMD validation while minimizing/occluding the mirror window.

### 7. Projection, Culling, and Recenter

Implemented/verified:
- Projection uses OpenXR per-eye FOV.
- Culling FOV uses current OpenXR eye FOV plus a margin.
- Recenter stores app-space yaw/lateral origin from latest valid HMD pose.
- The patched-Dawn mock-HMD test validates OpenXR FOV sign conventions,
  plausible per-eye FOV ranges, recommended image metadata, and stereo eye
  separation/IPD plausibility through `xr_view_geometry_gate=validated`.

Missing:
- Physical validation of FOV sign conventions, IPD/head-translation scale,
  side-view culling, and recenter comfort on a real headset/runtime.

### 8. World-Projected UI Audit

Implemented/verified:
- First known world-attached reticles are converted.
- Audit document classifies remaining projection call sites.

Missing:
- Scene validation for ambiguous projection helpers before any further
  conversion.

### 9. Docs, Settings Text, and Validation Matrix

Implemented/verified:
- `docs/building.md` and `src/dusk/ui/settings.cpp` describe current behavior.
- Physical validation matrix exists and has explicit rows for Disabled,
  Optional, and Required startup semantics.
- Default, patched-Dawn, and no-OpenXR builds have been verified.

Missing:
- Filled physical validation results and known-deviation entries after real
  headset testing.
- Completion still depends on filling the runtime matrix and representative
  scene checklist in `docs/plans/vr-physical-validation-matrix-2026-05-12.md`.
- Intentional VR deviations must be recorded in that matrix's known-deviations
  log after a concrete scene has been tested.
- The matrix's minimum acceptance sequence defines the required order: automated
  baseline, startup semantics, physical runtime activation, core HMD
  presentation, UI/mirror presentation, then representative scene validation.
