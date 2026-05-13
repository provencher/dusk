# VR Physical Validation Matrix - 2026-05-12

This matrix tracks validation that cannot be fully proven by the Monado
simulated-HMD/null-compositor tests. The automated tests prove OpenXR target
lifetime, Dawn interop, same-frame eye plus flat-UI acquisition, queue-idle
release ordering, simulated mirror-surface loss, simulated head-motion
plumbing, and OpenXR view-geometry sanity. They do not prove headset comfort,
compositor display behavior, or game-scene visual parity.

## Build Under Test

- Configure preset: `windows-msvc-dawn-vendor-openxr`
- Build command:
  `cmake --build --preset windows-msvc-dawn-vendor-openxr --target dusk_openxr_probe dusk`
- Runtime command shape:
  `.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=<0|1|2> .\game.ciso`
- OpenXR mode values:
  - `0`: Disabled flat startup.
  - `1`: Optional XR startup with flat fallback.
  - `2`: Required XR startup, fail-fast if OpenXR cannot initialize.
- Mirror modes:
  - Default: eye 0 plus flat UI when available.
  - Debug: `AURORA_XR_MIRROR_SBS=1`.

## Automated Baseline

Latest local automated baseline:

- 2026-05-12: `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure --timeout 120`
  passed 6/6 against Monado simulated HMD/null compositor. Passing tests:
  `dusk_openxr_probe_smoke`, `dusk_openxr_probe_invalid_runtime`,
  `dusk_openxr_probe_dawn_interop`, `dusk_openxr_probe_live_clear`,
  `dusk_openxr_probe_headless_mirror`, and
  `dusk_openxr_probe_mock_hmd_motion`.
- 2026-05-12 physical-testing prep rerun:
  `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure`
  passed 6/6 through the VS 2026 developer environment after an initial
  transient SteamVR-connected probe crash during the first helper invocation.

Record the exact output date and summary again before physical testing:

- `ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure`
  - Expected: 6/6 pass.
  - Required gates:
    - `xr_dawn_interop=ready`
    - `xr_proof_gate=cleared`
    - `xr_eye_target_gate=submitted`
    - `xr_flat_ui_target_gate=submitted`
    - `xr_live_gate=cleared_submitted_flat_ui_submitted`
    - `xr_vulkan_extension_gate=validated`
    - `xr_headless_mirror_gate=continued`
    - `xr_head_motion_gate=validated`
    - `xr_view_geometry_gate=validated`
- `ctest --test-dir build\windows-msvc-relwithdebinfo -L openxr --output-on-failure`
  - Expected: 3/3 pass for the prebuilt-Dawn unavailable/missing-interop path.
- `cmake --build --preset windows-msvc-no-openxr --target dusk`
  - Expected: compile success with OpenXR SDK/loader support disabled.

The patched-Dawn simulated-HMD path covers target submission, flat-UI
submission, simulated mirror-surface loss, head-motion sampling, and OpenXR
view-geometry sanity. It still does not replace the physical headset rows
below.

## Runtime Matrix

## Minimum Acceptance Sequence

Run these in order. Stop on the first failure, record evidence in the runtime
matrix, and do not mark the implementation complete until the failure is fixed
or recorded as an intentional, scene-tested deviation.

1. Automated baseline: patched-Dawn OpenXR CTest 6/6, default OpenXR CTest 3/3,
   and no-OpenXR build success.
2. Startup semantics: `Disabled`, `Optional` fallback, and `Required` failure
   without a usable runtime.
3. Physical runtime activation: patched-Dawn build reaches OpenXR `READY` then
   `ACTIVE` on SteamVR, Meta, or WMR with direct Dawn interop ready.
4. Core HMD presentation: stereo world, head translation/IPD plausibility,
   side-view culling, and `F8` recenter.
5. UI presentation: native HUD/menu, RmlUi, ImGui, and default/SBS mirror.
6. Representative scenes: water/refraction, bloom/filter/darkworld,
   motion-blur/history, fade/wipe/trimming, late particles, and world-projected
   reticles.

## Evidence Capture Template

For each physical runtime row, record:

- Date, runtime name/version, headset model, GPU/driver version, and Dusk commit.
- Build preset and executable path.
- `backend.xrMode`, mirror mode, and any relevant environment variables.
- Log excerpts containing:
  - `OpenXR ready:` or the required-XR failure message.
  - `Dawn interop:` text.
  - Runtime Vulkan instance/device extension diagnostics.
  - `OpenXR recenter updated from latest HMD pose`, or the inactive/no-tracking
    recenter warning for negative cases.
- HMD observation notes for stereo stability, comfort, UI readability, and
  whether the result matches mirror output.
- Mirror screenshot path when the row involves mirror behavior or UI layout.
  Use `validation-screenshots/` for local captures, named with the same
  date/runtime/headset/mode/scene prefix as the transcript.

Suggested commands:

```powershell
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build --preset windows-msvc-dawn-vendor-openxr --target dusk_openxr_probe dusk"
cmd.exe /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=x64 && ctest --test-dir build\windows-msvc-dawn-vendor-openxr -L openxr --output-on-failure"
.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=0 .\game.ciso
.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=1 .\game.ciso
.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=2 .\game.ciso
```

