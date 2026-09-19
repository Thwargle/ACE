# Rendering and frame-time review — 14 September 2026

The target is 144 frames per second: **6.944 ms per frame**. Measurements below
come from the local Core Ultra 9 285K / RTX 5090 / 64 GB PC, using the packaged
Windows Development client at 2560 × 1440, D3D12 SM5, VSync off and an uncapped
frame rate. They are repeatable reference scenes, not a claim that every live
server location or stereo VR scene meets the target.

## Measured changes

Matched runs used the same executable, content, resolution and quality. Only
`ace.UI.IndexedLookups` and `ace.Render.ReusePrimitiveBuffers` changed from 0
(reference behavior) to 1 (optimized behavior). Each run warmed up for 30 seconds
and then measured for 30 seconds. Other fixes in this release were common to
both variants; the later VR grip cache and collision-filter reuse are not
credited with the gains below.

- **Outdoor Yaraq, 32 NPCs:** 196.43 → 251.00 FPS; mean frame time 5.091 →
  3.984 ms; 99th-percentile frame time 6.039 → 4.996 ms.
- **C88C0143 interior, 8 NPCs:** 350.83 → 363.11 FPS; mean 2.850 → 2.754 ms;
  99th percentile 3.763 → 3.560 ms.
- **Outdoor spell effects:** 191.77 → 243.12 FPS; mean 5.215 → 4.113 ms;
  99th percentile 7.121 → 5.696 ms.

The optimized reference scenes all meet the 144 FPS budget at the 99th
percentile. The effects reference exceeded that budget before the two changes.
HUD refresh time in the CPU trace fell from about 1.041 to 0.761 ms outdoors and
1.022 to 0.729 ms with effects.

Evidence is under `Unreal/Saved/PCPerformance/Review/`, with separate baseline
and optimized folders for each scene: raw frame CSV, summary JSON, screenshot,
Unreal Insights trace and exported timer statistics. Early diagnostic folders
named BaselineOutdoor and BaselineOutdoor2 are not the matched comparisons.
The final release recheck is under `Unreal/Saved/PCPerformance/Release10/`.
It measured 252.84 FPS outdoors, 364.34 FPS indoors and 243.74 FPS with effects;
99th-percentile frame times were 4.916, 3.474 and 5.699 ms respectively.

## Work completed

1. **Procedural mesh rendering:** reused the scene proxy's persistent primitive
   uniform buffer instead of allocating a temporary one for each section and
   view every frame. The original path remains available for comparison. The
   project's UE 5.8 ProceduralMeshComponent override is shared by PC and Quest;
   its upstream provenance is recorded in that plugin's ACE-CHANGES.md.
2. **HUD lookup:** replaced repeated tree searches during one refresh with an
   ordered name index. Visibility is checked live; root changes and window
   reordering invalidate the index. Duplicate names preserve original search
   order. Dropdown refresh no longer repeatedly hides the open options panel.
3. **Particles:** updates touch active slots and slots that need clearing, retain
   authored vertex colors and multiply the original alpha by particle fade.
   Unchanged UVs/tangents are no longer copied through the update interface.
4. **Scenery:** exact duplicate transforms are suppressed within each setup,
   placement and collision variant. The audited outdoor scene reports zero
   duplicate identities and zero overlapping static instances. One identical
   authored pair was present before this change. Distinct nearby objects are
   preserved. Collision uses authored physics geometry or setup collision
   primitives, cooked once per shared mesh, rather than visual mesh bounds.
5. **VR collision queries:** repeated movement substeps reuse the same object
   exclusion filters while their admitted interior-cell sets remain unchanged.
   Actual collision and support sweeps still run for each substep.
6. **Doorway material:** portal-plane rejection exits as soon as a pixel is
   outside the admitted volume. Stereo eye and shadow behavior are preserved.
7. **Networking:** each socket pump has a packet limit and a time budget so a
   burst cannot drain indefinitely on the game thread. Whole packets are
   processed atomically and queued UDP datagrams retain their order. A larger
   receive buffer accommodates short bursts.
