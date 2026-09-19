# Retail / Unreal / Quest frame-time review — 16 September 2026

Targets: 90 FPS VR (11.111 ms/frame), 144 FPS desktop (6.944 ms/frame).
Current builds: Windows 2026.09.16.46; native Quest 2026.09.16.quest.36.

Latest native result: **57.83 FPS average, p99 21.308 ms** in the live Caul
capture, versus 54.07 FPS in v34. **90 FPS is not met.** The final sections record
the v35/v36 changes, measurements, limits, deployment and disk cleanup. The
earlier v34 distance-culling toggle dropped performance back to 12–14 FPS in
the same executable, verifying the larger preceding improvement on-device.

## What the connected Quest revealed

The user supplied a live low-performance location in Singularity Caul, around
landblocks 0904 / 0905. The installed v32 build was captured before modification.
The 664 complete game-loop frames averaged **85.081 ms (11.75 FPS)**, with
95th percentile **90.981 ms** and 99th percentile **97.385 ms**. VrApi typically
reported 12–13 application frames per second against a 90 Hz display. Menus and
head direction changed during this diagnostic trace: it is a bottleneck capture,
not a controlled stationary before/after trial.

The game-thread trace attributed about **18.465 ms/frame** exclusively to
particle mesh uploads and **13.497 ms/frame** to other effects work. There were
about **820 emitter-buffer flushes per frame**. The render thread received about
**811 light-transform updates per frame** (8.92 ms/frame), plus about 4.10 ms in
light/primitive interaction creation. These threads overlap: their times must
not be added together to estimate total frame time. `xrWaitFrame` is waiting,
not 32 ms of application CPU work.

Scene inventory found 1,024 animated scenery actors, 822 translucent sections
in that group, 30 entities, 121 terrain actors, and 17 interior actors. There
were **zero duplicate network identities and zero identical static placements**.
The one inventory paperdoll capture had both continuous capture flags disabled.
This does not rule out every possible rendering defect, but it contradicts the
idea that this slowdown is primarily duplicate copies of the entire world.

A brief live shadow-quality 5 → 0 comparison remained at roughly 12–13 FPS.
The view was not rigidly controlled; this rules out shadows as the dominant
cause in that sample, not as a meaningful GPU cost in all scenes. Quality 5
was restored afterward.

Evidence: `Quest/Performance/20260916/Before.utrace`, `Before-Frames.csv`,
`Before-{GameThread,RenderThread,RHIThread}.csv`, `Before.log`, `Before.png`,
and `ShadowsOff.log`. Frame timings are game-loop intervals; VrApi display
refresh and GPU-oriented App timings are different metrics.

## Changes in this build

1. **Restore authored particle draw distances.** Retail's
   `CPhysicsPart::GetMaxDegradeDistance` and
   `GfxObjDegradeInfo::get_max_degrade_distance` use the last drawable model's
   maximum distance, falling back to 100 AC meters without a table.
   `CPhysicsObj::ShouldDrawParticles` and `ParticleEmitter::UpdateParticles`
   suspend distant ambient simulation. Our decoder used the table only for
   draw mode / invisible stubs, then discarded its positive distance.
   The shared client now carries that metadata into each emitter. Distant
   emitters skip transforms, particle-buffer uploads and point lights. Initial
   particle allocation is deferred until in range. Infinite ambient effects
   resume without a backlog; finite effects still exhaust time/count budgets.
   Explicit stop commands retire degraded emitters. Sky/weather and tracked
   hand feedback remain exempt. This changes visual simulation only, not
   server projectile travel, collision or damage. Unlike retail's additional
   cell/frustum rejection, this patch uses conservative 3D camera distance;
   it does not claim to implement all retail visibility rules.

2. **Cache stable world draw commands.** Terrain and interior procedural meshes
   opt into Unreal's static mesh-batch submission. This avoids rebuilding each
   stable section per view and frame. Geometry and section visibility edits
   invalidate the proxy; transforms and material parameters remain live.
   Animated parts and particles keep dynamic submission. Wireframe falls back
   to the dynamic path. This reduces CPU submission work; it does not pretend
   to remove the legitimate depth, stereo and shadow passes.