The same baseline-and-launch flow can be driven by
`tools/vr_physical_validation.ps1`, which writes a timestamped transcript under
`validation-logs/` and restores temporary OpenXR-related environment variables
after launch:

```powershell
.\tools\vr_physical_validation.ps1 -Mode Required -Label steamvr-headset-ordon
.\tools\vr_physical_validation.ps1 -Mode Optional -Label no-runtime-fallback -MissingRuntime
.\tools\vr_physical_validation.ps1 -Mode Required -Label steamvr-headset-sbs -SbsMirror -SkipBaseline
```

For evidence capture, wrap each manual launch in a transcript named after the
runtime, headset, mode, and scene being tested:
`validation-logs/` and `validation-screenshots/` are ignored by git, so copy
or summarize any evidence that should be preserved in the matrix before
cleaning the local workspace.

```powershell
New-Item -ItemType Directory -Force .\validation-logs | Out-Null
New-Item -ItemType Directory -Force .\validation-screenshots | Out-Null
Start-Transcript -Path .\validation-logs\2026-05-12-steamvr-headset-required-ordon.log
.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=2 .\game.ciso
Stop-Transcript
```

For required-XR negative testing, set a missing runtime manifest before launch:

```powershell
$env:XR_RUNTIME_JSON = "$PWD\build\windows-msvc-dawn-vendor-openxr\missing-openxr-runtime.json"
.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=2 .\game.ciso
Remove-Item Env:\XR_RUNTIME_JSON
```

For the side-by-side mirror debug row, enable the mirror mode environment flag
only for that launch:

```powershell
$env:AURORA_XR_MIRROR_SBS = "1"
.\build\windows-msvc-dawn-vendor-openxr\dusk.exe --backend vulkan --cvar backend.xrMode=2 .\game.ciso
Remove-Item Env:\AURORA_XR_MIRROR_SBS
```

| Area | Runtime / Scene | Expected Result | Evidence | Status |
| --- | --- | --- | --- | --- |
| Startup disabled baseline | No active headset/runtime, `backend.xrMode=Disabled` | Dusk starts flat; rendering and input match normal non-XR behavior. | Log excerpt, flat gameplay/menu observation. | Not run |
| Startup optional fallback | No active headset/runtime, `backend.xrMode=Optional` | Dusk starts flat and logs OpenXR unavailable/blocked without fatal exit. | Log excerpt, runtime name. | Not run |
| Startup required failure | No active headset/runtime, `backend.xrMode=Required` | Dusk fails startup with required-XR error. | Log excerpt. | Not run |
| Runtime availability | System active runtime: Virtual Desktop OpenXR, SteamVR running with connected headset | If this is the selected runtime, it should expose a headset system or optional/required startup should report unavailability cleanly. | `reg query HKLM\SOFTWARE\Khronos\OpenXR\1 /v ActiveRuntime` reported `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`; default `dusk_openxr_probe.exe --allow-unavailable --require-dawn-interop` reported `xr_status=unavailable`, `XR_ERROR_FORM_FACTOR_UNAVAILABLE`, `xr_dawn_interop=ready`, and `xr_view_count=0`. | Blocked by active-runtime selection/no HMD system |
| Runtime availability | Explicit SteamVR runtime with physical headset | OpenXR reaches `READY` then `ACTIVE`; no copy/intermediate image path is used. | `XR_RUNTIME_JSON=C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json` probe reported `xr_status=blocked`, `xr_view_count=2`, both views `2688x2880`, `xr_dawn_interop=ready`, and `xr_proof_gate=proof_blocked`: Dawn Vulkan device API version `1.4.329` is above runtime maximum `1.2.0`. Required launch transcript: `validation-logs\20260512-194010-steamvr-headset-required-blocked-capture-required-defaultmirror-runtime.log`. | Failed: Dawn/OpenXR Vulkan API compatibility blocker |
| Stereo world | Ordon / open outdoor area | Both eyes show stable stereo world rendering with plausible IPD and head translation scale. | HMD observation, mirror screenshot optional. | Not run |
| Head rotation culling | Open area with peripheral geometry | Head rotation does not prematurely cull side-view geometry. | HMD observation; note camera/FOV settings. | Not run |
| Recenter | Same scene, physical lateral/yaw drift then `F8` | App-space origin recenters without mutating game camera/orbit/free-camera state. | Log excerpt and HMD observation. | Not run |
| Native HUD/menu | Gameplay HUD, pause/menu screens | HUD/menu render in head-locked flat UI, are readable, and do not appear as stereo world geometry. | HMD observation. | Not run |
| RmlUi prelaunch/settings | Prelaunch and settings UI with XR active | RmlUi appears in HMD flat UI and remains functional on mirror. | HMD and mirror observation. | Not run |
| ImGui overlays | Console/process/camera overlays enabled | ImGui appears in HMD flat UI and default mirror; flat mode remains unchanged. | HMD and mirror observation. | Not run |
| World-projected reticles | Player sight and boomerang locks | Reticles attach to world targets per eye and are not duplicated in flat UI. | HMD observation. | Not run |
| Water/refraction | Water or underwater/refraction-heavy scene | Refraction/water effects render coherently per eye or are documented with a comfort/correctness reason. | Scene name, HMD observation. | Not run |
| Bloom/filter/darkworld | Bloom/darkworld/filter-heavy scene | Effects render coherently per eye or are documented with a comfort/correctness reason. | Scene name, HMD observation. | Not run |
| Motion blur | Event/boss scene that triggers camera blur | Motion blur uses per-eye history and has no cross-eye smear or comfort issue. | Scene name, HMD observation. | Not run |
| Fade/wipe/trimming | Area transition, pause/menu transitions | Fade/wipe/trimming overlays appear in flat composition with expected ordering. | HMD observation. | Not run |
| Late particles | Scene with late 3D particles and 2D-game particles | Camera-dependent particles are stereo-correct; 2D-game particles remain in flat composition. | Scene name, HMD observation. | Not run |
| Mirror resilience | Minimize/occlude mirror while XR active | HMD rendering continues. Restored default mirror shows eye 0 plus flat UI. | HMD observation and mirror screenshot. | Not run |
| SBS mirror debug | `AURORA_XR_MIRROR_SBS=1` | Mirror shows side-by-side debug preview without breaking HMD rendering. | Mirror screenshot. | Not run |