8. **VR attachments:** holding-location metadata is cached per setup rather
   than rereading and decoding DAT bytes twice per avatar per frame. The cache
   is bounded and replaced with the DAT builder when archives are reloaded.

The earlier Quest 8 trace contained 767,577 pooled-buffer allocation calls and
804,266 frees, with resource-deletion spikes up to about 119 ms. That trace also
showed substantial HUD, movement, effects and visibility CPU work. It did not
include a GPU timing channel, so it does not establish a precise GPU bottleneck
or certify a Quest FPS improvement from the new renderer path.

## Review coverage and remaining opportunities

`tools/Review-Performance.py` inventories owned C++/C# source across the server,
adapter, common/entity/database/DAT libraries, Unreal bootstrap, ACEClient,
ACEWorldBake and the procedural-mesh override. The inventory covers 2,121 files
and 475,888 lines. SDKs, generated intermediates, binaries and third-party retail
reference trees are excluded from these counts. This is a repository-wide
navigation survey plus focused review of hot paths, not a line-by-line
certification that the entire repository is bug-free.

The expensive-operation index includes 35 actor-scan sites, 26 mesh creation
sites, 11 mesh update sites, 20 render-state rebuild sites, four physics-cook
sites and six scene-capture sites. Their presence alone is not a defect.

Reviewed behavior already limits several costs: distant terrain rings omit full
scenery, caches have bounded trimming, settled entities stop ticking, distant
animation presentation is throttled, and scene captures are used for explicit
paperdoll/inspection views. The reference outdoor world has no active capture.
VR poses use bounded-rate updates only for opted-in observers; retail clients
continue to receive standard movement/animation messages. Server database and
network workers already run separately; parallelizing world mutation is not a
justified client frame-rate optimization.

Further work should be driven by these measurements:

- **Character draw sections:** the populated outdoor snapshot has roughly
  3,000 draws across all passes. This does not mean the world is drawn twice.
  Consolidating compatible sections or adding a purpose-built avatar renderer
  is a larger remaining opportunity. Clothing palettes, hidden body sections,
  shadows and independently posed VR limbs need to remain correct.
- **Particle GPU uploads:** active-slot CPU work is reduced, but the procedural
  mesh still uploads its reserved vertex buffer. A custom instanced particle
  renderer could reduce upload bandwidth and draw submission. It needs visual
  parity checks for blend mode, source color, depth and shadow behavior.
- **Native Quest GPU cost:** capture fresh CPU and GPU traces in a populated
  room, at a doorway and during repeated spells. Compare transparent overdraw,
  shadows and stereo material cost before lowering quality. The headset was
  asleep during the final PC review, so this validation remains open.
- **Streaming transitions:** fixed-camera tests exclude rapid landblock changes,
  portal arrivals and real server bursts. Profile those separately for mesh
  creation, physics cooking and appearance construction spikes.
- **Live multiplayer:** repeat with both VR observers and retail players, then
  compare pose handling and crowded-scene animation cost. Synthetic NPCs do not
  model server traffic or headset motion.

## Reproduce

From the project root in PowerShell 7:

```powershell
pwsh -NoProfile -File Unreal/Build/VR/Measure-PCPerformance.ps1 -OutputRoot ./Unreal/Saved/PCPerformance/NewComparison
```

Run without an editor, another client, a build or another GPU workload. The
runner uses an isolated user directory and refuses to overwrite a completed
run. `-Variants optimized` runs just the release behavior. The development-only
`ace.PerfScene outdoor|indoor|effects` harness requires a disconnected client and
the existing local DAT install; it never logs into or mutates the server.

The camera is fixed and the population is synthetic. Screenshots verify that
terrain/interior, NPCs, sky, HUD and spell effects are actually rendered. The
interior camera can intersect the local avatar near the edge of the view; it is
not a controller-comfort or camera-alignment acceptance test. Frame summaries
are the performance comparison; aggregated timer counts from multiple trace
tracks must not be interpreted as duplicate world draws.

