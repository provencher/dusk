# Complete VR Implementation Plan — 2026-05-12

## Goal
Complete Dusk's Vulkan OpenXR mode from the current dormant scaffold. Dusk keeps owning stereo camera/game rendering; Aurora owns OpenXR runtime/session state, Vulkan swapchains, render-target binding, composition layers, submission, and mirror presentation.

"Complete" means more than a stereo world preview: active XR must include stereo world rendering, per-eye world-projected UI, stereo-compatible post effects, native game UI, RmlUi/ImGui overlays, recentering, mirror behavior, fallback/status semantics, and validation coverage.

## User Decisions
- **Backend target:** Windows Vulkan OpenXR only. Do not preserve WebGPU compatibility, macOS support, or MoltenVK portability if that blocks OpenXR.
- **Ownership:** Dusk owns stereo camera/world redraws; Aurora owns XR runtime, swapchains, render targets, composition, and submission.
- **Completion bar:** full UI/post-effect/projection parity, not just playable stereo world rendering.

## Background
- Aurora XR is currently a status/lifecycle shim: `xr::begin_frame()` forces `frameState.shouldRender = false`, and `begin_eye()` / `begin_flat_ui()` return false in `extern/aurora/lib/xr/xr.cpp:112`, `extern/aurora/lib/xr/xr.cpp:148`, and `extern/aurora/lib/xr/xr.cpp:162`.
- Aurora already calls XR at useful frame seams: initialize at `extern/aurora/lib/aurora.cpp:150`, frame start at `extern/aurora/lib/aurora.cpp:208`, frame begin after `gfx::begin_frame()` at `extern/aurora/lib/aurora.cpp:260`, and after GPU submit at `extern/aurora/lib/aurora.cpp:337`.
- Dusk already snapshots Aurora XR state and per-eye pose/FOV in `src/dusk/vr/vr.cpp:244`, and `begin_eye_view()` composes OpenXR eye pose/FOV over the current `view_class` transactionally in `src/dusk/vr/vr.cpp:291`.
- `mDoGph_Painter()` already has the dormant stereo loop at `src/m_Do/m_Do_graphic.cpp:2624`: begin Aurora eye target, mutate the Dusk view, draw world 3D, draw world-projected 2D, restore view, end Aurora eye target.
- Aurora's smallest render-target seam is `extern/aurora/lib/gfx/common.cpp`: `set_efb_targets()` binds color/resolve/depth at `:158`, `gfx::begin_frame()` installs default EFB targets at `:674`, and `begin_offscreen()` already proves GX rendering can be redirected at `:376`.
- Dusk's current XR branch skips the mono screen-space post path when an eye was drawn. That skipped block starts at `src/m_Do/m_Do_graphic.cpp:2393`; native 2D/HUD/menu drawing is later around `src/m_Do/m_Do_graphic.cpp:2730`.
- Prior plans `docs/plans/openxr-launch-2026-05-11.md` and `docs/plans/openxr-windows-carry-home-2026-05-11.md` established the Dusk-side scaffold and identified Aurora's OpenXR swapchain/render-target bridge as the blocker.
- Khronos OpenXR 1.1.59 documents the relevant rules: Vulkan sessions use `XrGraphicsBindingVulkanKHR`, swapchain images must be acquired then waited before rendering, projection layers require the located view count, and quad layers are core 2D UI composition layers.

## Decision Gates
1. **Windows Vulkan target.** Scope the first complete implementation to Windows Vulkan OpenXR. Do not spend planning or implementation time on macOS, Metal, or MoltenVK support.
2. **OpenXR swapchain image path.** Direct OpenXR `VkImage` wrapping is the target. If the current Windows Vulkan backend cannot expose/import what OpenXR requires, do the needed Dawn/Aurora Vulkan integration work or a narrow patch/fork; do not choose a copy/intermediate path as the primary design, and do not start a new native Vulkan GX renderer in this plan.
3. **Flat UI ownership.** Use one flat UI swapchain image per XR frame. Dusk records native GX UI into it when `mDoGph_Painter()` runs; Aurora may then render RmlUi/ImGui into the same image in later render passes before submit. On prelaunch/RmlUi-only frames, Aurora acquires and owns the flat UI image itself.

## Approach
1. **Make Aurora XR real before touching more Dusk rendering.** Convert the probe/stubs into a persistent OpenXR runtime that can reach `READY`, poll events, begin/end frames, locate views, own swapchains, and expose valid `AuroraXRView` data.
2. **Parameterize Aurora's EFB target seam.** Add an internal `gfx` target override modeled after the offscreen path so `aurora_xr_begin_eye()` and `aurora_xr_begin_flat_ui()` can install eye/UI targets without changing Dusk's draw code shape.
3. **Use OpenXR projection + quad composition.** Submit stereo world as an `XrCompositionLayerProjection`; submit flat native UI/RmlUi/ImGui as a head-locked `XrCompositionLayerQuad` in view space.
4. **Keep Dusk's stereo camera path.** Dusk continues to redraw world sections per eye with `begin_eye_view()` / `end_eye_view()`, owns projection math and recentering, and audits world-attached UI for per-eye projection.
5. **Port post effects deliberately.** Every call in the current mono post block must be classified as per-eye world/post work, flat UI composition work, or intentionally disabled with a validation note. The final milestone must not skip the whole block in XR.
6. **Mirror is best-effort, never HMD-blocking.** Blit/sample eye 0 plus flat UI when possible; otherwise skip mirror preview without affecting HMD submission.