3. **Avoid intermediate VR limb poses.** Local and remote tracked head/arms no
   longer reset to bind transforms immediately before receiving their final
   tracked transforms. Attached equipment and lights avoid those extra updates.
   Untracked limbs keep their bind fallback. Upper/lower-arm placement and scale
   are applied together, preserving the existing IK and hand orientation.

4. **Negotiate Quest refresh early.** An Android-only OpenXR extension module
   loads before instance creation, checks supported rates, and requests 90 Hz
   only when available. It logs both the request and actual refresh events.
   The separate 72 FPS application cap was removed. SteamVR keeps the runtime's
   chosen rate. A successful refresh request is not proof of 90 FPS rendering.

5. **Bound mobile shadow resolution.** The existing mobile setup uses a single
   nearby 40 m cascade; the Android configuration now explicitly caps CSM
   resolution at 1024 as well as the general shadow-map resolution.

## Shadows on the headset

Yes, this renderer can support them without enabling Mobile HDR. Inspection of
the installed UE 5.8 mobile renderer confirmed CSM setup and shader policies are
available in the LDR forward path used by this project. A rendered ES3.1 test at
AC-scale world coordinates, using a real DAT human and a ground receiver,
detected 400 darkened pixels when the directional shadow was enabled.
The hidden head/body parts retain full-body shadow casting.

Evidence: `Unreal/Saved/v34-MobileRegression/`,
`Unreal/Saved/PerformanceShadowTests/Shadow-On.png` and `Shadow-Off.png`.
This is a mobile-renderer regression, not a substitute for checking both eyes
on the physical Quest. Stereo appearance and the cost in a lit gameplay scene
still require a live device sample after installation.

Epic's rendering-mode overview provides background; the installed engine and
the actual LDR rendering test are the evidence for this project's behavior:
https://dev.epicgames.com/documentation/unreal-engine/mobile-rendering-and-shading-modes-for-unreal-engine

## Retail differences and remaining performance work

- **Models and animations:** retail uses DAT setup parts, replacement-model
  degrade tables and fixed-function-era drawing. Unreal reconstructs those
  parts into meshes/materials and adds modern lighting, scene proxies and
  shadow passes. The authored replacement-model LOD chain is still not used
  for ordinary scenery/characters; this is a remaining gap distinct from the
  particle distance fix. Physics geometry should stay independent of visual LOD.
- **VR:** two eyes, tracked limbs, world-space menus, controller selection,
  contact queries and pose replication add work absent from retail. Skipping
  intermediate limb transforms preserves those features. Existing opt-in pose
  updates and ordinary retail motion messages were not changed here.
- **Terrain and interiors:** Unreal uses reconstructed geometry, cell admission
  and portal clipping; retail's BSP/PView path is different. Caching stable draw
  commands improves submission while keeping the same admission/collision.
  Cold streaming, doorway crossings and physics cooking need separate tail-
  latency captures; fixed-camera benchmarks do not certify them.
- **Effects:** current batches still transform/upload visible particle vertices
  on CPU. An instanced particle renderer and a measured budget for nearby
  cosmetic lights are possible next steps if the new native trace still points
  there. Do not reduce spell hit detection or hide near combat feedback for FPS.
- **Crowds and UI:** many character material sections and world-space widget
  redraws remain costs. Consolidating compatible sections or reducing unchanged
  widget work should follow fresh native profiles. Preserve text clarity, alpha
  silhouettes and the sky blending fixes; no grass was added.

The refreshed source navigation inventory covers 2,144 owned C++/C# files and
approximately 482k lines across server, adapter, DAT, client and renderer code.
It identifies expensive-operation sites for focused review. It is not a claim
that every line or every retail gameplay feature has been revalidated.

## New desktop measurements

Same-build reference/optimized comparisons, at identical quality and resolution:

- Outdoor, cached draw commands off → on: **265.99 → 282.59 FPS**;
  mean **3.760 → 3.539 ms**, p99 **4.524 → 4.412 ms**. A modest gain in this run.
- Interior, cached draw commands off → on: **408.12 → 408.76 FPS**;
  mean **2.450 → 2.446 ms**, p99 **3.132 → 3.090 ms**. Effectively unchanged.
- Dense Caul, particle distance off → on: **96.87 → 294.00 FPS**;
  mean **10.323 → 3.401 ms**, p95 **37.449 → 4.277 ms**,
  p99 **42.757 → 4.806 ms**.
