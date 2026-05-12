# Complete VR Implementation Plan — 2026-05-12

## Goal
Complete Dusk's Windows Vulkan OpenXR mode from the current `xrDev` state. Dusk keeps owning stereo camera/game rendering; Aurora owns OpenXR runtime/session state, Vulkan swapchains, render-target binding, composition layers, submission, and mirror presentation.

"Complete" means more than the current stereo world preview: active XR must include stereo world rendering, per-eye world-projected UI, stereo-compatible post effects, native game UI, RmlUi/ImGui overlays, recentering, mirror behavior, fallback/status semantics, and validation coverage on a real headset runtime.

## User Decisions
- **Backend target:** Windows Vulkan OpenXR only. Do not preserve WebGPU compatibility, macOS support, or MoltenVK portability if that blocks OpenXR.
- **Interop target:** direct OpenXR `VkImage` wrapping/import. Do not use a copy/intermediate image path as the primary VR design.
- **Ownership:** Dusk owns stereo camera/world redraws; Aurora owns XR runtime, swapchains, render targets, composition, and submission.
- **Completion bar:** full UI/post-effect/projection parity, not just playable stereo world rendering.

## Branch Baseline
This plan has been revalidated against the local branch delta `ZX` (`dc1365b1b5`) → `xrDev` (`a209a6328f`). `xrDev` includes substantial work that was not reflected in the original plan:

- `extern/aurora` advanced from `b2ce4ea` to `0a74422`, adding the Dawn/OpenXR interop proof, persistent runtime, eye swapchains, EFB target overrides, projection submission, and SBS mirror debug support.
- Superproject commits after `ZX` include `1f23836829`, `58b2757a0f`, `28cddefacb`, `c70a6cad00`, `ac0ffff913`, `ef32b23e9e`, `2f0c0d7540`, and `20895328c4` before this plan commit.
- `docs/plans/openxr-dawn-interop-decision-2026-05-11.md` records the patched-Dawn decision path and mock-HMD validation.

## Current State on `xrDev`

### Implemented / Locally Validated
- Aurora now owns persistent OpenXR runtime state: instance, system, session, `LOCAL` space, session state, frame state, located views, projection layer views, and eye swapchains in `extern/aurora/lib/xr/xr.cpp:64`.
- Aurora starts real OpenXR frames, polls session events, waits/begins frames, and locates views in `extern/aurora/lib/xr/xr.cpp:482` and `extern/aurora/lib/xr/xr.cpp:547`.
- The Windows Vulkan/Dawn interop path exists behind patched vendored Dawn: `get_dawn_vulkan_handles()` and `wrap_dawn_vulkan_swapchain_image()` live in `extern/aurora/lib/webgpu/dawn_vulkan_interop.cpp:12` and `extern/aurora/lib/webgpu/dawn_vulkan_interop.cpp:28`.
- Aurora can directly wrap acquired `XrSwapchainImageVulkanKHR::image` values as Dawn/WebGPU textures in `extern/aurora/lib/xr/xr.cpp:851`.
- Aurora has an EFB render-target override API in `extern/aurora/lib/gfx/common.hpp:218` and `extern/aurora/lib/gfx/common.hpp:245`.
- `aurora_xr_begin_eye()` now acquires/waits an OpenXR eye image, wraps it, installs EFB targets, and fills projection-layer view data in `extern/aurora/lib/xr/xr.cpp:817`.
- `aurora_xr_end_eye()` marks the eye rendered and restores default EFB targets in `extern/aurora/lib/xr/xr.cpp:918`.
- XR images are released after WebGPU submission and before `xrEndFrame()`; projection layers are submitted only when all eyes rendered in `extern/aurora/lib/xr/xr.cpp:594`.
- Dusk startup accepts `READY` or `ACTIVE` for Required mode in `src/m_Do/m_Do_main.cpp:439`.
- Dusk passes configurable OpenXR eye dimensions to Aurora from `src/m_Do/m_Do_main.cpp:764`; settings defaults are in `include/dusk/settings.h:199` and `src/dusk/settings.cpp:123`.
- Dusk already runs the stereo world loop: `aurora_xr_begin_eye()`, `dusk::vr::begin_eye_view()`, world draw, world-projected 2D draw, restore, and `aurora_xr_end_eye()` in `src/m_Do/m_Do_graphic.cpp:2626`.
- Mock-HMD/no-headset validation exists through `tools/openxr_probe_main.cpp` and the `dusk_openxr_probe` CTest targets in `CMakeLists.txt:550`.

