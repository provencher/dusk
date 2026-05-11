# OpenXR Launch: Plan

## Goal
Launch Dusk through OpenXR with head tracking only. Controller tracking must not be required; HMD pose should layer over the existing game/third-party camera as a parent transform; screen-space UI should remain usable in VR; world-targeted UI such as lock-on/reticles must stay attached to targets; and recentering should reset the app's VR origin without mutating existing orbit/free-camera state.

## Background
- Aurora owns platform startup and frame bracketing. Dusk fills `AuroraConfig` and calls `aurora_initialize()` in `src/m_Do/m_Do_main.cpp:705`; prelaunch and main loops call `aurora_begin_frame()` at `src/m_Do/m_Do_main.cpp:203` and `src/m_Do/m_Do_main.cpp:289`, with main-frame submission ending at `src/m_Do/m_Do_main.cpp:340`.
- The render-facing camera is `view_class`, which owns `projMtx`, `viewMtx`, `invViewMtx`, `projViewMtx`, and `viewMtxNoTrans` in `include/f_op/f_op_view.h:42`.
- Game camera state is copied into framework camera state in `store()` and published through `view_setup()` in `src/d/d_camera.cpp:11072` and `src/d/d_camera.cpp:11107`; rendering later uploads `camera_p->view.projMtx` in `src/m_Do/m_Do_graphic.cpp:2204`.
- Existing camera reset/orbit behavior lives in `dCamera_c::Reset()` at `src/d/d_camera.cpp:10733` and the debug/free camera path. VR recentering should be separate app-space offset state layered above that camera, not a rewrite of `mCenter`, `mEye`, or `mViewCache`.
- `mDoGph_Painter()` starts at `src/m_Do/m_Do_graphic.cpp:2065` and runs for hundreds of lines through world drawing, 2D draw lists, post effects, and ImGui `PostDraw()`. XR should refactor this locally before adding stereo behavior.
- UI has three strata: RmlUi documents updated in `src/dusk/ui/ui.cpp:217`, ImGui overlays around the game render pass, and game-native J2D/GX draw lists. Targeting UI already starts from world positions and projects through `mDoLib_project()` in `src/m_Do/m_Do_lib.cpp:66`, e.g. `daPy_sightPacket_c::setSight()` in `src/d/actor/d_a_player.cpp:421`.
- No prior OpenXR implementation or plan exists in the repo. Adjacent prior work includes mouse gyro, debug/fly camera, orbital camera fixes, and RmlUi overlay/layout changes.
- OpenXR head tracking works without controller actions. Use `LOCAL` as the default app reference space, `VIEW` for head-locked UI, locate views each frame using predicted display time, and implement recentering with an app-owned origin offset because core OpenXR has no universal “recenter now” API. See the [OpenXR 1.1 spec](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html), [`XR_REFERENCE_SPACE_TYPE_VIEW`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_REFERENCE_SPACE_TYPE_VIEW.html), and Microsoft’s [OpenXR recentering guidance](https://learn.microsoft.com/en-us/windows/mixed-reality/develop/native/openxr-cookbook).

## Approach
1. **Target desktop Vulkan OpenXR first.** OpenXR is a presentation/session mode layered over Vulkan, not a new `"openxr"` graphics backend string. `backend.xrMode` is the source of truth for XR; when it is enabled, startup forces or validates Vulkan for this first launch target.
2. **Put OpenXR lifecycle in Aurora.** Aurora owns instance/session/reference spaces, Vulkan graphics binding, swapchains, `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`, `xrLocateViews`, eye render targets, and any UI layer targets. Dusk requests XR and consumes per-frame eye pose/FOV data.
3. **Keep projection matrix construction in Dusk.** Aurora should expose per-eye pose, FOV, recommended size, and validity. Dusk builds per-eye projection matrices using the existing `view_class` near/far policy so camera clipping remains tied to Dusk's render state.
4. **Compose XR eye pose over the existing camera at render time.** Preserve `dCamera_c::Run()`, `freeCamera()`, debug fly camera, and `Reset()` semantics. During XR rendering, save selected `view_class` fields, write temporary per-eye matrices, draw that eye, then restore the original view.
5. **Keep launch UI simple.** RmlUi, ImGui, and ordinary HUD/menu J2D remain a flat head-locked surface for launch. Lazy-follow and richer UI motion modes are follow-up work after the stereo render and targeting seams are stable.
6. **Split world-targeted UI from flat screen UI.** Lock-on/reticle-style packets that project world positions move to a Dusk-owned world-projected queue so they project during each eye draw against that eye's `projViewMtx`. Menus and ordinary HUD stay in the flat UI pass.
7. **Recenter by updating app-space offset only.** Recenter uses the latest valid HMD pose to remove lateral offset and yaw from Dusk's XR origin. It does not mutate game camera/orbit state.

## Work Items
1. **Answer Aurora prerequisites first**
   - Locate the Aurora headers/source defining `AuroraConfig`, `aurora_initialize()`, `aurora_begin_frame()`, and render-target handling.
   - Confirm whether Aurora can render RmlUi/ImGui/native flat UI into a separate OpenXR quad-layer target. If not, launch fallback is to draw flat UI into both eyes while still rendering world-targeted UI per eye.
   - Confirm render threading. This plan assumes `view_class` overrides are stack-scoped on the render thread inside `mDoGph_Painter()` and are not observed by concurrent update code.

2. **Aurora XR API boundary**
   - Add no-op XR fields/APIs first: `enableOpenXR`, optional `requireOpenXR`, frame state, per-eye begin/end, UI-target begin/end, and accessors for active/should-render/view count/eye pose/FOV/recommended size.
   - Replace no-ops with OpenXR/Vulkan implementation: `LOCAL` app space, optional `VIEW` space for flat UI, no required action sets/controllers, frame wait/begin/end, view location, swapchain acquire/release, and graceful flat fallback when XR is requested but unavailable.

3. **Settings and startup policy**
   - Add persistent `backend.xrMode` and a launch-only flat UI mode setting if needed; do not add an `"openxr"` backend ID.
   - In `src/m_Do/m_Do_main.cpp:705`, when `backend.xrMode` requests OpenXR, validate Vulkan availability, set Aurora XR flags, and set `desiredBackend` to Vulkan. If Vulkan or XR initialization fails and XR is not required, fall back to existing flat startup.
   - Add prelaunch/settings UI controls in `src/dusk/ui/settings.cpp`; enabling XR should make the Vulkan coupling explicit and communicate restart requirements.

4. **Dusk VR service**
   - Add `include/dusk/vr/vr.hpp` and `src/dusk/vr/vr.cpp`.
   - Track requested/active state, eye count, per-eye raw pose/FOV, per-eye composed matrices, current recenter offset, pending recenter, and a world-projected 2D packet queue.
   - Call `dusk::vr::begin_frame()` after successful `aurora_begin_frame()` in both the prelaunch loop (`src/m_Do/m_Do_main.cpp:203`) and main loop (`src/m_Do/m_Do_main.cpp:289`).

5. **Camera composition and culling**
   - Implement `begin_eye_view(view_class&, eyeIndex, token)` / `end_eye_view(token)` in `dusk::vr`.
   - The token saves and restores `lookat`, `bank`, `fovy`, `aspect`, `projMtx`, `viewMtx`, `invViewMtx`, `projViewMtx`, and `viewMtxNoTrans`. Its lifetime is stack-scoped to one eye draw inside `mDoGph_Painter()`.
   - Use saved `view.invViewMtx` as the base game camera transform, compose the recentered eye pose in game units, build the per-eye projection from Aurora FOV plus `view.near_` / `view.far_`, update `lookat` for consumers, and recompute inverse/no-translation/proj-view matrices.
   - Expand camera culling while XR is active in `view_setup()` (`src/d/d_camera.cpp:11072`) with a conservative launch FOV so head rotation does not cull side-view geometry.

6. **Local render-pass split**
   - Before stereo, partition `mDoGph_Painter()` (`src/m_Do/m_Do_graphic.cpp:2065`) into named sections without changing mono order:
     - frame/ImGui prelude
     - active camera/window/viewport setup
     - world/3D draw lists and particles
     - mono screen-space post effects
     - ordinary native 2D HUD/menu lists
     - ImGui/RmlUi flat UI tail and `endRender()`
   - Then add the XR path: run the world/3D section once per eye inside Aurora eye targets with `begin_eye_view()` / `end_eye_view()`, draw world-projected packets per eye, and draw ordinary flat UI once through Aurora's UI target or the fallback chosen in Work Item 1.
   - Skip mono screen-space post effects in XR eye passes for launch when they assume a single flat framebuffer.

7. **World-projected targeting UI**
   - Add a PC/Dusk helper near existing 2D draw-list helpers in `include/d/d_com_inf_game.h`, e.g. `dComIfGd_setWorldProjected2DXlu(dDlst_base_c*)`.
   - The helper stores only actor-owned `dDlst_base_c*` packets in `dusk::vr` while XR is active; otherwise it forwards to `dComIfGd_set2DXlu(...)`. Existing layer ordering should match the current `2DXlu` position, but projection happens during each eye's draw.
   - Convert `daPy_sightPacket_c` (`src/d/actor/d_a_player.cpp:421`) and boomerang lock cursor code first: queue through the helper and perform `mDoLib_project()` during `draw()` when XR is active.
   - Audit `mDoLib_project(`, `mDoLib_pos2camera(`, and `dComIfGd_set2DXlu(` call sites. Convert only world-position reticles/markers; leave menus and ordinary HUD in flat UI.

8. **Recenter UX**
   - Add one launch trigger first: `F8` in the existing global ImGui input path.
   - On success, update the Dusk XR origin offset from the latest valid HMD pose. On failure, log whether XR is inactive or tracking is not valid yet.
   - Validate that recentering fixes lateral physical drift without changing orbit/free-camera angles or `dCamera_c::Reset()` behavior. A RmlUi button/toast can follow after the core path works.

9. **Documentation and validation**
   - Update `docs/building.md` with OpenXR runtime/Vulkan prerequisites and config notes.
   - Minimum launch checks: XR off remains unchanged, XR requested without a runtime falls back cleanly, XR active Vulkan launch renders both eyes, F8 recenter works, and the first converted targeting reticles stay attached in stereo.

## Open Questions
- Which OpenXR loader/header dependency strategy should Aurora use: system loader, vendored headers/loader, or platform-specific dependency setup?
- After the first targeting conversions, which additional projected UI call sites are world markers versus ordinary HUD/menu elements? This should be answered by the Work Item 7 audit, not guessed up front.
