# VR World-Projected UI Audit - 2026-05-12

This audit tracks the first pass over `mDoLib_project()`, `mDoLib_pos2camera()`,
`dComIfGd_set2DXlu()`, and cursor/marker draw packets for the OpenXR path.

## Converted / Active

- Player sight reticle: `src/d/actor/d_a_player.cpp` registers through
  `dComIfGd_setWorldProjected2DXlu()` and recomputes `mDoLib_project()` during
  packet draw when VR is active.
- Boomerang lock cursors: `src/d/actor/d_a_boomerang.cpp` registers through
  `dComIfGd_setWorldProjected2DXlu()` and recomputes each lock position during
  packet draw when VR is active.
- `dComIfGd_setWorldProjected2DXlu()` queues packets into
  `dusk::vr::world_projected_queue()` while VR is active. The queue is drawn once
  per XR eye after world rendering and skipped from the flat UI layer to avoid
  duplicate HUD projection.

## Deliberate Flat UI / Screen-Space

- `dComIfGd_set2DXlu()` remains the correct path for ordinary HUD, menus, fades,
  snapshots, map/wipe overlays, and message UI. These now render to the head-locked
  flat UI quad when stereo world rendering succeeds.
- The Wii/Shield pointing cursor packet in `src/d/d_com_inf_game.cpp` is a
  screen-space cursor/navi effect and remains flat UI.
- Balloon, shop, timer, message, and snapshot projection paths such as
  `src/d/actor/d_a_balloon_2D.cpp`, `src/d/d_shop_system.cpp`,
  `src/d/d_timer.cpp`, `src/d/d_ovlp_fade2.cpp`, and `src/d/d_ovlp_fade3.cpp`
  feed flat score/HUD/menu overlays. They should stay in the flat UI layer.
- Message and talk screen projection calls in `src/d/d_msg_object.cpp`,
  `src/d/d_msg_scrn_item.cpp`, and `src/d/d_msg_scrn_talk.cpp` compute screen
  placement for flat message UI. They should stay flat unless a tested scene proves
  a specific marker is world-attached.
- Weather, sky, lens, moon, sun, star, rain, and debug-light projection calls in
  `src/d/d_kankyo*.cpp` feed effect placement rather than 2D cursor packets. These
  belong to the post-effect/effect parity pass, not the flat UI queue.
- Attention/lock-on notice cursors in `src/d/d_attention.cpp` are not deferred
  2D projected packets. They render as `3Dlast` models using the active view
  matrix/FOV, so the existing XR per-eye world draw path covers them.

## Ambiguous / Needs Scene Validation

- Insect and small-object projection helpers in files such as
  `src/d/d_insect.cpp`, `src/d/actor/d_a_obj_ari.cpp`,
  `src/d/actor/d_a_obj_cho.cpp`, `src/d/actor/d_a_obj_dan.cpp`,
  `src/d/actor/d_a_obj_kabuto.cpp`, `src/d/actor/d_a_obj_kamakiri.cpp`,
  `src/d/actor/d_a_obj_katatsumuri.cpp`, `src/d/actor/d_a_obj_kuwagata.cpp`,
  `src/d/actor/d_a_obj_ten.cpp`, and `src/d/actor/d_a_obj_tombo.cpp` appear to
  calculate projected positions for actor behavior or effect scaling. They are not
  registered as draw-list packets through `dComIfGd_set2DXlu()` in this pass and
  need scene validation before conversion.
- Camera/debug projection calls in `src/d/d_camera.cpp`, `src/d/d_ev_camera.cpp`,
  and enemy/object helper files are used for gameplay camera math, debug visuals,
  or effects. Leave them unchanged until a visible stereo artifact is found.
- Enemy view-area checks such as `src/d/actor/d_a_e_fk.cpp`,
  `src/d/actor/d_a_e_fs.cpp`, `src/d/actor/d_a_e_sm.cpp`, and `src/d/actor/d_a_cow.cpp`
  use `mDoLib_project()` for gameplay/control decisions rather than draw-list
  registration. They should not be moved into the world-projected UI queue.
- Map marker references in ImGui event flag labels are data labels, not runtime
  2D world marker draw packets.

## Implementation Note

`WorldProjectedItem::modelFromWorld` was removed because no caller supplied a
non-identity model matrix and the draw loop only invokes each packet's `draw()`.
World-attached packets that need per-eye placement should recompute their own
projection during `draw()` using their stored world position, matching the player
sight and boomerang cursor conversions.
