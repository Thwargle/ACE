# Loading, memory, and fog performance — 21 September 2026

Targets remain 90 FPS VR (11.11 ms) and 144 FPS desktop (6.94 ms).
Neither target is established by this investigation. Captures began from public
v56, with v57/v58 as local profiling builds. The validated changes described here
are now included in public release v60; its steady-scene FPS remains unmeasured.

## Confirmed improvements

- Cold v57 player appearance spent 94 ms decoding and 7,218 ms creating meshes
  and acquiring material parents. In v58 these phases were 97.64 ms and
  45.82 ms. All 66 runtime material assets had already loaded asynchronously
  in 357.8 ms before login. The user confirmed that the first login felt much
  better. Total portal time was still about 41.95 seconds, so this fixes a
  blocking initialization stall, not every part of world streaming.
- DAT indexing and disk-cache maintenance now use dedicated thread-pool work
  with inherited Unreal context. They cannot be executed on the game thread
  as background TaskGraph work during synchronous package loading.
- Gameplay layout JSON is parsed off-thread. Detached widget construction has
  a per-frame budget and the prepared layout is reused at world reveal.
- Terrain mip filtering and lossless packing happen before game-thread texture
  creation. In the captured scene, 170 terrain textures retain 533.06 MiB of
  compressed CPU restore data instead of 680 MiB of base-level pixels: a
  146.94 MiB reduction while also retaining their prepared mip chains.
  GPU textures still occupy 906.77 MiB. Their 1024-square base resolution,
  BGRA8 pixels, and 11 mip levels have not changed. This is lossless CPU
  storage compression, not ASTC compression of terrain on the GPU.

## Remaining steady-scene cost

The v55 stationary baseline averaged 45.68 FPS over 31 one-second samples.
The v58 dense outdoor capture was around 40–44 FPS. Scene, weather, and view
differences prevent treating those samples as a controlled build comparison.
Do not claim a steady-state FPS gain from the loading or memory changes.

The complete v58 live CPU trace contains 2,156 frames over about 46 seconds:

- `ACE_SkyFog`: 472 calls and 3.734 seconds exclusive time, about 7.9 ms per
  update. It rewrites fog parameters across cached materials and world actors.
- Uniform-expression updates: 4.345 seconds exclusive across rendering tasks.
- Vulkan pooled-buffer allocation: 2.270 seconds across 2.18 million calls.
- Mesh draw setup: 2.935 seconds across workers.
- `xrWaitFrame`: 25.203 seconds is synchronization/waiting, not application CPU
  work. These timers overlap across threads and must not be added into one
  critical-path frame time. GPU timestamps were unavailable, not zero cost.

Quest v59 replaces scene-wide material fog rewrites with one per-world
material parameter collection. Local fog masks remain material parameters so
interiors, previews, and particles can opt out. Optional old baked landscapes
retain their compatibility path. Render tests cover live color changes,
global disabling, and the local opt-out; the actual Quest FPS benefit must be
measured after installation.

## Live comparisons and rendering configuration

Point lights, 15-second A/B/A phases: 43.07 FPS enabled, 44.47 disabled,
44.33 restored. Disabling them provided no demonstrated benefit over the
restored phase, so lights remain enabled.

Scene screen percentage, 10-second A/B/A phases: 44.1 FPS at 100, 43.6 at 70,
40.9 restored. This did not improve performance; the separate mobile view
scaling path also limits what this toggle can establish. Resolution remains
unchanged. A fog-rate comparison was excluded because menus changed mid-test.
All live experimental settings were restored and tracing was stopped.

The running Quest uses Vulkan, mobile multiview, 1344 × 1408 pixels per eye,
and 4× MSAA. OpenXR's reported ideal size is 1680 × 1760, with the saved headset
render-target scale at 80%. Scene screen percentage is 100. It is not running
3072 × 3216 at 2× MSAA. Keep `r.CullInstances=0`: enabling that separate GPU
instance culling path previously caused right-eye scenery popping.

## Evidence and follow-up

Private captures are under the ignored `Quest/Performance/20260921-Runtime`
directory, including `Perf58-CPU.utrace`, `v58-timers.csv`, `v58-cold-final.log`,
`v58-audit.log`, and `v58-VrApi-final.log`. The live A/B/A comparisons have their
own `comparison.json` files beneath `Quest/Performance/Comparison-*`.
Raw logs can contain account, character, and server information; they are not
release artifacts.

Do not use the small startup-only v58 trace for world timing. A second startup
process tried to reuse its trace path and Unreal refused to overwrite it.
The separate 36.6 MB live CPU trace is the complete world capture.

After the shared fog change, repeat the same outdoor view with menus closed
and inspect fog, uniform-expression, and buffer-allocation costs. If those
costs fall, investigate remaining primitive updates, mesh submission, and
widget rendering using a new trace. GPU terrain block compression remains a
separate experiment requiring a fast encoder/cache, alpha/seam fidelity
checks, and measured cold-start cost before enabling it.

## Validation and deployment

- Windows editor and game Development builds passed.
- Desktop material compilation, rendered weather/fog, terrain texture fidelity,
  and loading-transition automation passed. UI preparation and terrain seam/
  arrival coverage also passed during the v57/v58 changes.
- Mobile ES3.1 LDR material and rendered weather/fog tests passed, including
  shared fog color changes and global/local disable checks.
- All 35 current cooked material parents were regenerated with their shared
  fog collection dependency. Quest Vulkan/ASTC cooking and packaging passed.
- Quest package `2026.09.21.quest.59` was installed and verified on the local
  Quest 3. Shared Unreal/Quest source and runtime assets match. The headset
  was asleep, so the game was left closed for the user's next test.
- v59 steady-scene FPS and CPU cost remain unmeasured. Do not substitute the
  shader tests or removed material loops for an actual device comparison.

## September 24: portal traversal allocation reduction

The shared portal visibility path now retains immutable cached doorway
geometry instead of copying every doorway's vertex array into each traversal.
Active traversals keep their own shared reference across cache eviction or DAT
reload. Partially streamed geometry is still not cached. Each queued view moves
its frustum and path into the traversal, and polygon clipping reuses its two
scratch buffers between planes. Camera clipping still runs every frame.

A warmed desktop PView fixture (landblock `7D64`, four phases of 1,000 calls)
measured 44.33 microseconds per call before and 29.34 after, about 34% less CPU
time for this routine. Both versions produced 18 visible cells, 9 entry
apertures, and 6 exits. The isolated scratch-buffer benchmark did not show a
meaningful timing gain; the combined traversal result includes the removed
geometry and queued-view copies.

This is not a full-frame benchmark or a Quest FPS measurement. The connected
Quest reported unauthorized USB debugging during this pass, so native CPU,
memory, GPU, and FPS comparisons remain pending. Resolution, texture quality,
culling settings, network update rates, and visible-cell results are unchanged.

Validation passed for doorway cache lifetime/invalidation, portal clipping,
interior streaming, terrain reveal, and mesh application. The mesh, doorway
cache, and clipping scratch tests also passed with the ES3.1 mobile renderer.
The Windows editor and Android Development builds passed. Private benchmark
logs and reports are under ignored `Unreal/Saved/ArmorHealingAudit/`, including
`perf-before.log`, `perf-after.log`, and `mobile-game.log`.