## Representative Scene Checklist

Use legally dumped local game data only. Exact stage names can vary by save
state; record the actual stage/room and how the scene was reached.
Use `docs/plans/vr-post-effects-audit-2026-05-12.md` to map post-effect
validation focus areas back to the current per-eye and flat-composition calls.

| Validation Focus | Candidate Scene Type | Primary Code/Behavior Under Review | Pass Criteria |
| --- | --- | --- | --- |
| Outdoor stereo/culling | Ordon/Faron/Hyrule Field style open area | `dusk::vr::conservative_culling_fovy()`, per-eye `begin_eye_view()` matrices | Peripheral geometry remains visible while turning the head; no eye mismatch. |
| Water/refraction | Fishing pond, lake, underwater, or water surface with refraction | Per-eye water/refraction and framebuffer/depth capture paths | Refraction aligns per eye and does not smear or sample the other eye. |
| Bloom/filter/darkworld | Twilight/darkworld, strong bloom, fog/filter-heavy scenes | Per-eye bloom/filter/darkworld/invisible list portions of the post block | Effect is comfortable, stable, and depth-consistent in both eyes. |
| Motion blur/history | Event, boss, or fast camera motion that triggers blur | Per-eye framebuffer capture scratch/history selection | No cross-eye ghosting; blur is comfortable or documented as disabled/deviated. |
| Fade/wipe/trimming | Area transition, pause transition, special fade/wipe | Flat composition tail, `calcWipe()`, `trimming()`, normal and special fade paths | Overlay appears once in head-locked flat UI with correct ordering. |
| Late particles | Scene with camera-dependent 3D particles plus 2D-game particles | Per-eye late 3D/effect lists and flat 2D-game particle composition | 3D particles stereo-project correctly; flat particles do not double per eye. |
| World-projected reticles | Player sight and boomerang lock targets | `dComIfGd_setWorldProjected2DXlu()` queue drawn per eye | Reticles stay attached to targets and are absent from the flat UI duplicate path. |
| Flat UI readability | Gameplay HUD, pause/menu, settings, ImGui console | OpenXR flat UI quad layer, RmlUi/ImGui/native UI passes | UI is readable in HMD, stable relative to head, and mirrored as expected. |

## Known Unvalidated Areas

- Physical headset compositor behavior has not been validated in this workspace.
- Scene-specific world/depth post effects still require per-scene inspection.
- SteamVR sees the connected headset when selected explicitly, but Dusk cannot
  create a physical OpenXR session yet because Dawn currently selects a Vulkan
  API version above SteamVR's reported maximum.
- The system active OpenXR runtime is currently Virtual Desktop, which did not
  expose an HMD system to the default OpenXR probe during this test.
- The current queue synchronization policy is conservative `vkQueueWaitIdle`
  before image release; it is validated by simulated-HMD tests but should be
  revisited only after physical runtime validation confirms correctness.

## Known Deviations Log

Use this table only after testing a scene. Do not pre-fill deviations from
implementation assumptions; each row should cite a concrete scene and observed
behavior.

| Date | Runtime / Headset | Scene | Deviation | Reason | Follow-up |
| --- | --- | --- | --- | --- | --- |
| 2026-05-12 | SteamVR explicit OpenXR runtime / connected physical headset | Startup/runtime activation | Required XR startup fails before HMD presentation; probe detects two stereo views but reports `xr_status=blocked`. | Dawn-selected Vulkan device API version `1.4.329` exceeds SteamVR OpenXR runtime maximum `1.2.0`; bypassing the guard caused session creation to fail during testing, so the guard remains protective. | Create the Dawn Vulkan device through an OpenXR-compatible API version or runtime-mediated Vulkan device creation path before rerunning physical presentation, controller, UI, mirror, and scene rows. |
