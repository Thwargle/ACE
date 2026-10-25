# Local cosmetics API 1

The Windows client includes a reusable renderer and Lua service host. A cosmetic
plugin ships separately in `Saved/ClientPlugins/Installed/<id>`. No client rebuild
or native Unreal module is needed for a new provider using this contract.

The client ships the API, not importers or game-specific data. Providers ship Lua,
manifest-defined controls/presets, and a self-contained Windows converter. The
converter owns source discovery, file integrity/provenance checks, conversion,
and cache receipts. Lua remains sandboxed. Native converters are ordinary local
programs outside that sandbox, so enabling a provider explicitly grants both
`local_cosmetics` and `local_tools`. There is no general Lua process, filesystem,
network, console, or native-library API.

## Manifest

```json
{
  "api": 1,
  "id": "my_cosmetics",
  "name": "My Cosmetics",
  "version": "1.0.0",
  "mode": "local",
  "service": "local_cosmetics",
  "service_api": 1,
  "permissions": ["local_cosmetics", "local_tools"],
  "cosmetics": {
    "helper": "tools/MyConverter.exe",
    "cache_namespace": "MyCosmetics",
    "source_label": "source",
    "help": "Choose a locally available model.",
    "fields": [
      {"key":"root", "option":"--root", "label":"Source folder", "required":true},
      {"key":"model", "option":"--model", "label":"Model", "required":true},
      {"key":"slot", "option":"--slot", "label":"Slot", "default":"weapon"}
    ],
    "presets": [{"name":"Example", "values":{"model":"example","slot":"weapon"}}]
  }
}
```

`helper` must be a relative `.exe` path inside the installed plugin. Namespaces
and field keys use at most 64 letters, digits, `_` or `-`. Fields require unique
keys and `--option` names. Up to 24 fields and 32 presets are supported. Optional
`prefix` is prepended to that field's value as one argument (for example `0=`).
`required` applies to conversion. Empty optional fields are omitted. Converter
arguments are quoted individually and never interpreted by a shell. A source scan
uses `root`, when present; selecting a scan result populates `root` and `product`
fields if declared. Controls and presets are data, not executable UI scripts.

Ship the declared converter and its dependency licenses under `tools`. Build
dependencies and game assets do not belong in the package. The current Windows
API rejects network paths, symlinks and junctions for helpers and cache/source
files. A missing converter leaves a visible error in the plugin window.

## Lua lifecycle

`main.lua` returns `function(snapshot, profile)`. The snapshot has `service_api`,
`busy`, `has_selection`, `request`, and `status`. `request` is an opaque token for
a button action already validated and retained by the client. Example:

```lua
local scanned, restored = false, false
return function(s, p)
    if s.busy then return nil end
    if s.request > 0 then
        scanned, restored = true, true
        return {action="local_cosmetics", request=s.request}
    end
    if not scanned then
        scanned = true
        return {action="local_cosmetics", operation="scan"}
    end
    if not restored then
        restored = true
        if s.has_selection then
            return {action="local_cosmetics", operation="reload"}
        end
    end
end
```

Only `scan`, `reload`, or a matching outstanding request token are accepted.
Lua cannot supply executable paths, command lines, output paths, or gameplay
commands. Tokens cannot be replayed. The existing 32 MiB Lua heap and instruction
budget apply. An enabled service starts at login without starting automation.
Disable, refresh, script failure, capability unload, and shutdown cancel pending
work and restore original rendering. A failed script stays stopped until enabled
again. UCM and other gameplay automation can continue running independently.

One provider owns the cosmetic override at a time. The latest verified selection
releases other providers, preventing competing hidden-mesh restoration states.
Held items must attach to the locally possessed character and carry its ownership
ID. Body overlays attach only to that possessed human pawn's appearance component. Collision, item statistics and network equipment are unchanged.

## Companion protocol

The host runs these commands asynchronously. Ordinary replies are limited to 256 KiB
and 120 seconds. Catalog replies allow 128 MiB; `scan-items` has no time cutoff
and can be cancelled, with provider checkpoints available for resuming.

- `scan [--root <folder>]`: stdout JSON with `installations`, an array of objects
  containing `root`, optional `product`, and optional `version`.
- `convert-select <manifest field arguments>`: convert local sources, validate and
  select the resulting cache, then return the verified receipt below.
- `verify-active`: revalidate the selected cache and its original sources, then
  return the same receipt. Do not trust a cache solely because it exists.

Exit zero with one JSON document for success. Nonzero exit may return
`{"error":"Readable explanation"}`. Successful convert/verify output:

```json
{
  "schema": 1,
  "id": "0123456789abcdef0123456789abcdef",
  "directory": "C:/Users/Player/AppData/Local/MyCosmetics/cache/0123456789abcdef0123456789abcdef",
  "slot": "weapon",
  "sourceStamp": "C:/LocalSource/build-version.txt",
  "fit": {"x":0,"y":0,"z":0,"pitch":0,"yaw":0,"roll":0,"scale":1}
}
```