- Caul repeated in reverse order: optimized **285.04 FPS**, reference
  **94.59 FPS**. Optimized p99 **4.960 ms**, reference **44.032 ms**.
  The large improvement survives the change in run order.

At the Caul sample-start audit, 817 emitter slots existed in both variants.
With the optimization, 782 were degraded; active particles dropped from
34,485 to 917 and particle lights from 813 to 32. The asynchronous all-pass
RHI draw snapshot dropped from 2,630 to 1,524. The old path's periodic particle
updates explain why its median was much faster than its 37–44 ms tail frames.

The optimized stationary scenes meet the desktop 144 FPS budget at p99.
This is not a guarantee for every location, cold load, multiplayer crowd,
or PC VR stereo view. Two Caul repeats are stronger evidence than a single
trial, but still a small sample. No compile/render-test workloads ran during
the measured desktop samples.

Evidence directories:
`Unreal/Saved/PCPerformance/Review20260916/CachedWorld-v34/`,
`ParticleDistance-v34/`, and `ParticleDistanceReverse-v34/`.
Each run contains a frame CSV, summary JSON, screenshot, trace and scene audit.
`Before-v33/` holds the earlier pre-change outdoor/interior/effects reference;
the same-build switches above are the controlled comparisons used for attribution.

## Historical retail comparison

The September 14 matched-location PresentMon capture measured retail + Decal
at 181.57 FPS (p99 11.068 ms) and Unreal desktop v18 at 213.22 FPS (p99 6.521 ms).
Each sample was 45 seconds at the Smithy on this PC. Different UI overlays,
quality, camera framing and D3D9/DXGI presentation paths limit that comparison.
No new retail capture was made in this pass. Do not compare those present
intervals directly against native Quest game-thread times as if they were the
same hardware, workload, or metric.

Source: `Unreal/Saved/RetailParityReview-20260914/comparison-summary.json` and
its raw PresentMon CSVs. The current desktop measurements use reproducible
offline DAT scenes with synthetic NPCs, not a live retail replay.

## Reproduction and validation

`Unreal/Build/VR/Measure-PCPerformance.ps1` runs a 30-second warm-up and a
30-second sample per scene at 2560 × 1440, uncapped, VSync off, no HMD and an
isolated user directory. The local PC has a Core Ultra 9 285K, RTX 5090 and
64 GB RAM. Keep builds and other GPU work stopped during measurements.

Use `-Comparison cached-world -Scenes outdoor,indoor` to compare cached draw
commands, or `-Comparison particle-distance -Scenes caul` for the authored
ambient-effect optimization. These switches retain the reference path in the
same executable. `caul` uses the captured landblock vicinity and 32 synthetic
NPCs; it is not the user's exact camera or live monster population.

On-device comparisons can toggle `ace.Particles.DistanceCulling 0/1` without
reinstalling. `ace.RenderAudit` reports active/degraded emitter counts and
particle lights, alongside scene inventory. Its RHI draw count is an asynchronous
all-pass snapshot, not the number of unique world objects.

Desktop SM5 regressions passed for particle distance/timing/lighting, cached
geometry, VR rig/menus and VR render/replication. ES3.1 LDR regressions passed
for particle distance/timing/lighting, cached geometry, particle batch lifetime,
VR rig/replication and the shadow receiver. The particle pixel oracle now
accounts for UE's mobile LDR gamma output rather than comparing it to linear
desktop HDR. Packaged Windows checks passed for cached geometry, particle
distance and VR rig/render replication. Reports are under `Unreal/Saved/v34-*`.

Windows and Android Development packages built successfully. Shared client
source/runtime-material parity passed. Quest v34 was installed over the existing
app, and its on-device APK SHA256 matches
`E346EDF0DCB7CEDC739BE50E18BE85286B2401B118218EC57785A4C38AC0EFC1`.

The Windows build is deployed to `Unreal/PackagedVR/Windows`; executable SHA256
`30CEB8B20772A98108C64E09C8F3E36F9245248FB1F7CE7C8D2EC30C87A89B1C`
matches the tested archive. The running ACE server was not changed.

## Native v34 follow-up: headset capture completed

