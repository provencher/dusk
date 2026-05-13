# VR Post-Effects Audit - 2026-05-12

This audit classifies the screen-space/post block in
`src/m_Do/m_Do_graphic.cpp` for the Windows Vulkan OpenXR path. It is a code
classification, not visual validation. The physical validation matrix still
owns headset comfort and representative scene results.

## Execution Model

- `draw_world_3d_section()` renders the ordinary world lists for each XR eye.
- `draw_world_projected_2d_section()` renders the world-projected queue for
  each XR eye after the world pass.
- `draw_mono_screen_space_post_effects_section(false)` runs for each acquired
  XR eye. Passing `false` keeps the flat composition tail out of the eye
  target.
- `draw_flat_composition_post_effects_section()` runs inside the OpenXR flat UI
  target after stereo world rendering, before the native 2D/HUD/menu section.
- The non-XR path still calls `draw_mono_screen_space_post_effects_section(true)`
  so flat rendering keeps the original ordering.

## Per-Eye World / Depth Work

These calls are intentionally retained in the per-eye post section because they
use the active camera, depth, framebuffer captures, world lists, or
camera-dependent particle projection:

| Call / Group | Classification | Validation Focus |
| --- | --- | --- |
| `motionBlure(&camera_p->view)` | Per-eye history/camera effect | Motion blur scenes; no cross-eye smear. |
| `drawDepth2(&camera_p->view, view_port, ...)` | Per-eye depth-of-field pass | Focus/depth scenes; depth consistency. |
| `dComIfGd_drawOpaListInvisible()` / `dComIfGd_drawXluListInvisible()` | Per-eye world/depth lists | Darkworld/invisible actors; stereo alignment. |
| `dComIfGp_particle_drawFogPri4()` / `dComIfGp_particle_drawProjection()` | Per-eye projected particles | Projection-heavy effects; per-eye attachment. |
| `dComIfGd_drawListZxlu()` | Per-eye translucent Z-update list | Translucent geometry; eye consistency. |
| `dComIfGd_drawOpaListFilter()` | Per-eye filter world list | Filter/darkworld scenes; depth consistency. |
| `dComIfGp_particle_drawFogPri1()` / `dComIfGp_particle_draw()` / `dComIfGp_particle_drawFogPri2()` / `dComIfGp_particle_drawFog()` / `dComIfGp_particle_drawFogPri3()` / `dComIfGp_particle_drawP1()` / `dComIfGp_particle_drawDarkworld()` | Per-eye camera-dependent particles | Late particle scenes; no flat duplication. |
| `retry_captue_frame(&camera_p->view, view_port, ...)` | Per-eye framebuffer/depth capture | Water, bloom, motion history, repeated capture. |
| `dComIfGp_particle_drawScreen()` | Per-eye projected/screen-effect particle path until scene evidence proves otherwise | Projection/full-screen particle scenes. |
| `dComIfGd_drawIndScreen()` | Per-eye indirect-screen/refraction work | Water/refraction scenes. |
| `dComIfGd_drawXluList2DScreen()` | Per-eye full-projection screen list | Full-projection effects; stereo placement. |
| Conditional `F_SP124` and water/bloom `retry_captue_frame(...)` calls | Per-eye repeated captures | Stage-specific capture and water/bloom scenes. |
| `mDoGph_gInf_c::getBloom()->draw()` | Per-eye bloom/filter composition | Bloom and filter-heavy scenes. |
| `dComIfGd_drawOpaList3Dlast()` | Per-eye late 3D list | Late 3D actors/effects. |

## Flat Composition Work

These calls are moved out of XR eye targets and drawn once into the OpenXR flat
UI target when stereo rendering succeeds:

| Call / Group | Classification | Validation Focus |
| --- | --- | --- |
| `dComIfGp_particle_draw2Dgame()` | Flat composition | 2D-game particles are head-locked and not doubled per eye. |
| `trimming(&camera->view, viewport)` | Flat composition | Transition/trimming overlay ordering. |
| Normal `mDoGph_gInf_c::calcFade()` path | Flat composition | Fade overlays appear once and in front of world. |
| Special `F_SP127` / `0x80` fade fallback | Flat composition | Special fade remains visible even when normal 2D draw is disabled. |
| `dDlst_list_c::calcWipe()` | Existing 2D/HUD phase | Wipes remain ordered with flat UI and native 2D. |
| Native `dComIfGp_particle_draw2Dback()`, menu back/fore, 2D fore, HUD/menu lists | OpenXR flat UI target | HUD/menu readability and no stereo-world placement. |

## Stateful Resources

Framebuffer and depth capture scratch resources used by motion blur,
depth-of-field, bloom, indirect-screen, and repeated capture paths are selected
per active XR eye. That prevents the second eye from overwriting the first eye's
capture intermediates during the same frame. Any additional stateful history
found during representative scene testing should be added here before the
implementation is considered complete.

## Not Yet Proven

- Physical headset comfort and visual correctness for the per-eye post section.
- Scene-specific correctness for water/refraction, bloom, darkworld/filter,
  motion blur, late particles, and full-projection screen effects.
- Whether `dComIfGp_particle_drawScreen()` has any scene-specific cases that
  should instead become flat composition. It remains per-eye until a tested
  scene proves otherwise.