The directory must be exactly `%LOCALAPPDATA%/<cache_namespace>/cache/<id>`;
`id` is a 32-digit GUID. The helper stores `active.json` in that namespace to
indicate an available selection. Its content is private to the helper. The
client polls the text `sourceStamp` every five seconds and disables rendering if
it changes or disappears; it is limited to 1 MB. Original asset checks remain
the helper's responsibility. Optional `fit` uses centimeters and Unreal degrees;
if present all seven finite values are required, translation is limited to
±500 cm and scale to 0.01–10. If omitted the transform is identity: the client
assumes no particular source axes. Slots are currently `weapon` (AC attachment 1), `offhand` (attachment 8) and
`shield` (attachment 3). The offhand weapon and shield cannot occupy the same hand.
The body features add `head`, `shoulders`, `waist`, `back`, `chest`, `arms`,
`hands`, `legs`, and `feet` on AC human setups 0x02000001 and 0x0200004E.
ACWM/2 attaches rigid sections; ACWM/3 follows animated body parts with up to
four influences per vertex. Covered AC clothing/accessories are suppressed,
with a bare underlay preserving skin and the face. Disabling restores them.

## Mesh interchange

Each verified cache contains `mesh.acwm` and `texture-0.png`, `texture-1.png`, etc.
All integers/floats in ACWM are little endian:

1. Four ASCII bytes `ACWM`, int32 version `1`, int32 vertex count, int32 section count.
2. Per vertex: eight float32 values `x,y,z,nx,ny,nz,u,v`.
3. Per section: int32 blend (`0` opaque or `1` masked), int32 index count, then
   that many int32 vertex indices. Each consecutive triple is one triangle.

Coordinates are Unreal centimeters, Z up, left handed, with clockwise front
faces. No source-specific handedness or mount conversion happens in the client.
Limits: 100,000 vertices, 64 sections, 300,000 indices per section, 32 MiB mesh,
4096×4096 per PNG, 32 million pixels total. PNG dimensions are checked before
decoding. Mesh values must be finite and bounded, indices valid and counts
divisible by three. Trailing data is rejected. Materials are the client's
unlit textured/masked materials. Source-game material shaders are not imported.

ACWM/2 uses the same header/vertices with version `2`. Each section starts with
an additional int32 body-part index before blend/count/indices. Allowed mappings:
`head` → 16, `shoulders` → 10 and/or 13, `waist` → 0, `back` → 9. Other indices
are rejected. Additional mappings: chest 0/9; arms 10/11/13/14; hands 12/15;
legs 0–8; feet 3/4/7/8. Held slots require version 1; body slots accept 2, 3 or 4. A shoulder
pair has sections for each arm, each following its own native animated component.
Coordinates are relative to the target part in centimeters; human part geometry
faces -Y. The receipt fit applies to every part in that slot. These mounts are
source-independent; converters own source axes, scale and fitting.

ACWM/3 adds skin data after vertices and before sections: int32 binding count
(1–4096), then each binding's int32 AC part and six float32 values for source
pivot/end. Then each vertex has exactly four int32 bone/float32 weight pairs.
Weights must be finite, nonnegative and sum to one (tolerance .02); bone indices
must be valid. Section part indices are -1. Source pivots and vertices use the
same whole-body bind coordinate frame. The renderer fits bone spans to AC,
updates only when the pose changes, and applies the receipt fit to the garment.

ACWM/4 retains version 3's layout and permits rigid sections inside a skinned
appearance. A section part of -1 uses the skin weights; a valid nonnegative part
uses that AC part's local frame directly. Rigid vertices still carry valid weights
for format validation. Rigid and skinned sections must not share vertices. This
supports one garment containing cloth plus an attached buckle or decoration.

ACWM/5 retains version 4's sections and weights. Each binding additionally stores
six float32 values after pivot/end: reference minimum XYZ and maximum XYZ in its
anatomical frame (Z along pivot-to-end; X is cross(+Y,Z), using +Z when parallel;
Y is cross(Z,X)). Equal bounds opt out. Finite, ordered bounds are required.
The renderer uses these bare reference-body bounds to fit girth and terminal
parts against the wearer's bare AC anatomy, independent of equipped clothing.
Joint lengths drive limbs; torso bounds drive waist/chest height. Penetrating
cloth vertices are projected outside the bare surface once at attachment time,
then rebound to the corresponding AC segmented part to retain that fit during
animation. Foot surfaces fit against outward geometry and follow AC's separate
toe parts. Adjacent joints use temporary bare reference geometry even if the
cosmetic does not replace their clothing; those references are released after
fitting. Legacy bindings without reference bounds retain four-weight skinning. Providers
should supply sufficient cloth topology for the different body's contours.
Rigid sections preserve their authored mounts. Normal fitting matrices are
cached; anatomical fitting does not run on each animation tick.

