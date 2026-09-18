# Project override of UE 5.8 ProceduralMeshComponent

Upstream: installed Epic Games UE 5.8 runtime plugin. Original
`Private/ProceduralMeshComponent.cpp` SHA256:
`170B578B98C4A3957CF75E69754806B464B2E3CA3B7AE40CF936477BF092E3D7`.

The raster mesh path reuses the scene proxy's persistent primitive uniform buffer
instead of allocating one per section, eye, and frame. The scene proxy maintains
the buffer when transforms and primitive data change. The original allocation
path remains the fallback when a persistent buffer is unavailable.
Use `ace.Render.ReusePrimitiveBuffers 0` for a controlled comparison.

Non-colliding particle batches can update a packed prefix of their fixed-capacity
vertex buffers and submit only its live indices. UV buffers stay unchanged; the
position, tangent and color uploads scale with the live vertex count. Empty
sections issue no draw. Proxy recreation and ray-tracing geometry use the same
active index count. Normal procedural meshes still update and draw their full
sections. Unused CPU vertex slots are unspecified until a full update.
Use `ace.Particles.ActivePrefix 0` to compare the original full-capacity path.
`ACE.Rendering.ParticlePrefixRender` compares rendered translucent/additive output
through growth, swap removal, clearing, refill and scene-proxy recreation.

`Quest3Test/Sync-ClientSource.ps1` mirrors this source and plugin descriptor into
the native project. Both projects already explicitly enable this plugin. Keep
this override aligned with the engine version when upgrading Unreal.

Terrain and interior components opt into `bPreferCachedDraws`. Their stable
sections use `DrawStaticElements` so Unreal can cache mesh draw commands rather
than rebuilding every section per eye every frame. Transforms use the proxy
uniform buffer; geometry/section-visibility edits recreate the proxy. Wireframe
uses the dynamic path. Animated parts and particles retain dynamic submission.
Use `ace.Render.CachedWorldDraws 0` before loading a scene for a reference run.
`ACE.Rendering.CachedWorldDraws` compares real rendered pixels after transforms,
material changes, hide/show, vertex edits, and clearing geometry.
