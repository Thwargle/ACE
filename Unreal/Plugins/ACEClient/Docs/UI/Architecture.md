# Asheron's Call Original UI — Architecture

One-to-one mapping from retail client classes to Unreal (ACEClient plugin).

## Runtime path (DAT-driven gameplay HUD)

1. **Extract** LayoutDesc trees from `client_local_English.dat` via `Unreal/diag/UIInventory`  
   → writes `Docs/UI/Resolved/0xXXXXXXXX.json` (nested children, `imageFile` DIDs, baseLayout merge).
2. **`UACEUILayoutResolver::LoadLayout`** loads the resolved JSON into `FACEUIElement` trees  
   (unique `InstanceId` per node — `ElementId` is not unique after baseLayout merge).
3. **`UACEUIFlow::SetMode(Gameplay)`** loads `classic_gameplay` (`0x21000005`).
4. **Default visibility** for classic gameplay hides on-demand floaties; only always-on chrome paints.
5. **`UACEUICanvasWidget`** paints LayoutDesc at **1:1** pixel size. Floaties edge-anchor on
   wide/tall windows; vitals use clipped retail meter textures.
6. **`AACEPlayerController`** prefers the DAT canvas (`bUseDatDrivenHud`); `UACEGameHUDWidget` is fallback only.

Regenerate resolved layouts:

```text
dotnet run --project Unreal/diag/UIInventory/UIInventory.csproj -c Release -- "C:\Turbine\Asheron's Call" "Unreal/Plugins/ACEClient/Docs/UI"
```

## classic_gameplay placement (RootGameplay_Field, 800×600)

Placement on larger viewports is **retail edge-anchor reflow** (`UIElement::UpdateForParentSizeChange`,
acclient.c:158919), driven by the authored `leftEdge/topEdge/rightEdge/bottomEdge` in the resolved
JSON. Left/Top modes: 2 = follow parent delta, 3 = center, 4 = proportional, 0/1 = keep.
Right/Bottom modes: 1 = follow, 3 = center, 4 = proportional, 0/2 = keep. Mode 0 keeps the runtime
box (user-dragged floaties). Disagreeing pairs stretch (SmartBox/Keyboard fill, PowerBar widens).

| Element | Position | Size | Default | Authored anchors (L/T/R/B) → behavior |
|---------|----------|------|---------|----------------------------------------|
| Radar | 680, 0 | 120×140 | visible | 2/1/1/2 → top-right |
| FloatyIndicators | 0, 0 | 150×30 | visible | 1/1/2/2 → top-left |
| FloatySideVitals | 0, 0 | 460×26 | visible | 3/1/3/2 → **top-center** (retail; no X hack) |
| FloatyToolbar | 490, 500 | 310×100 | visible | 2/2/1/1 → bottom-right |
| FloatyMainChat | 0, 500 | 410×100 | visible | 1/2/2/1 → bottom-left |
| FloatyPanel (inv/char/…) | 490, 128 | 310×372 | hidden | 2/2/1/1 → bottom-right |
| FloatyExamination | 180, 128 | 310×400 | hidden | 2/2/1/1 → bottom-right |
| FloatyCombatPanel | 0, 400 | 610×90 | hidden | 3/2/3/1 → bottom-center |
| FloatyEnvPanel | 0, 400 | 610×120 | hidden | 3/2/3/1 → bottom-center |
| PowerBar | 95, 400 | 610×25 | hidden | 1/1/1/2 → stretches horizontally |
| FloatyChat1–4 | left column | 250×104 | hidden | 0/0/0/0 → keep runtime box (draggable) |
| FloatyVitals | 0, 0 | 160×58 | hidden (alt to SideVitals) | 3/1/3/2 → top-center |
| Keyboard / SmartBox | 800×600 | — | hidden | 1/1/1/1 → stretch to fill viewport |
| Admin | 0, 0 | 416×400 | hidden | 3/3/3/3 → centered |

Toggle via `UACEUIElementManager::SetElementVisibleByName` (e.g. `RootGameplay_FloatyPanel_Field`).
Opening a panel page replaces other pages in the same floaty; clicking a floaty brings it to front
(`BringFloatyToFront`). Children are clipped to floaty / `PanelPages` bounds (retail UIRegion clip).

RootGameplay_Field's full-screen `imageFile` is cleared at load so the world shows through.

## Class map

| Original | Unreal |
|----------|--------|
| `UIElementManager` | `UACEUIElementManager` |
| `UIRegion` / `UIElement` | `FACEUIElement` |
| `UIFlow` | `UACEUIFlow` |
| LayoutDesc inflater | `UACEUILayoutResolver` (resolved JSON from language DAT) |
| Media / textures | `UACEUIResourceResolver` |
| 800×600 canvas host | `UACEUICanvasWidget` |

## Coordinate system

- LayoutDesc geometry: **800×600** logical pixels at **1:1** (no stretch).
- Larger viewports: **retail per-edge anchor reflow** (see placement table above). The root is
  resized to the viewport (retail `UIElementManager::RefreshEvent`) and every child re-anchors
  from its authored edges. Retail note: layout DID `0x21000008` is `classic_externalcontainer`,
  not the gameplay layout — gameplay is `0x21000005` under UIFlow mode `0x10000008`.
- Vitals meters use retail DAT fill textures (`0x0600747E…`), clipped by vital fraction — not solid color overlays.

## Layout nesting (why there is only one LoadLayout)