## Native Quest follow-up, afternoon of 14 September

The connected Quest 3 actually presented at **90 Hz (11.111 ms budget)**. The
project's `t.MaxFPS=72` setting is not a request to change the display refresh
rate. The native outdoor scene did not meet either a 72 Hz or 90 Hz comfort
target. Desktop benchmark results above must not be used to claim that it does.

Evidence is in `Quest/Performance/20260914/`. In the outdoor diagnostic,
20 one-second app samples at 100% eye-buffer scale averaged **20.65 FPS**;
33 samples at 80% averaged **25.24 FPS**. The runtime's App timing field averaged
34.05 and 26.16 ms respectively. These are sequential diagnostic samples, not a
controlled fixed-view benchmark: the user could move their head and the Quest
boundary overlay was sometimes active. They are not p95/p99 frame timings.
`Summarize-VrApi.py` filters by the game PID so the system compositor's 90 FPS is
not mistaken for game performance.

The valid 47-second `QPerf14-OutdoorCPU.utrace` contains approximately 1,215
game frames. Exclusive CPU work per frame includes body-part animation 2.21 ms,
HUD 2.20 ms, visibility 1.82 ms, world-fog updates 1.81 ms, and particle-batch
flush 1.02 ms. Render-thread light/primitive interaction creation averages
1.19 ms; there are roughly 45 light-transform updates per game frame. The RHI
thread spends about 4.04 ms per frame submitting the mobile base pass. Wait
scopes, especially `xrWaitFrame`, are not CPU work. GPU channels in these traces
contain no GPU timer events, so they do not provide a GPU pass breakdown.

The scene inventory reports zero duplicate object identities, zero overlapping
static instances and zero active scene captures. It includes 121 terrain actors,
4,152 scenery instances and 152 animated scenery actors. Those are loaded
objects before visibility culling, not counts of simultaneous draw calls.

Changes deployed in Quest version 14 and Windows 2026.09.14.24:

- The VR resolution control now changes UE 5.8's real OpenXR eye-buffer CVar,
  `xr.SecondaryScreenPercentage.HMDRenderTarget`. The previous `vr.PixelDensity`
  variable is absent, so the user's quality setting had no effect.
- World-fog broadcasts run at 10 Hz instead of potentially walking all material
  caches every frame. Clock jumps and fresh sky builds update immediately;
  tracked poses, sky movement and lighting continue at their existing rates.
  `ace.Sky.FogUpdateHz=0` restores the original cadence for comparison.
- The world-selection outline renders its fixed texture once. The target-label
  texture redraws when its text changes. Mesh transforms remain live. This
  removes two unnecessary UI render-target passes while looking at an unchanged
  selected object; the baseline RHI trace showed four Slate target renders per
  frame in that scene.
- Shared startup no longer overwrites the Android shadow-cascade cap with four.
  The runtime sun already requested one cascade, so no measured speedup is
  attributed to this correction.
- Sky additive fades explicitly handle mobile LDR's gamma output. Celestial
  particles use clamped, full-detail sky textures, and faint glow-card borders
  fade out. The underlying moon masks were intact. A dedicated LDR rendering
  regression now exercises glow brightness and edges as well as the desktop
  sky checks. Headset screenshots separate the game artifacts from the blue/red
  Quest boundary grid.

Prioritized remaining work: reduce particle vertex-buffer uploads; bound active
effect lights by visibility/distance; batch compatible scenery/avatar sections;
replace repeated per-material environment broadcasts with shared shader
parameters; and profile the remaining sky/translucent overdraw on the device.
The current measurements do not justify disabling collisions or dropping
required world objects. Stock OpenXR foveation is also worth a controlled
startup A/B test, but has not been enabled as an unmeasured quality change.