The user returned to the live Caul area on September 16 at about 19:21 EDT.
The installed v34 build delivered 55–58 FPS before tracing; the detailed capture
recorded 1,920 complete game-loop frames over 35.54 seconds, averaging
**18.495 ms / 54.07 FPS**, with **p95 20.863 ms and p99 21.870 ms**.
Every captured frame exceeded the 11.111 ms budget for 90 FPS. Later samples
were around 50–54 FPS as head direction changed. This is substantial progress
from the earlier 11.75 FPS capture, but **the native 90 FPS target is not met**.

The same running executable was tested with `ace.Particles.DistanceCulling 0`
for a short reference interval. It dropped to 11.9–14.0 FPS, with 26,384 active
particles and 831 particle lights. With culling enabled, the initial scene audit
showed 840 emitter slots, 803 degraded emitters, 682 active particles and 34
particle lights. Culling was restored to 1 afterward. This same-build switch
supports attributing the major gain to the distance optimization, rather than
the earlier/later build comparison alone.

Game-thread exclusive work per complete frame:

- Particle buffer flush: **0.254 ms**, previously 18.465 ms.
- Other effects: **0.698 ms**, previously 13.497 ms.
- HUD binder: **0.845 ms**; Slate canvas: **0.298 ms**.
- VR component: **0.760 ms**; player controller: **0.731 ms**.
- Cell visibility: **0.679 ms**.
- Pinned retail/vitals panel scopes: **0.228 / 0.217 ms**.
- `xrWaitFrame`: **6.886 ms of waiting**, not CPU computation to optimize away.

The render-thread light-transform scope fell from about 811 calls and 8.92 ms
per frame to 15 calls and 0.189 ms per frame. Thread durations overlap and must
not be added into an imaginary total frame cost. GPU tracing was requested,
but the exported GPU event timeline is empty on this build. VrApi's retained
`App` values are roughly 8–10 ms; these are runtime telemetry, not a usable
per-render-pass GPU breakdown. No GPU pass attribution is claimed.

### Shadows on the actual headset

A short on/off/on comparison from 19:28–19:29 EDT measured approximately
50–51 FPS with `r.ShadowQuality 5`, 54 FPS in the settled off interval, then
50–51 FPS after restoring it. This suggests roughly 1–1.5 ms of frame cost in
this scene, not a path from 54 to 90 FPS by disabling shadows. Head pose moved,
the sample was short, and screenshots briefly disturb frame times; treat this
as directional evidence rather than a precise fixed-camera GPU benchmark.

The headset confirmed the 1024 CSM resolution cap. Stereo screenshots were
saved, but the dark Caul ground, changing view and visible Quest boundary
overlay do not provide a clean visual shadow acceptance test. The earlier
ES3.1 rendered shadow test still proves the renderer path. A clearly lit
nearby body/ground shadow in both eyes remains a separate visual check.

### Next measured optimization priorities

1. Reduce unchanged HUD refresh work and redundant panel render-target draws.
   The binder invokes many panel refresh functions even with the main menu
   closed, while pinned vitals still need the source canvas. Preserve chat,
   server confirmations, drag input and live vitals; do not simply stop UI ticks.
2. Avoid redundant collision-body recreation. The trace contains about ten
   `CreatePhysicsState` calls per frame. A one-second nested export attributes
   424 calls to the world-presenter path and 104 to the VR component. This is
   evidence of recurring work; the exact objects and triggering setters need
   narrower instrumentation before changing collision behavior.
3. Reduce repeated visibility/portal work and inactive-emitter tick overhead.
   Visibility alone costs 0.679 ms/frame outdoors; 837 effects component scopes
   still execute per frame even though only a few emitters are drawing.
   Camera and doorway correctness must remain intact when caching work.
4. Obtain a valid GPU pass capture, then evaluate submission, LOD and UI
   composition cost. The current trace does not justify reducing texture or
   antialiasing quality, which previously caused visible regressions.

Artifacts: `Quest/Performance/20260916/v34-live/Optimized.utrace`, frame
and scope CSVs, `Reference.log`, `ShadowComparison.log`, stereo PNGs,
`summary.json`, and reproducible `analyze.py`. The first `ShadowsOff.log`
contains only an asleep interval and is excluded from results. The successful
comparison is `ShadowComparison.log`. Final console queries verified
`ace.Particles.DistanceCulling=1` and `r.ShadowQuality=5`. No build, server or
persistent graphics-settings changes were made during this profiling follow-up.

## v35/v36 follow-up: equipment, dormant effects, UI