### Still Missing / Incomplete
- `aurora_xr_begin_flat_ui()` is still a stub and returns false in `extern/aurora/lib/xr/xr.cpp:930`; there is no flat UI swapchain, `VIEW` space, or `XrCompositionLayerQuad` path.
- Native game 2D/HUD/menu drawing is skipped after XR stereo world rendering: `drawFlat2D = !renderedXrStereoWorld && ...` in `src/m_Do/m_Do_graphic.cpp:2736`.
- Mono screen-space post effects are still skipped whenever an XR eye rendered; the fallback-only mono post call is in `src/m_Do/m_Do_graphic.cpp:2652`.
- RmlUi and ImGui render to the window/mirror path, not an HMD flat UI layer.
- The mirror path is debug SBS/eye-preview oriented, not the final eye-0-plus-flat-UI composited mirror.
- `aurora::begin_frame()` can still reject the whole frame before XR begins when the mirror/window surface is unavailable.
- Culling still uses the hardcoded XR widening in `src/d/d_camera.cpp:11106` instead of a Dusk VR helper based on the current eye FOV/pose envelope.
- Only the first world-projected reticle conversions are in place; the full `mDoLib_project()` / `mDoLib_pos2camera()` / marker-packet audit remains open.
- Current validation proves target lifetime and mock-HMD plumbing, not real headset visual correctness, comfort, or game parity.

## Approach
1. **Do not rebuild the solved bridge.** Treat Aurora runtime, patched-Dawn direct `VkImage` wrapping, EFB target overrides, and functional eye targets as implemented on `xrDev`, pending real-headset validation.
2. **Make flat UI the next Aurora/Dusk seam.** Implement a head-locked flat UI swapchain/quad layer and route native 2D, RmlUi, and ImGui into it.
3. **Port post effects deliberately.** Every call in the current mono post block must be classified as per-eye world/post work, flat UI composition work, or intentionally disabled with a validation note. The final milestone must not skip the whole block in XR.
4. **Keep Dusk's stereo camera path.** Dusk continues to redraw world sections per eye with `begin_eye_view()` / `end_eye_view()`, owns projection math and recentering, and audits world-attached UI for per-eye projection.
5. **Make mirror and frame lifecycle HMD-safe.** Mirror preview is useful, but mirror-window failure must not block XR frame pacing/submission.

## Remaining Work Items

### 1. Validate the implemented eye path on real Windows OpenXR runtimes
- Test with a physical headset/runtime, not just Monado/null-compositor mock validation.
- Confirm the required build flags and provider path are documented and reproducible: `AURORA_ENABLE_OPENXR=ON`, Vulkan backend, vendored Dawn, `AURORA_DAWN_APPLY_OPENXR_PATCH=ON`, and `AURORA_DAWN_OPENXR_HANDLES` defined.
- Verify direct image wrapping works on the target runtime without adding a copy/intermediate path.
- Check `xrGetVulkanInstanceExtensions2KHR` / `xrGetVulkanDeviceExtensions2KHR` requirements against the Dawn-created Vulkan instance/device; add explicit validation or extension plumbing if the runtime requires it.
- Audit queue ownership/synchronization around OpenXR calls that may touch the Vulkan queue; current release ordering is after submit and before `xrEndFrame()`, but explicit queue synchronization policy still needs validation.

Validation:
- SteamVR/Meta/WMR target runtime reaches `READY` then `ACTIVE`.
- Both eyes render and submit as projection layers.
- Runtime-specific extension/device incompatibility maps to `BLOCKED` with actionable logging.
- No copy/intermediate image path is introduced for the main eye render target.

### 2. Implement flat UI swapchain and quad-layer composition
- Add `VIEW` space and flat UI swapchain ownership to Aurora XR runtime state.
- Implement `aurora_xr_begin_flat_ui()` / `aurora_xr_end_flat_ui()` to acquire/wait a transparent flat UI image, install an EFB render target, and release after command submission.
- Submit the flat UI as a head-locked `XrCompositionLayerQuad` in `VIEW` space.
- Ensure one flat UI image is acquired at most once per frame, regardless of whether Dusk, Aurora, or both render into it.
- On prelaunch/RmlUi-only frames where `mDoGph_Painter()` does not render native UI, let Aurora acquire and render the flat UI layer itself.

Validation:
- `aurora_xr_begin_flat_ui()` returns true during active XR frames.
- The OpenXR frame submits projection + quad layers when both are available.
- Flat UI absence does not break projection-only frames.

### 3. Route native game 2D/HUD/menu to the flat UI layer
- Factor the native 2D/HUD/menu tail in `mDoGph_Painter()` into a callable flat UI section.
- When XR eyes rendered, draw that section into `aurora_xr_begin_flat_ui()` / `aurora_xr_end_flat_ui()` instead of skipping it via `drawFlat2D = !renderedXrStereoWorld`.
- Keep world-projected packets out of the flat UI layer when stereo eyes rendered.
- Preserve fade/wipe ordering, `dDlst_list_c::calcWipe()`, and frame-interpolation UI behavior.

Validation:
- Game HUD/menu appears in HMD after successful stereo world rendering.
- Existing flat mode rendering order and visuals are unchanged.
- World-projected reticles are not duplicated in flat UI.

### 4. Render RmlUi and ImGui into HMD flat UI
- In `aurora::end_frame()`, render RmlUi and ImGui into the acquired flat UI image in later render passes before command submission.
- If Dusk did not acquire a flat UI target that frame, Aurora should acquire one for RmlUi/ImGui-only presentation.
- Update prelaunch UI flow so XR-active menu/settings UI is visible in HMD.