Capture caveats: `QPerf14-Stationary.utrace` is a mixed snapshot that includes
movement, spells and reconnecting. Its recording continued into
`QPerf14-MixedFull.utrace`; neither is a stationary benchmark. The 77-byte file
`QPerf14-Outdoor.utrace` is a failed pull, and the matching header-only CSVs are
invalid. Use `QPerf14-OutdoorCPU.utrace` and `OutdoorCPU-*.csv` for the short valid
outdoor CPU capture. Draw events were disabled during that capture and restored
afterward. The temporary resolution and shadow experiments were also restored.

One separate reliability defect was observed: after the server timed out a
suspended Quest, the client continued showing an apparently connected world.
Reconnecting and teleporting the existing developer character recovered it.
This is not evidence that the outstanding collision problem is solved; an
established-session receive-timeout/recovery path still needs implementation.

Version 14 follow-up: the user confirmed that the moving colored cards and moon
border blend correctly in the headset. The retained live log contains 43 app
samples from 20:26:07–20:26:49 UTC at 80% resolution: 26.77 FPS, runtime App
25.31 ms, GPU busy 88.0%. This is a different head view from the earlier sample;
it is not a controlled before/after speedup. The comfort target remains unmet.
`VR14-Resolution80.json` records the calculation. The attempted later
`VR14-SkyFixed.png` is empty because the app/headset was no longer presenting;
it is not screenshot evidence. The valid desktop/mobile rendering fixtures and
the user's live confirmation support the sky result.

The requested follow-up in version 15 enables alpha-to-coverage on the masked
world materials used for leaves and banners. The previous 4x MSAA configuration
alone did not enable this material feature. Runtime world textures now use the
anisotropic world sampler with linear mip interpolation, honoring the existing
Texture Filtering option instead of overriding it with TF_Trilinear. Sky and UI
textures retain their separate sampling rules. The VR settings expose 2x/4x
MSAA on renderers using MSAA, with 4x remaining the quality default. Deferred PC
rendering retains its temporal AA. Higher resolution is still adjustable, but
is not forced on the already GPU-limited Quest. These changes need a separate
native quality and frame-time check; they are not a claim that all temporal
shimmer or the performance deficit has been eliminated.

Native acceptance rejected the version 15 quality experiment: B-to-inspect was
confirmed working, but foliage flicker worsened against sky (not terrain).
Reducing r.MaxAnisotropy live did not improve the user's observation; that does
not isolate the material-coverage change from texture sampler lifetime. Version
16 restores trilinear world textures and the original cutout parents.

Review then identified a separate sky-edge error: the sky's shader used resolved
SceneDepth to reject an entire pixel whenever any foreground depth was present.
That loses partial MSAA coverage at foreground silhouettes. Version 16 removes
this whole-pixel gate, moves sky vertices farther along their per-eye viewing
rays, and uses hardware depth testing without sky depth writes. Projection and
layer ordering stay intact. A white-on-white edge regression checks for dark
fringes, alongside the existing sky fade and occlusion renders. The native
headset still has to confirm visual improvement. The change also removes one
depth-texture read per sky pixel, but no GPU speedup is claimed without a
controlled measurement.

The v15 gameplay sample (20:45:20–20:46:19 UTC, 60 one-second samples) averaged
32.12 FPS at 80% resolution/90 Hz, runtime App 16.89 ms, GPU busy 72.2%. The
headset changed view, menus and boundary overlay during the capture. It is not
an apples-to-apples comparison with the previous outdoor samples. The scene
audit still found zero duplicate identities and overlapping static instances;
one capture was present for the open inventory paperdoll. This is expected UI
work rather than a duplicate world render.

Version 16 native feedback: plants look a little better against the sky; residual
shimmer remains. The 21:17:00–21:17:59 UTC sign-area capture averaged 28 FPS at
80% resolution/90 Hz, runtime App 21.26 ms, GPU busy 79.7%. See
`VR16-Smithy-Resolution80.json` and `VR16-Final-VrApi.log` (game PID 25540).
Different views and headset overlays prevent a controlled performance comparison.
This remains far below a comfortable native VR frame rate.