The paperdoll also accepts `prepare-appearance --id <cache id>` for a selected
catalog entry. Its response is a verified receipt used to update that entry's
model thumbnail; it does not activate or save the draft outfit. Named unavailable
entries may retain a cache ID for diagnostics with `supported: false`; they are
searchable, but cannot replace a draft selection.

Overlays have no collision or replication. Replaced clothing and accessory parts
are hidden only after a successful attachment. Shared bare underlays avoid overlap
between sleeves/gloves and leggings/boots. Parts restore on disable or logout,
and reattach after appearance rebuilds. Remote characters are never changed.

Held-item replacements also hide the original item's particles and inferred
particle lighting, including effects created after the cosmetic is applied.
Effect lifetimes continue normally. Removing the cosmetic or swapping weapons
restores the original effects while respecting distance culling. Player spell
effects and effects on other items are unaffected.

## Release and validation

`ClientMods/api.json` advertises the contract and ships with ACEClient. The runtime
marker is `ACE.LocalCosmetics/1`. Windows release assembly requires that marker
and rejects optional AC-WoW files or raw/converted model files in the client
archive. `ACE.Plugins.CosmeticAPI` tests a synthetic, independently named provider,
path boundaries, identity mounting, mesh loading and texture limits. No optional
provider checkout or proprietary assets are needed for core client tests.

## Paperdoll and local catalogs

A provider can opt into the native slot picker with `cosmetics.paperdoll: true`.
The shared interface draws slot buttons with thumbnails and a dropdown with an integrated, automatically focused search field per selected slot,
remembers choices while switching slots, and provides per-slot session fit controls.
Body slots accept compatible rigid or skinned armor; providers must not map
unavailable armor silently to a hand.

The provider implements `scan-items`, `catalog-items`, `apply-outfit` and `clear-slot`. These require
an opaque UI request token; Lua cannot invoke arbitrary scans or pass arguments.
`scan-items` receives the declared `root`, `product`, optional `listfile`, `filter`
fields. There is no scan-count parameter. `catalog-items` receives only root/product. Set `scan_only: true`
on fields that must never be passed to manual `convert-select`.

Catalog replies contain `items` and a `note`, with no entry-count truncation. Each item has `id`,
`name` (at most 256 characters), `sourceSlot`, `targets` (supported renderer/body slot
keys), `supported`, `reason`, `values` (declared converter field defaults), and a
GUID `cacheId` for a converted model or provider conversion plan. Entries without valid cache IDs are excluded from the
picker; unavailable named entries remain visible with their reason but cannot replace a draft selection. A shield targets `shield`; it appears under the paperdoll's combined
offhand/shield button. Names and source-to-target mappings belong to the provider.
Use the actual in-game item name in `name`. Optional `kind: "model"` marks raw source-model variants,
hidden until **Include unnamed model variants** is checked; `kind: "item"` and omitted kinds are shown normally.
Search matches name plus `values.item`, `values.model` and `values.display`, case-insensitively with multiple words.
Filtering and keyboard navigation do not change the draft; clicking or Enter commits the choice.
Receipts may include `appearanceName` and `catalogId` (up to 256 characters each) to preserve names on outfit reload.

Choosing entries changes a draft, retaining choices across slots. **Apply outfit**
invokes `apply-outfit --selection slot=cacheId` once for every selected slot; an
empty draft clears the outfit. The provider must verify all sources and mappings
before atomically replacing its saved selection, then return the whole
`appearances` array. The host validates all slots before replacing renderers;
a bad slot retains the existing outfit. Up to 12 distinct slots are accepted,
with shield/offhand conflicts rejected. `verify-active` and `clear-slot` may
also return an empty outfit. Discard reloads the saved outfit.

Scanning is asynchronous and cancellable, with no time or item-count cutoff.
Optional `scan-progress.json` in the provider's cache root (up to 16 KiB) can
report `attempted`, `total`, `ready`, and `model`; the host polls it once a second.
Providers should checkpoint their own catalog and bound working memory while
continuing through all local candidates. Per-asset validation limits still apply.
Fit adjustments remain session-only and reset on re-verification.

Providers can write optional `thumbnail.png` beside `mesh.acwm` in each GUID cache.
The thumbnail must be PNG, at most 256x256 pixels and 256 KiB. Include it in the
provider's output integrity receipt. The shared client derives its path from the
validated GUID/cache namespace; catalog entries cannot redirect image loads.
Slots, dropdown rows and the main preview share this convention. Visible widgets
own their texture lifetime; the full catalog does not eagerly decode thumbnails.
No native provider registration or source-specific image code is required.
An optional `iconId` references another GUID cache's `thumbnail.png` as a fallback while the wearable
model has not been converted. The same local-path, PNG dimensions and byte limits apply. This lets many
named items share one installed-game icon without duplicating files or converting every model in advance.
Model thumbnails take precedence once prepared. The provider owns icon generation and source validation.
