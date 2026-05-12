# Critique — Complete VR Implementation Plan (2026-05-12)

Scope: focused critique of `docs/plans/complete-vr-implementation-2026-05-12.md`. Plan line refs spot-checked against `extern/aurora/lib/xr/xr.cpp` and look accurate.

## 1. Top 3 under-specified seams

1. **WI 3 — Vulkan device access / Dawn integration.** This is correctly called out as the hard gate, but the spec is one paragraph of "add Aurora-internal Vulkan device info access" with no file reference for the Dawn glue. Critical unanswered details: which platform/build configurations actually link a Vulkan-capable Dawn (macOS likely defaults to Metal); how the runtime forces Vulkan selection; what surface a "narrow Dawn patch/fork" touches; whether OpenXR-supplied `VkImage` objects are wrappable as Aurora textures or must be copied. The phrase "bypass the incompatible layer for Vulkan-only XR" is doing a lot of work for a single sentence.

2. **WI 7 — flat UI timing and ownership.** Plan says native game UI is drawn from `mDoGph_Painter()` while RmlUi/ImGui are drawn from `aurora::end_frame()` into the same flat UI swapchain. The handoff is unspecified: who acquires/releases the swapchain image, what happens on frames where the painter does not run, ordering vs. `xrEndFrame()`, and whether Dusk and Aurora can both submit to the same Aurora render pass in one frame.

3. **WI 5 — swapchain release timing.** "Release swapchain images only after GPU submit" is restated in WI 6 but never grounds out: Dawn submission is not the same as GPU completion, and OpenXR requires release before `xrEndFrame()`. Needs a concrete fence/poll strategy or an explicit statement that Dawn's submit ordering is sufficient.

## 2. Contradictions / missing dependencies

- WI 3's decision gate has no fallback work item. If Dawn cannot expose Vulkan handles, the plan says "patch/fork or bypass" — that bypass is plausibly the size of this entire plan and is not budgeted.
- WI 8 (post split) depends on WI 7 (flat UI target exists) for the "move to composition path" bullet; not stated.
- WI 10 (world-projected UI audit) presumes WI 5 + WI 7 are functional; not stated.
- WI 1 references `AURORA_XR_READY` as if it exists; if the current enum only has `ACTIVE/BLOCKED/...`, the "status semantics" change is bigger than one work item.

## 3. Over-planning — cut or simplify

- **WI 1** is one condition flip plus a comment edit. Fold into WI 2; delete the four-bullet validation list.
- **WI 12's 14-item validation matrix** duplicates the per-WI validation sections. Keep one; the per-WI lists are more useful.
- **WI 11 (mirror)** is correctly framed as best-effort, then over-specified. Collapse to: "blit eye 0 to mirror surface when sampleable; skip silently otherwise."
- **"Open Questions: None"** is inaccurate — the Dawn gate is itself an open question. Either delete the section or move WI 3's gate into it.
- **References** — five Khronos URLs is more than needed; two (projection layer, Vulkan binding) suffice.

## 4. Questions that would change implementation order

1. **Does the current Dawn build ship a Vulkan backend on the target platforms?** If macOS is Metal/MoltenVK only, XR is Windows-Linux first and WI 3's scope shrinks dramatically — affects whether WI 2 is meaningful before a platform decision.
2. **Are OpenXR `VkImage`s directly usable, or copy-required?** Copy-required inverts WI 5 → WI 11: mirror becomes a free byproduct of the copy rather than a separate item.
3. **Does `mDoGph_Painter()` run during prelaunch/RmlUi-only frames?** If not, WI 7 must drive flat UI from Aurora alone on those frames, which is a different mechanism than the painter tail factor-out.
4. **Does any current post effect already maintain per-eye history?** If none do, the eye-indexing scaffolding in WI 8 is greenfield; if one does (motion blur), port it first as the canary.
5. **What is today's Required-mode failure point?** If it is only a single `BLOCKED` check, WI 1 collapses into WI 2 and removes an ordering hop.