The normal world-presenter tick called `DestroySpawnedSelf` every frame. Even
when no duplicate self actor existed, that helper detached all equipment and
then reattached it. The helper now clears pending self descriptors but returns
before touching equipment when there is no duplicate actor. The exceptional
duplicate-self path still preserves and rehomes equipment. A real-DAT regression
checks 120 repeated calls without weapon-transform churn, correct held collision
policy, and recovery when a duplicate self actor actually exists.

Distant, infinite ambient emitters that have no scripts, sounds, tweens, UV
scrolls or other time-sensitive work now check for re-entry at 10 Hz instead of
every frame. This can delay reappearance at the authored cutoff by up to 100 ms.
Visible particles, finite effects, weather and tracked-hand feedback keep their
existing cadence. New effects immediately cancel the idle interval, and their
first tick excludes time accumulated before they were started. Fully finished
distant components now reach the tick-disable path instead of returning early
and continuing to tick indefinitely. `ace.Particles.IdleTickInterval 0` provides
a reference path; the default is `0.1`.

Pinned VR surfaces only refresh when enabled and available. Incoming UI state,
chat, confirmations and the source canvas still update. An additional temporary
name index in the VR panel pass was tried in v35 and then removed in v36 after
measurement: its few queries did not justify indexing the whole retail tree.
The gameplay binder retains its separate index for its much larger query batch.

### Native v35 capture and regression finding

`Quest/Performance/20260916/v35-live/Optimized.utrace` contains 1,937 complete
game-loop frames over 34.99 seconds: **18.047 ms / 55.41 FPS**, p95 **21.746 ms**,
p99 **23.345 ms**, maximum 24.490 ms. All frames still miss 11.111 ms. Compared
with v34's 54.07 FPS, mean frame time is 0.448 ms lower; the tail is worse. Head
pose, live effects and the Quest boundary overlay limit this cross-build
comparison. This is not evidence of reaching the VR target or improving tails.

- Physics-state creation fell from about 10/frame to **2.009/frame**. A one-second
  nested export attributes the remaining 110 calls to the VR component; the
  recurring world-presenter equipment churn is gone.
- Effects component scopes fell from about 837/frame to **195.54/frame**.
  Their exclusive time fell from 0.698 to **0.485 ms/frame**.
- Unscoped world tick fell from 1.067 to **0.697 ms/frame**. Particle batch flush
  remained about **0.269 ms/frame**; visibility remained **0.666 ms/frame**.
- VR component exclusive time increased from 0.760 to **1.485 ms/frame**.
  A subsequent same-build UI reference trace measured **1.032 ms/frame** with
  name indexing off, but the HUD became more expensive (0.808 to 1.255 ms/frame).
  This supports removing the new VR-only index while retaining the HUD index.
- `xrWaitFrame` is **7.156 ms/frame of waiting**. It is not computation, and
  overlapping game/render/RHI time must not be summed.

A brief idle-tick toggle measured approximately 49–52 FPS with the optimization
off, then 56 FPS after restoring it, before a later 51 FPS sample. Head movement
and short intervals limit attribution. The retained log distinguishes transition
samples. Scene audit: 841 emitter slots, 797 degraded, 931 active particles,
41 particle lights, zero duplicate identities and zero overlapping static
instances. Source, raw logs, screenshot, CSV exports and reproducible `analyze.py`
are retained together. The first asleep UI attempt produced no usable trace and
was excluded; `UILinear.utrace` is the subsequent active capture.

### Desktop same-build idle-effect comparison

The reverse-order repeat in `Unreal/Saved/PCPerformance/Review20260916-v35-IdleReverse`
ran after compilers and Insights exports stopped. With authored particle-distance
culling on in both runs, the Caul fixture measured **300.79 FPS / 3.325 ms** with
idle throttling off versus **371.48 FPS / 2.692 ms** with it on. P99 improved from
**4.454 to 4.100 ms**; both fit the 6.944 ms desktop budget. This isolates idle
tick throttling, not the full v34-to-v35 change. It is a fixed offline DAT scene
with synthetic NPCs, not a certification of all live play or PC VR.

The first order also improved (285.83 to 370.84 FPS), but its optimized interval
overlapped an Insights export on the host. It is retained for transparency and
not used as the primary desktop comparison. V36's UI correction affects VR only,
so the no-HMD idle comparison exercises the same code path in both revisions.