## Work Items

### 1. Create the real OpenXR runtime and readiness semantics
- Add an internal `OpenXRRuntime` under `extern/aurora/lib/xr/`, compiled only with `AURORA_HAS_OPENXR`.
- Own `XrInstance`, `XrSystemId`, `XrSession`, `LOCAL` and `VIEW` spaces, session state, frame state, located views, eye swapchains, and flat UI swapchain.
- Reuse the existing probe checks for loader/runtime, `XR_KHR_vulkan_enable2`, HMD system, and primary stereo view config, but stop returning `BLOCKED` after successful probing.
- Implement event polling, session transitions, `xrWaitFrame` / `xrBeginFrame`, `xrLocateViews`, and `xrEndFrame` with zero layers before adding rendering.
- Change Aurora and Dusk Required-mode checks so `AURORA_XR_READY` or `AURORA_XR_ACTIVE` satisfies startup; do not require active rendering during `aurora_initialize()`.
- Keep `DISABLED`, `UNAVAILABLE`, `BLOCKED`, and `LOST` as failure states for Required mode and clear fallback reasons for Optional mode.

Validation:
- XR disabled still launches flat.
- Optional without loader/runtime continues flat with a clear message.
- Required without loader/runtime fails clearly.
- Runtime present reaches `READY`, logs view count/recommended sizes, and updates `AuroraXRView` data before rendering is enabled.

### 2. Resolve the Vulkan/OpenXR image bridge
- Inspect `extern/aurora/lib/webgpu/gpu.cpp`, `extern/aurora/lib/webgpu/gpu.hpp`, and `extern/aurora/lib/dawn/BackendBinding.cpp` for the Windows Vulkan integration surface.
- Add Aurora-internal Vulkan device info access from the selected Vulkan backend: instance, physical device, device, queue family, queue index, queue, and API version.
- Ensure OpenXR-required Vulkan instance/device extensions are enabled before device creation or prove the existing device satisfies the runtime's requirements.
- Add an internal path to wrap/import `XrSwapchainImageVulkanKHR::image` directly as an Aurora renderable texture plus matching depth textures.
- Prefer one-sample XR eye/UI targets for the first complete milestone; keep flat-window MSAA separate.

Validation:
- Create a minimal OpenXR swapchain, enumerate images, directly wrap one image, clear/render into it through Aurora, submit, release it, and call `xrEndFrame` successfully.

### 3. Add `gfx` EFB target overrides
- Add an internal `EfbTargetSet` and begin/end override API in `extern/aurora/lib/gfx/common.hpp` / `.cpp`.
- Parameterize `set_efb_targets()` so a render pass can target normal EFB, an XR eye image, or the flat UI image.
- Start a distinct `RenderPass` for each eye/UI override; restore default EFB state after the override for mirror/fallback work.
- Reset viewport/scissor to the override target size when the override begins.

Validation:
- XR disabled path is visually unchanged.
- A non-XR test override can redirect GX drawing to an offscreen texture and then resume normal EFB drawing.

### 4. Implement functional eye targets
- Create one OpenXR color swapchain per view and a matching Aurora depth texture per eye.
- Implement `aurora_xr_begin_eye(index)` to acquire/wait the eye swapchain image and install the `gfx` target override.
- Implement `aurora_xr_end_eye()` to end the override and mark the eye rendered.
- Release swapchain images **after** WebGPU command submission and **before** `xrEndFrame`; OpenXR requires submitted commands, not GPU completion. Keep queue access externally synchronized around OpenXR calls that may touch the Vulkan queue.
- Set `frameState.shouldRender = true` only when the session is running, views are located, and all required swapchains are ready.
- Submit an OpenXR projection layer only when every expected eye rendered for the frame.

Validation:
- `src/m_Do/m_Do_graphic.cpp:2624` enters the stereo branch and renders both eyes.
- No partial one-eye projection layer is submitted.
- Optional mode falls back flat if eye target setup fails.

### 5. Make Aurora frame orchestration XR-safe
- Let HMD rendering continue when the mirror window is minimized or surface acquisition fails; flat-only mode can keep today's stricter behavior.
- Keep `xrBeginFrame()` / `xrEndFrame()` call order valid even when `shouldRender` is false.
- Keep Dusk rendering skipped if `gfx::begin_frame()` itself fails.
- Sequence XR frame end as: encode all render passes, submit WebGPU commands, call `gfx::after_submit()`, release acquired XR swapchain images, then call `xrEndFrame()`.

Validation:
- HMD frames continue with an unavailable mirror surface.
- XR session state transitions do not leave a frame begun without a matching end.
- Flat mode present behavior remains unchanged.