Validation:
- Prelaunch RmlUi appears in HMD.
- ImGui overlays/console appear in HMD when enabled.
- RmlUi/ImGui still render normally in flat mode.

### 5. Split and port post effects for stereo parity
- Split `draw_mono_screen_space_post_effects_section()` into stereo per-eye work and flat composition work.
- Run world/depth-dependent effects per eye: depth-of-field, bloom, filters, darkworld/invisible/refraction lists, late 3D lists, and particles that depend on the active camera.
- Eye-index stateful resources such as motion blur history, framebuffer captures, depth captures, and indirect-screen intermediates.
- Move screen-frame work such as trimming, color fade overlays, and ordinary 2D-game particles to the flat UI/composition path unless validation proves they are world-projected.
- For any effect intentionally disabled in VR for comfort/correctness, document the reason and test scene; do not use disablement as the default porting strategy.

Validation:
- XR no longer skips the entire mono post block.
- Representative water/refraction, bloom, darkworld/filter, fade, and late-particle scenes render correctly in both eyes.
- Flat mode order and visuals remain unchanged.

### 6. Make mirror/window lifecycle HMD-safe
- Let XR frame progression continue when the mirror window is minimized, paused, or cannot acquire a surface; flat-only mode can keep today's stricter behavior.
- Keep the existing SBS mirror as a debug option, but add a default mirror preview that blits/samples eye 0 plus flat UI when possible.
- If mirror sampling is unavailable, log once and skip mirror preview rather than adding a copy-only mirror path.

Validation:
- HMD rendering continues with an unavailable mirror surface.
- Mirror window shows a best-effort left-eye composited preview when possible.
- XR inactive mirror behavior remains unchanged.

### 7. Complete projection, culling, and recenter validation
- Validate OpenXR FOV sign conventions and Dusk's frustum construction in `src/dusk/vr/vr.cpp`.
- Validate OpenXR-to-game coordinate conversion, IPD/head-translation scale, and recenter offset behavior on a real headset.
- Replace the hardcoded XR culling widening in `src/d/d_camera.cpp` with `dusk::vr` helper(s) that compute a conservative union from current eye FOVs and a head-motion margin.
- Keep F8 recenter as an app-space yaw/lateral offset; do not mutate game camera/orbit state.

Validation:
- Head rotation does not cull side-view geometry prematurely.
- Head translation/IPD scale is plausible.
- F8 recenters with valid tracking, reports inactive/no-tracking states, and preserves game camera state.

### 8. Finish world-projected UI audit
- Search `mDoLib_project(`, `mDoLib_pos2camera(`, `dComIfGd_set2DXlu(`, and cursor/marker packets that store world positions.
- Convert world-attached markers to `dComIfGd_setWorldProjected2DXlu()` and recompute projection in draw-time XR context.
- Decide whether `WorldProjectedItem::modelFromWorld` should be applied during draw or removed if unused; it is currently stored but the draw loop calls only `item.drawList->draw()`.
- Leave pure HUD/menu elements in the flat UI layer.
- Document ambiguous call sites until tested.

Validation:
- Existing player sight and boomerang cursor conversions still attach correctly in stereo.
- Newly converted markers project independently per eye.
- Menus/HUD do not become erroneous stereo world markers.

### 9. Update docs, settings text, and validation matrix
- Update `docs/building.md` and `src/dusk/ui/settings.cpp` so they no longer claim eye swapchain interop is wholly missing; distinguish implemented patched-Dawn eye rendering from remaining flat UI/post-effect limitations.
- Document the no-headset Monado/mock-HMD validation path and its limits: it proves target lifetime/submission plumbing, not real headset display quality.
- Document the physical headset validation matrix and known VR deviations.

Validation:
- Settings text matches current behavior.
- `ctest -L openxr` still passes in the patched vendored Dawn build.
- Final checks include Windows Vulkan flat, Windows Vulkan OpenXR, build without OpenXR loader target, and `git diff --check`.

## Resolved Planning Decisions
- Windows Dawn/Aurora Vulkan native hooks are mandatory for this implementation; `xrDev` now uses a patched vendored Dawn path for that requirement.
- OpenXR `VkImage`s should be directly wrapped/imported as Aurora render targets. Avoid a copy/intermediate path because the overhead is not acceptable for the primary VR design.

## References
- `docs/plans/openxr-launch-2026-05-11.md`
- `docs/plans/openxr-windows-carry-home-2026-05-11.md`
- `docs/plans/openxr-dawn-interop-decision-2026-05-11.md`
- `docs/reviews/complete-vr-implementation-plan-critique-2026-05-12.md`
- Khronos OpenXR 1.1.59 specification: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html
- `XrGraphicsBindingVulkanKHR`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingVulkanKHR.html
- `XrCompositionLayerProjection`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerProjection.html
- `XrCompositionLayerQuad`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html