### Validation and cleanup

Windows and Android v35 packages built. Desktop, mobile-renderer and packaged
Windows regressions passed for movement/equipment, particles and VR rig/menus.
The final idle-wake change also passed all three mobile particle checks. After
removing the VR index for v36, the mobile-renderer menu regression passed again.
Reports are retained under `Unreal/Saved/v35-*` and `v36-*`.

The requested disk cleanup removed 225 obsolete generated targets with no errors:
old symbol copies, abandoned linker temporary files, superseded Quest-share
staging, duplicated benchmark mesh caches, unused IDE/download/staging caches
and the secondary editor intermediate directory. It reclaimed **57.42 GiB of
actual free disk space** (71.55 GiB logical file sizes), reaching 395.76 GiB free
before subsequent builds regenerated scratch files. Source, assets, databases,
current builds, published releases, profiling evidence and the running server
were preserved. Current packages and the latest shared archive passed before/
after checksum checks. See `Unreal/Saved/DiskCleanup-20260916-193915.json`.

`tools/Clean-ObsoleteBuilds.ps1` previews exact targets by default. It validates
resolved paths remain in the workspace, rejects reparse-point trees and refuses
to clean while an editor/compiler is active. Removal uses PowerShell literal
paths throughout. Symbol/share retention is deliberately fixed to this cleanup;
future-version artifacts are not automatically deleted.

### Final v36 headset verification and deployment

The user returned to Caul with menus closed after the final installation.
`Quest/Performance/20260916/v36-live/Optimized.utrace` contains **1,732 frames
over 29.98 seconds**, averaging **17.292 ms / 57.83 FPS**, with **p95 20.367 ms,
p99 21.308 ms**, and maximum 24.929 ms. All frames remain over the 90 FPS budget.
The earlier v34 capture was 18.495 ms / 54.07 FPS: this is a modest observed gain
of about 7%, not a controlled fixed-head A/B or evidence of steady 90 FPS.

VR exclusive work is now **0.628 ms/frame**, below both v35's 1.485 and v34's
0.760 ms. Effects execute **189.64 scopes/frame at 0.471 ms**, and physics-state
creation remains about **2.007 calls/frame**. HUD is 0.829 ms and cell visibility
0.674 ms. Scene inventory remains 841 emitters, 797 degraded, 930 live particles,
41 lights, and zero duplicated world identities or overlapping static placements.
Sky/fog work varies between captures (0.551 ms/frame in v35 versus 0.001 in v36),
so the entire frame-time difference cannot be assigned to the UI correction.

The remaining budget gap requires more work on stable UI refreshes, visibility,
the two remaining VR collision-body recreations, and a valid GPU pass capture.
The CPU trace contains 7.772 ms/frame in `xrWaitFrame`; that is waiting and is
not justification for removing XR synchronization. Shadows were retained; the
earlier measured shadow cost and visual-validation limitations still apply.

Both final packages built successfully. V36's mobile-renderer and packaged
Windows VR-menu regressions passed; reported warnings are existing login-layout
construction diagnostics, not failed assertions. Shared source/runtime-material
parity passed with zero differences. Windows is deployed to
`Unreal/PackagedVR/Windows` with executable SHA256
`1D667887985C7810E3C539A6887C94F2F8D55548C1167410D88A15575CE35C3A`.
Quest reports version code 36 / `2026.09.16.quest.36`; its installed APK matches
`050B4F233445C543CFA73D07B80D381FFCA813BC1A9E0741BB33EB8F0D6964AE`.
The running server is unchanged. Final queries verified distance culling 1,
idle interval 0.1, HUD indexing 1 and shadow quality 5. Capture was stopped.

To reproduce the final summary, run `v35-live/analyze.py` with the `v36-live`
directory as its first argument. Raw frame/scope CSVs and the screenshot are
retained beside the trace; no new full build archive was created for profiling.

### V37: Yaraq shadow receivers and combat aiming

The user confirmed that native Quest had no environment shadows in Yaraq. A
live change to `r.Mobile.Shadow.CSMShaderCullingMethod=0` restored them. UE 5.8's
mobile receiver admission performs a separate primitive-octree query from the
caster query fixed earlier; that octree also excludes AC coordinates beyond its
extent. The fix uses the existing visible-primitive list. Startup and Quest
configuration now persist it, without changing world coordinates or terrain.