`classic_gameplay` is the whole gameplay tree. The panel and env-floaty pages reference the other
classic layouts through `baseLayout`, and the extractor inlines them, so inventory
(`0x21000023`), vendor (`0x21000012`), secure trade (`0x2100000D`), external container
(`0x21000008`), salvage (`0x2100000C`) and the rest arrive as sub-trees of
`RootGameplay_Field` (element `0x10000495`) rather than as separate roots. Toggling a page is
therefore a visibility change, not a layout load.

## Viewport elements (type 0x0D)

LayoutDesc marks true 3D hosts as `UIElement_Viewport`:

| Element | Rect | Status |
|---------|------|--------|
| `RootGameplay_SmartBox_Field/PortalSpace` | 800×600 | World view (rendered by the game viewport) |
| `InventoryPanel_Field/PaperDollField/PaperDoll` | 224×214 | Live character via scene-capture rig |
| `FloatyExamination/BasicCreatureExamineUI/Exam_PaperDoll` | 300×265 | Scene-capture rig (`EnsureExamPaperDollPreviewRig`) |
| `SmartBox/BarberField/Barber3DViewport` | 200×200 | Not implemented |

The paperdoll rig (`UACEUIGameplayBinder::EnsurePaperDollPreviewRig`) spawns an off-map actor with
`UACECharacterAppearanceComponent` + `USceneCaptureComponent2D`, flags its parts
`VisibleInSceneCaptureOnly`, and paints the render target into the element rect beneath the
equipment slot icons. The Setup is rebuilt only when appearance or equipment changes.

Note: inventory grid cells are **not** authored in LayoutDesc — retail `gm3DItemsUI` creates them
at runtime, so the client's 32×32 cell math over `Inv_3DItemList` is the correct model.

## Element type factories

`UACEUIElementManager::Initialize` registers a factory per retail `ElementDesc.Type`, and
`UACEUILayoutResolver` builds every node through `CreateElementByType`, so each node gets its
type's defaults. `FACEUIElement` is a single struct, so a factory's job is the behavioural flags:
`bActivatable` (click target) and `bAcceptsFocus`. `Text` and `Viewport` are not click targets —
an activatable Viewport would block world picking through `PortalSpace` and eat paperdoll drags.
Types with no factory fall back to `Field`.

`IsInteractiveHitTarget` consults `bActivatable` first, which is also how a binder disables a
control (`Btn->bActivatable = bLit` on an unaffordable Raise button).

## Paint cost

`SyncElementWidgets` walks the visible tree each tick, but `ApplyPaintState` caches the last
values pushed to each `UBorder` (`FACEUIPaintState`: texture, rect, Z, tint, visibility) and only
calls Slate setters on change. Steady-state ticks therefore allocate no brushes and trigger no
layout invalidation. The brush carries an explicit `ImageSize`, so a size change re-sets it, and
`SetBrush` resets the tint — the cache invalidates the colour to match.

## Image draw modes

LayoutDesc `drawMode` on an image node is honoured in the paint path:

| drawMode | Meaning | Slate brush |
|----------|---------|-------------|
| 1 | Repeat the source art across the element — panel slabs (`0x06004CC2` over 300×600), window borders, bevels, edge strips | `Tiling = Horizontal / Vertical / Both`, `ImageSize` = texture size × UI scale |
| 3 (and everything else) | Stretch to the element rect | `NoTile` |

Tiling only engages when the element is larger than the texture in that axis, so nodes whose
art is authored at the element size still draw 1:1.

## Interaction rules that mirror retail server behaviour

- **Vendors take exactly one Use per interaction.** `ActOnUse` queues `LoadInventory` then
  `ApproachVendor(Open)` behind the vendor's rotate time, so `OpenVendorGuid` lags the send.
  The approach tick sends Use once (`ApproachUseSendCount`) and then only holds pose; resending
  in the rotate window queued one Open emote per send and the vendor greeted several times.
  `Vendor.ActOnUse` also downgrades the emote to `VendorType.Undef` when the panel is already
  open for that player.
- **Give never sends Use.** Dropping an inventory item on an NPC sends `GiveObjectRequest`
  (`0x00CD`: targetGuid, itemGuid, amount) and starts a move-only approach. `bApproachMoveOnly`
  keeps the MoveToPosition echo and the re-close path from flipping the approach into a Use —
  a Use runs `StopExistingMoveToChains` on the server and cancels the pending give with
  `ActionCancelled`.
- **Refusal text.** Retail string templates use a bare `_` as the name placeholder
  (`"_ doesn't know what to do with that."`); `LookupWeenieErrorWithString` substitutes it.

## Still to do (gm*UI behavior)

- Native language-DAT unpack in `UACEDatSubsystem` (skip JSON extract step).
- Typed widget behaviour beyond flags (Meter fill from Media, native Scrollbar / ListBox).
- 3D viewports for examination paperdoll and barber.
- World map texture on `WorldPanel_Field`; Options page bindings.
- Char create / delete / restore in `UACEUICharSelectBinder`.

## Gameplay binding (`UACEUIGameplayBinder`)

Wired from toolbar ElementNames to ACE GameActions / session state:

| UI | Network / data |
|----|----------------|
| Inventory / panel buttons | Show `FloatyPanel` page; inventory from `GetPackItems` / `GetEquippedItems` |
| Examine / Use | `SendIdentifyObject` / `SendUseItem` (+ approach) |
| Peace / Melee / Missile / Magic | `SendChangeCombatMode`; show `FloatyCombatPanel` in combat |
| Side vitals meters | `OnVitalsUpdated` / `GetPlayerVitals` |
| Main chat | `SendChatMessage` / `OnChatMessage` |
| Appraisal | `OnAppraisal` → show `FloatyExamination` + summary text |
| Shortcuts 1–18 | `GetShortcutObject` → `SendUseItem` |