### 6. Add flat UI quad-layer rendering
- Create a flat UI OpenXR swapchain sized to Dusk's logical/native UI framebuffer.
- Implement `aurora_xr_begin_flat_ui()` / `aurora_xr_end_flat_ui()` with transparent clear and deferred release after command submission.
- Factor the native 2D/HUD/menu tail in `mDoGph_Painter()` into a callable flat UI section and draw it into the flat UI target after successful eye rendering.
- In `aurora::end_frame()`, render RmlUi/ImGui into the same acquired flat UI image in later render passes. If Dusk did not acquire a flat UI target that frame, Aurora acquires one for RmlUi/ImGui-only presentation.
- Submit the flat UI as a head-locked `XrCompositionLayerQuad` in `VIEW` space.

Validation:
- Game HUD/menu, prelaunch RmlUi, and ImGui overlays are visible in HMD.
- World-projected reticles are not duplicated in the flat UI layer.
- Flat UI acquisition/release happens once per frame regardless of whether Dusk, Aurora, or both rendered into it.

### 7. Split and port post effects for stereo parity
Depends on Work Item 6 for effects moved to flat composition.

- Split `draw_mono_screen_space_post_effects_section()` into stereo per-eye work and flat composition work.
- Run world/depth-dependent effects per eye: depth-of-field, bloom, filters, darkworld/invisible/refraction lists, late 3D lists, and particles that depend on the active camera.
- Eye-index stateful resources such as motion blur history, framebuffer captures, depth captures, and indirect-screen intermediates.
- Move screen-frame work such as trimming, color fade overlays, and ordinary 2D-game particles to the flat UI/composition path unless validation proves they are world-projected.
- For any effect intentionally disabled in VR for comfort/correctness, document the reason and test scene; do not use disablement as the default porting strategy.

Validation:
- XR no longer skips the entire mono post block.
- Representative water/refraction, bloom, darkworld/filter, fade, and late-particle scenes render correctly in both eyes.
- Flat mode order and visuals remain unchanged.

### 8. Complete projection, culling, and recenter validation
- Validate OpenXR FOV sign conventions and Dusk's frustum construction in `src/dusk/vr/vr.cpp`.
- Validate OpenXR-to-game coordinate conversion, IPD/head-translation scale, and recenter offset behavior.
- Replace the hardcoded XR culling widening in `src/d/d_camera.cpp` with `dusk::vr` helper(s) that can compute a conservative union from current eye FOVs and a head-motion margin.
- Keep F8 recenter as an app-space yaw/lateral offset; do not mutate game camera/orbit state.

Validation:
- Head rotation does not cull side-view geometry prematurely.
- Head translation/IPD scale is plausible.
- F8 recenters with valid tracking, reports inactive/no-tracking states, and preserves game camera state.

### 9. Audit all world-projected UI
Depends on functional eye targets and flat UI so call sites can be tested in the correct destination.

- Search `mDoLib_project(`, `mDoLib_pos2camera(`, `dComIfGd_set2DXlu(`, and cursor/marker packets that store world positions.
- Convert world-attached markers to `dComIfGd_setWorldProjected2DXlu()` and recompute projection in draw-time XR context.
- Leave pure HUD/menu elements in the flat UI layer.
- Document ambiguous call sites in the validation matrix until tested.

Validation:
- Existing player sight and boomerang cursor conversions still attach correctly in stereo.
- Newly converted markers project independently per eye.
- Menus/HUD do not become erroneous stereo world markers.

### 10. Implement mirror preview
- Blit or sample eye 0 plus flat UI into the mirror surface when XR rendered and the surface is presentable.
- If XR images cannot be sampled directly for mirror preview, log once and skip mirror preview rather than adding a copy-only mirror path.
- Mirror failure must never block HMD submission.

Validation:
- Mirror window shows a best-effort left-eye composited preview when possible.
- Minimized/unpresentable mirror window does not stop active XR.
- XR inactive mirror behavior remains unchanged.

### 11. Update docs, settings text, and final checks
- Update `docs/building.md` to replace stale "active rendering blocked" language with Vulkan/OpenXR prerequisites, Optional/Required behavior, `READY` vs `ACTIVE`, F8 recenter, mirror behavior, and known VR effect deviations.
- Update `src/dusk/ui/settings.cpp` OpenXR Mode help text to match the implemented behavior.
- Run the per-work-item validation above plus final build checks: Windows Vulkan flat, Windows Vulkan OpenXR, build without OpenXR loader target, and `git diff --check`.

## Resolved Planning Decisions
- Windows Dawn/Aurora Vulkan native hooks are mandatory for this implementation; do the required integration work rather than narrowing the goal.
- OpenXR `VkImage`s should be directly wrapped/imported as Aurora render targets. Avoid a copy/intermediate path because the overhead is not acceptable for the primary VR design.

## References
- `docs/plans/openxr-launch-2026-05-11.md`
- `docs/plans/openxr-windows-carry-home-2026-05-11.md`
- Khronos OpenXR 1.1.59 specification: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html
- `XrGraphicsBindingVulkanKHR`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingVulkanKHR.html
- `XrCompositionLayerProjection`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerProjection.html
- `XrCompositionLayerQuad`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html