The former single 1024 shadow map covered 40 metres, leaving a coarse player
silhouette. Mobile lighting now uses two 1024 cascades over 60 metres, with
distribution exponent 7 to concentrate the first cascade within roughly 7.5
metres. An additional startup override forcing Android back to one cascade was
corrected. Desktop retains four cascades over 600 metres. Both paths continue
to cast the real body and equipment geometry; first-person hidden body parts
retain hidden-shadow casting. This does not smooth the authored DAT geometry.

The mobile-renderer and desktop `ACE.Rendering.MobileShadowReceiver` and
`ACE.VR.RigAndMenus` tests passed. Shadow captures use an actual DAT human and
runtime sky light at AC world coordinates. Hiding the human preserved 193/193
shadowed ground pixels on ES3.1 and 200/202 on SM5. Packaged Windows repeated both
tests successfully (199/200 shadow pixels); its absent HTML report template and
existing login-layout diagnostics did not fail any test assertions. JSON reports
and PNG captures are under `Unreal/Saved/v37-Tests-*`.

Combat no longer opens the floating Impact/Trigger instruction panel. Peace-mode
interaction help remains. Arc, straight magic, bow, crossbow and thrown missile
previews highlight the first creature body along the predicted path, respecting
intervening geometry. Targeted non-projectile spells follow explicit selection;
self spells do not light up an enemy. Regression coverage includes overlapping
targets, losing a target, a blocking wall, and switching straight/arc topology.
Only one preview mesh section is used; straight projectiles use one segment.

Windows `2026.09.16.47` is deployed to `Unreal/PackagedVR/Windows`, executable
SHA256 `04FFCB0BB4F399633EDEDBF7D6B7D6D7E6ED0414868CDF2511512F938D81EA4B`.
Quest code 37 / `2026.09.16.quest.37` is installed; its installed APK matches
`7FF97064DED02FDE6A3EDCAEA9192A4E326378B2AD710CA3EDB436F8BDE2CA9F`.
Shared client source and runtime-material parity passed with zero differences.
The server is unchanged. Device queries verified method 0, two cascades, 1024
resolution, and runtime light range 6000 cm/exponent 7. Screenshots, settings
queries and comparison logs are in `Quest/Performance/20260916/v37-Yaraq`.

The live two/one/two-cascade comparison recorded 44.22, 36.30 and 37.10 FPS
respectively (9/10/10 one-second VrApi samples at 90 Hz, render scale 0.8).
Head direction and visible effects changed while the user tested; the two
identical-setting samples also drift substantially. These are not a controlled
estimate of cascade cost or a performance improvement claim. The screenshots
confirm different views. The default two-cascade setting was restored afterward.
The 90 FPS target remains unmet in this scene.

### V38: move the visible shadow-detail boundary outward

The user confirmed that v37's local body shadows and aimed-creature highlights
looked great, but the nearby change in shadow detail was jarring. UE's
`MobileLightingCommon.ush` selects a cascade by depth and only fades at the final
distance; it does not use the desktop inter-cascade blend. Increasing
`CascadeTransitionFraction` alone would not fix that visible switch.

V38 reduces the mobile distribution exponent from 7 to 3. With two cascades and
60 metres total range, the detailed region ends at roughly 15 metres instead of
7.5, and the density difference between maps is smaller. Cascade count and
1024 map resolution are unchanged. This moves/reduces the visible transition;
it does not introduce a shader crossfade or establish a frame-rate improvement.
Desktop's four-cascade layout remains unchanged.

The mobile shadow regression passed again and preserved 190/190 shadowed ground
pixels with the body hidden. Windows `2026.09.16.48` is built/deployed, executable
SHA256 `C4EE5AB6310CE23CE061A210F0ABB207E31C7879487637929562F459D78BBB06`.
Quest code 38 / `2026.09.16.quest.38` is installed and its APK hash verified as
`62FBDD22A89B45D50D1F65BF62FEEF21F45072CC94CE0B2FAD638C1FDAC80753`.
Source/material parity is exact. The headset slept before a live comparison of
the wider split, so that visual check remains pending; the app was not launched
asleep. V37's combat-highlight and full-body-shadow user confirmations still
apply to the unchanged combat and avatar paths.
