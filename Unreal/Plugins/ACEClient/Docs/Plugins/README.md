# Client plugins - API 2

AC:Unreal hosts portable Lua plugins. The same source and native management panel
are used on Windows, Linux, PC VR and standalone Quest. This is a new API, not a
loader for Decal DLLs or Virindi View Services assemblies.

## Client chat commands

Commands specific to AC:Unreal use the `acue` prefix. Type `/acuehelp` for the
list: `/acueperf` (performance report), `/acueobject` (selected object report),
`/acueplugins` (Plugin Manager), `/acueplugin <id> <command>`, `/acueucm`, and
`/acueitems`. Chat also accepts `@` in place of `/`.

Use these names in new Lua plugins, meta commands, examples, and help text.
The older `/aceperf`, `/aceobject`, `/plugins`, `/plugin`, `/ucm`, `/items`, and
`/it` spellings remain compatibility aliases for existing tools and saved metas.
Retail commands such as `/fps` and `/saveui`, and server commands such as
`/acehelp` and `/myquests`, keep their original names.

## Users

**Action Deck** is an optional bundled desktop hotbar. Enable it in Plugin
Manager and open the gold tiles / teal spark icon. Drag inventory items or
spellbook/spell-bar icons into its three rows of twelve slots. Click an icon
to use it, drag between slots to swap actions, or right-click to edit a slot.
The icon follows the pointer above other windows while dragging. Drop outside
the Action Deck window to remove the shortcut; dropping on its toolbar or
settings leaves it in place. Binding or removing a shortcut does not move or
destroy the inventory item. Keys stay with their slots when actions move.
Icons use the retail item/spell compositor, including item underlays and
enchantment effects, spell power backgrounds and target badges, without raw
white mask outlines.

Turn **Override** on to use deck hotkeys in place of the native spell and item
hotbars. Bottom-row defaults are `1` through `=`, middle-row defaults add Ctrl,
and top-row defaults add Shift. Empty and unavailable slots reserve their keys
too. Typing and keybinding editors receive their input normally. Action Deck
settings pause deck actions while keeping its keys reserved, so editing cannot
accidentally use a native hotbar item. Closing the deck or disabling the plugin
restores native bindings.

The gear opens row-modifier and per-slot key settings. Individual key overrides
keep their modifiers when a row changes. Duplicate chords are rejected.
Settings can also assign low, medium or high melee/missile attacks, clear a slot,
or restore default keys. **Lock** pins the position and hides the window frame;
the Override and Lock controls disappear while pinned. The gear remains visible;
use **Unlock window** in settings to restore the frame and controls. Window
placement follows the normal plugin layout, while actions, keys, Override and
display options save separately per character/server.

Settings can hide the native spell bar and melee/missile bar independently while
Action Deck is open and enabled. Closing or disabling it restores the appropriate
native bar. Hiding bars does not change stance or enable hotkey Override.

**Show target debuff and damage-over-time timers** displays estimated remaining
effect time over spell slots for the current selection. A translucent fill drains
over the whole button; the timer shows seconds or minutes:seconds. Timers start
only after matching server confirmation of your cast, including manual and UCM
casts. Resists and fizzles do not start them. Changing targets shows that target's
effects; death, leaving awareness, and expiry clear them. A confirmed stronger
spell in the same family covers lower-tier slots. Timers never prevent recasting.
Duration comes from known spell metadata; other players' casts, early dispels and
server-specific duration adjustments cannot be inferred from these confirmations.

Item slots search all owned packs, prefer the originally bound item, then use a
replacement with the same item class, name and type. Depleted or missing items
stay grey until a replacement is available. Healing kits use a selected living
player when compatible, otherwise self, without changing selection. Other items
use the native equip/use/targeting flow, including confirmations. Spells require
confirmed magic stance; attacks require their corresponding confirmed stance.
Busy actions and equipment changes temporarily disable slots. Hover a slot to
read its name, full key binding, and the reason it is unavailable.

**Third Eye** is an optional bundled spawn watcher inspired by
[Virindi Sense](http://virindi.net/wiki/index.php/Virindi_Sense). Enable it in
Plugin Manager and open the eye icon. On **Setup**, enter a name or use **Add
selected**, choosing exact name or partial name matching. Matching ignores letter
case and treats punctuation literally. Entries can be disabled or removed.
**Detected** lists current matches nearest first, with distances, click-to-select,
and a Waypoint arrow button. Search and page through crowded results; long watch
lists are paginated too. Page controls stay compact and page sizes adapt to the
window height, without a scrollbar. Range, alerts and markers live on **Options**.
Long row names stay on one line; hover to read the full name. It uses the nearest 512 selectable objects already known to the
client, within the same area and at most 240 metres; it cannot discover distant
server spawns. The panel reports when that awareness list is truncated.

Green local chat alerts announce new matches and combine large bursts. A short
absence does not repeat the same alert. Optional world and Waypoint-map markers
show up to 32 nearest matches. **Watch for matches** pauses detection, alerts and
markers while retaining the editable list. The list, range, alert and marker
preferences save per character and server. Third Eye runs alongside UCM, starts
when enabled on login, and continues with its window closed. It never moves,
attacks, appraises, or sends chat to the server. Disable it in Plugin Manager to
remove its icon and stop it.

**[Auto Craft](AutoCraft.md)** is a separate plugin with searchable category,
item and recipe selectors, quantity or Max available, ingredient preparation,
and Make / Pause / Stop controls. Enable it in Plugin Manager and open its
mortar-and-pestle icon. The bundled production catalog covers 4,744 output items;
preferences are saved per character and server.

Plugin windows remember whether they were open or closed at logout, including
custom Lua windows and the UCM log. Their positions and sizes are restored too;
opening a window does not start its automation. Disabled plugins stay hidden.
**Options > Config > Keep UI windows on screen** applies to both game and plugin
windows. It is enabled by default. Disable it to allow offscreen placement;
enable it again to bring windows back inside the game interface.

**Plugin Manager > Background opacity** sets the default for plugin windows with
a slider and percentage readout. Drag to preview; release to save.
Check **Use this opacity for all plugins** to override every individual value, or
leave it unchecked to use each plugin's own value. Each plugin card has an opacity
control and **Use plugin/default** to clear your override. Text, icons and controls
stay fully readable. Changes update open windows without closing their menus.
The header's **Pin** button (also available in Plugin Manager) hides the title bar
and border and locks the window in place. Background opacity applies whether the
window is pinned or unpinned. Quick-show temporarily restores the frame without
changing the background. Window sizes can be reduced for compact HUDs.

**Supply Ledger** is a compact inventory counter inspired by Virindi's Comps HUD.
Select an owned item and click **+** to track its item type across all packs,
including equipped stacks. Each row has its own **-** button, so an out-of-stock
item can still be removed. Lists are
saved separately for each character and server. Zero-count entries remain visible.
Each row uses the selected item's inventory icon, including its overlays and
item-type background. Its artwork is retained when the item runs out.
Click any row's name or icon to show or hide all item names together. Counts stay
visible in the same position; resize the window to bring them closer to the icons.
Hover an icon to read its item name. This preference is saved with the character's
supply list. An empty ledger keeps its **+** add control visible even when pinned.

**Hunt Ledger** shows XP and luminance earned in the last five minutes, the average
per hour, and totals since login or **Reset run**. Hourly rates use actual elapsed
run time; the first few seconds are naturally less representative. Spending XP or
luminance does not subtract from earned totals. Luminance at the server's cap
cannot be counted unless the server reports a gain. Closing the window keeps the
run tracking. Logging into a character starts a fresh run.
The **Next level (est.)** line below the totals uses remaining level XP and the
average XP rate for the current run. It waits for XP gains after a reset and
shows **Max level** at the level cap.

Both ledgers work independently of UCM. The game radar lock/unlock icon controls
all plugin windows. Locked ledgers hide their title bars and editing controls.
The visible contents stay at the same screen position when the header is hidden
or restored, including when a window is lined up near the bottom of the screen.
Hold **Left Control** to temporarily show those controls and move pinned windows;
release it to restore the lock. Rebind **Quick-show pinned windows (hold)** in
Keyboard settings. Drag Supply Ledger's bottom-right corner while unlocked to
resize it. Its width and list height survive pinning and logins, along with position,
visibility and the permanent UI lock.
Enable or disable the ledgers
in **Plugin Manager**. Five-minute history uses a fixed-size buffer.

The plugin bar uses original 26-pixel vector emblems; hover for the full plugin name and
status. Its **gear** opens Plugin Manager. Its grip still moves when the UI is unlocked, and its minus button
collapses it to a line. Hide or restore the bar with **Show plugin bar** in
**Settings > Game Play > Plugin Manager**. In the same manager, **Horizontal plugin bar**
switches between a horizontal row and the default vertical column. The orientation
is saved across logins and keeps the icons square, including when the bar must scroll.
Drag an icon to reorder it, or drag the six-dot grip to the left of a plugin's
icon in Plugin Manager. A gold insertion line marks the drop position. Both views
share one saved order; changing it does not change plugin execution order. Unlock
the UI or hold Quick-show before dragging. Plugin Manager itself uses the same
movable, resizable, pinnable frame as other windows, and remembers its position,
size, pin and open/closed state across logins.
Plugin windows share retail DAT bitmap labels, inventory
artwork, dark backgrounds, gold borders and red buttons. Text entry retains Slate's
native caret, selection and IME behavior. Standalone windows use their title bar
for the plugin name, including UCM, Loot Profile Editor, Item Tools and Waypoint;
the content begins with status, controls or help instead of a repeated heading.
Action buttons and tabs center their captions horizontally and vertically, with
equal insets on all four sides. Compact and title-bar controls use smaller equal
insets; pressing a button keeps its caption in place. Dropdowns retain their
green artwork and use the same spacing rules for their selected captions.

UCM's main activity section starts expanded in Buffs, Buff others, Combat and
Recovery; secondary sections start collapsed and keep your expansion choices while
the window is open. Buff exceptions are edited directly inside their section.
Combat groups targeting, melee hands, spellcasting, summons and equipment separately.
**Monster defaults** provides fallback priority and range for rules without overrides;
a zero range uses the normal melee or missile/spell distance.

Each bundled plugin has its own emblem (tapers for Supply Ledger, an hourglass
and rising chart for Hunt Ledger). Icons retain square proportions at every UI
scale. Third-party plugins can supply a PNG icon and a display title through
[plugin presentation metadata](PublicAPI.md#plugin-title-and-toolbar-icon); an
extension sigil is used when no valid custom icon is available.

Waypoint map zoom is saved separately for indoor and outdoor views, both in the
pinned minimap and the full map window. Closing a map or relogging restores its
zoom along with saved plugin positions, sizes, visibility, opacity and pin preferences.

**Item Tools** provides bulk giving, vendor buy/sell, GiveVT-style profile
transfers, keyed chest collection/distribution, and character-specific supply
restocking. See [Item Tools](ItemTools.md) for setup and commands.

**Waypoint** adds clickable chat coordinates, a movable navigation arrow and a
world/dungeon map, with optional GoArrow atlas import. See [Waypoint](Waypoint.md)
for controls, data sources and the UtilityBelt compatibility findings. This visual
plugin requires no Start action or game-action permissions.

Open **Plugins** on the login screen, **Game Play → Manage Plugins** in desktop
options, **More → Manage Plugins** in VR, or type `/acueplugins`. Each plugin shows its
version, requested actions, status, and profile controls. Enable grants its listed
capabilities. Gameplay automation uses a separate Start action after entering the
world. Local cosmetic services start when enabled and can run at the login screen.
A capability change requires enabling again.

**Open plugin folder** opens the exact `ClientPlugins/Installed` directory used
by this client. Copy a plugin's folder there and press **Refresh** in the manager.
Refresh stops old instances and reloads their manifests and scripts.

### Reusable UCM profiles

Open **Overview → Configuration** to select independent profiles and enable
their activities. There is no named Setup to create or save. The current six
selections and activity switches persist in `Saved/ClientPlugins/ucm-selection.json`.
Reusable files live under `Saved/ClientPlugins/Profiles`:

- `Character`: combat equipment, targeting defaults, buffs, summons and recovery.
- `Routes`: waypoints, following, looping and route movement settings.
- `Loot`: loot rules, salvage groups and looting settings.
- `Monsters`: per-monster rules and the imported monster catalog.
- `Metas`: state programs, imported metas and dependency references.
- `Vendors`: vendor supply lists.

Keep a Character profile such as `PlayerNameVOID` selected and swap the Route or
Meta for the next hunt. Edits save to the selected file. **New** and **Delete**
are in each editing tab; new routes start empty. Deletion retains a backup in
`Profiles/Deleted` and switches affected selections to Default. Default files
cannot be deleted. Activity toggles remain independent of the selected files.

**Import** has separate **Import vTank Loot Profile (*.utl)** and
**Import vTank Route (*.nav)** buttons, plus Meta and Character imports.
Files come from the configured import folder (initially `ImportInbox`). Refresh
after adding files. Imported profiles use the source filename with a suffix for
name collisions. Original files are unchanged. Meta dependencies and embedded
routes become named profiles, including empty routes used to clear navigation.
The meta selects them when its rules execute; Overview follows those changes.
Editing a dependency or exported route updates subsequent use of that profile.
Embedded routes are refreshed from their named files when the meta is loaded
or UCM starts. Stop/Start retains meta progress when the configuration is unchanged.

Protections and Banes are in **Buffs**, saved in Character. **Monsters** offers
name search and collapsible panels. New rules inherit the Character profile's
**Combat** defaults for Priority and Attack Range. Both use whole-number sliders;
uncheck **use Combat default** to override a monster. Explicit zero priority is
valid. A global attack range of zero uses the weapon-mode attack range.

Selecting a profile manually stops the run. Start reloads shared files from disk;
conflicting stale edits are rejected. Older setups migrate automatically while
preserving the last selection and all independently saved profiles. Original
monolithic data is backed up under `Profiles/Legacy/ucm`; old manifests remain
available for compatibility but no new named setups are required or created.
Legacy `LootProfiles` files are copied into `Profiles/Loot` on first use.

### Local cosmetic services

The Windows client includes **local cosmetics API 1** for independently distributed
Lua plugins. The API handles local weapon/shield rendering, validation boundaries,
plugin controls and cleanup. Providers supply their own Lua, manifest-defined
presets and bundled converter. See [Local cosmetics API](LocalCosmetics.md) for
the manifest, companion protocol, mesh format and a source-independent example.

Local providers declare `mode: local`, `service: local_cosmetics`, `service_api: 1`
and both `local_cosmetics` and `local_tools`. The manager exposes the native-tool
capability when enabling; companion programs run outside the Lua sandbox. No
filesystem, native-loader, network or arbitrary process API is added to Lua.
These services start when enabled, run alongside UCM, and restore original
rendering and cancel pending work when disabled or reloaded.

**AC-WoW Transmog** is a separate optional download using that API. The client
release does not include its Lua package, WoW presets, converter, CASC reader or
game files. Other authors can target the same released API without adding native
Unreal modules. The current API supports rigid weapons/shields and compatible
rigid or skinned body-slot replacements on Windows x64. Quest companion-tool
execution is not supported.

In desktop play, a compact **Mods bar** appears at the left edge. Each enabled
plugin has a button: click it to show or hide that plugin's own window. Several
windows can stay open together. Gold text indicates an open window; `RUN` marks
a running plugin. Closing a window does not stop its plugin. Use its Stop control
or `/acueucm stop` to stop automation. The `+` button opens plugin management.

Drag the bar or a window by its title strip. Positions are saved locally and
clamped to the viewport when the window size changes. The bar and its windows
hide with the desktop interface (Alt+Z by default) and are removed on logout.
Disabled plugins are removed from the bar. In VR, use More → Manage Plugins.

UCM is **Unattended Combat Manager**. The 0.7 interface has Overview, Buffs, Buff others,
Combat, Recovery, Route, Loot, Vendors, Monsters, Metas, Import and Log pages. Desktop plugin
windows can be resized using the lower-right grip; position and size are saved.
In VR, More → Manage Plugins opens UCM directly, with a separate management button.

### Display and controls

**UCM Log** is a separate movable, resizable desktop window. Open it with the
**LOG** button on the plugin bar, **Open UCM Log** in UCM Micro, or
`/acueucm log`. The **Log** page inside UCM also works in VR. Closing the log does not
stop UCM or recording. Window position and size are saved.

The log keeps the latest **1,000 events**, replacing the oldest. It records local
timestamps, activity changes, route recovery attempts, pauses and errors, with
the Character and Route profiles, waypoint and location where available. Unchanged polling
statuses are skipped. **Problems only** filters warnings/errors; **Copy log**
copies the visible history. Scroll back to pause following, or enable **Follow
latest** to return to new messages. The latest problem remains visible even if
its original entry has rolled out of the history. **Clear** clears both.

History is saved every ten seconds while changed, and when UCM stops, to the
single bounded file `Saved/ClientPlugins/ucm-log.json`. It replaces that file
instead of appending forever; a crash can lose the last ten seconds. No chat
transcripts or credentials are recorded.

Loot Editor and other plugin text fields keep typed keys out of gameplay actions.
**Keyboard > UI > Show / hide interface** is rebindable; replace Alt+Z there if
NVIDIA or another application captures it.

In **UCM > Overview**, **Suspend 3D view while UCM runs** disables the desktop
world draw while keeping the interface, simulation, and networking active. Turn
the option off or stop UCM to restore the view. Logout also restores it. This
does not disable the engine or apply in VR; simulation and UI still use resources.

In **Combat**, select **Automatic attack spell** or a specific learned direct
attack, including Void bolts, streaks and rings. The **Void curses** controls
enable Corruption, Destructive Curse, Corrosion, Weakening Curse, and Festering
Curse. UCM uses eligible learned tiers and waits for effect confirmation before
moving on to direct attacks. Per-monster rules can override these settings.

### UCM Micro, combat and route recovery

**UCM Micro** is a compact companion window on the plugin bar and in plugin
management. A full-width **Start UCM / Stop UCM** button controls the run.
Its checkboxes control the current activities:
buffing, combat, looting, navigation, vital recovery, buff requests, vendor
restocking, metas, and peace while idle. Closing Micro does not stop UCM.
UCM still needs its normal permissions; Micro grants no additional capabilities.

Micro also has **F** (Force Buff), **CF** (Cancel Force Buff), **FC** (Follow
Character), and **C** (Corners). Select another player before enabling FC, then
start UCM. Disabling FC also disables navigation so an old route cannot take
over unexpectedly. Corners follows observed positions in order, including
positions received during casting/equipment waits; it does not skip ahead to a
nearby later segment. The same follow controls are available on the Route tab.
Only positions known to the client can be followed; losing the target pauses
following, and an overflow of the bounded movement history pauses navigation.

The fixed-size **Current status** area shows the pending spell, equipment
switch, or wait condition without resizing Micro. Hover for the full text.
The main UCM window shows the same status. Missing cast confirmations have a
bounded timeout; a missing final buff acknowledgment retains and revalidates
the queued next action instead of silently discarding it.

Both manual and UCM launcher changes stow incompatible equipped arrows,
quarrels, or darts before wielding the replacement. Each removal waits for
server confirmation, and matching ammunition stays equipped.

In **Keyboard Layout → UI**, bind **UCM Start / Stop** to a keyboard, mouse,
or controller button. It is unbound initially to avoid replacing an existing
control. The binding is saved in local input settings. It is not exported to
retail keymaps, which have no equivalent action.

Combat automatically prefers the highest usable trained combat skill, then
ranks eligible equipment, ammunition and spells for that skill against the
target's resistances. If that loadout is unavailable, it can use the next usable
skill. Explicit imported monster-rule weapon/mode overrides are preserved.
Attack height uses the target's DAT selection origin, server scale and elevation.
Advanced file profiles may retain `manual_combat` or `manual_attack_height`
overrides; the normal interface does not require either choice.

After a combat/loot detour, navigation retraces sampled positions before
resuming its ordered route. Joining skips nearby points behind scenery or closed
doors; ordinary route traversal still opens configured doors. Blocking allows
bounded recovery and target avoidance, then stops if the return path cannot be
followed. This is recorded-route following, not arbitrary dungeon pathfinding.

### Buffing and recovery

Enable **Buff**, then Start. Automatic selection includes relevant known Self
creature/life buffs and item enchantments, using the highest tier within the
configured magic-skill margin. The Buffs tab has separate Creature, Life and Item
Magic selectors: **Automatic highest** (default), or an exact level I–VIII.
Explicit tiers still require the learned spell, sufficient skill and components;
unavailable families are skipped. These settings also apply to buff requests,
while recovery independently chooses its highest usable spell. Automatic skill
buffs require training, as in vTank. Use **Include** under Buff exceptions to
add an untrained skill or another sustained buff family explicitly. Fellowship
spells require explicit inclusion and membership in a fellowship.
Self-buffing targets the player once per item-spell family: banes and
Impenetrability cover worn armor, and weapon auras apply to the character.
It does not repeat those casts on inventory items or weapons in the combat pool.
Optional family exclusions and legacy explicit buff lists remain available.
The host equips a usable casting tool before casting, respects existing buffs,
and waits for server action completion. Three failed/unconfirmed attempts skip that buff family for two minutes
and continue the cycle; no cast is treated as successful merely because it was sent.
A server missing-components response excludes that tier temporarily and tries a
lower supplied tier. If no tier remains, UCM continues with other families. Force Buff reports
when unavailable or failed buffs were skipped instead of claiming full success.

New Character profiles refresh five minutes before expiry; existing profiles keep
their configured **Rebuff before expiry** value. Once a buff starts, the refresh
window widens by 30 seconds for 30 seconds to group nearby expirations, as in
vTank. Short-duration buffs and consumables cap the window at half their duration
so a fresh buff does not immediately become due again. **Refresh buffs while idle**
is a separate, optional earlier top-off and is disabled by default.
Character buffs use timed server enchantments, including those received at login;
permanent equipment enchantments do not satisfy the rebuff plan. This lets UCM
finish casting before replacing its buff wand with combat equipment, instead of
repeatedly switching when the wand's enchantments disappear on unequip.
Confirmed item-only buffs retain character/server-scoped timers across relogs.
Force Buff explicitly ignores remaining times. A new buff-other request casts
the requested set once; duplicate queued requests do not restart it. Other-player
casts use the buff skill buffer and family/protection/bane exclusions, and need
server confirmation before advancing. The client cannot inspect another player's
complete active enchantment registry, so it does not assume their existing buffs.

Health, stamina and mana each have a recovery slider. UCM prefers eligible Life Magic recovery spells,
then assessed supplies (including healing kits and potions). Enable **Prefer kits
and consumables** on the Recovery tab to reverse this order. Unavailable or failed
methods fall back to the other kind; failed methods cool down for 15 seconds.
Changing this toggle overrides imported recovery order and saves with the Character profile.
The status below it shows the active preference; **Use imported recovery order**
can explicitly restore the imported priorities. Stamina to Mana is allowed only with sufficient
stamina. It observes replicated vital increases and server action failures before retrying.
Rejected recovery spells temporarily yield to other tiers or supplies.
Filled mana stones/charges can replenish low equipped-item mana. Empty supply
pools permit automatic selection; adding supply types restricts the pool.
Assessments are refreshed periodically, so equipment mana is not instantaneous.

**Automatically fill empty mana stones** picks up otherwise unwanted magical
corpse loot when **Enable Looting** is on; it needs no source-item pool or loot
profile. Normal loot actions take precedence. Like vTank, Skip/NoLoot and items
beyond a KeepUpTo limit can supply stones. The item must have workmanship,
positive maximum mana, and at least **Minimum mana in source loot** (1,000 by
default; imported ManaTankMinimumMana is honored). Tinkered, inscribed, retained,
equipped and configured equipment/supplies are excluded. UCM takes one donor per
empty stone, confirms the pickup, and rechecks its identity and protections before
using the stone. It never scans pre-existing possessions for destruction; the old
manual source list is ignored. Stopping/restarting UCM abandons pending donor jobs.


### Combat and items

Choose melee, missile, magic, or auto. Inventory appraisals provide requirements,
damage, speed, modifiers, slayer and element data. Individual weapon IDs can be
added to an equipment pool; an empty pool permits all eligible inventory weapons.
Ammunition is matched to the launcher and assessed damage. Equipping uses ordinary
server actions and waits for the equipped state. Private/custom requirements that
are unavailable to the client are not guessed.

A monster's explicit damage type is a requirement for its attack loadout.
UCM chooses a matching melee weapon, spell, or launcher ammunition from the
eligible equipment. An incompatible saved weapon or global spell preference
cannot override that element. Incompatible melee offhands are replaced or
removed; if no matching loadout is available, UCM reports the failure instead
of attacking with the wrong element. Door navigation uses the server's current
collision state, including doors that automatically close after being used.

Known attack spells and equipment are scored against resistances **when the
server sends them**. This is a heuristic, not an exact DPS simulation: it does not
model every proc, imbue, critical, armor part or projectile trajectory. Many
servers do not expose base monster resistances. UCM uses neutral values in that
case; Rules lets the player specify an element, combat mode, ignore flag and
priority per monster-name prefix. First matching rule wins. It does not bundle or
copy VT's monster database. Acquisition range, approach distance, attack range,
power/accuracy and attack height have separate controls.

### Navigation, loot and states

**Buff others** accepts `mage`, `heavy`, `finesse`, `light`, `unarmed`,
`twohanded`, `melee` and `missile` by default. Keywords are editable; existing
custom keyword lists are preserved. All melee requests include Dual Wield,
Dirty Fighting, Recklessness, Sneak Attack and Shield. `melee` includes all four
modern weapon skills; `unarmed` uses Light Weapons, as in modern retail.
Requests share a FIFO queue of up to eight players. UCM tells each requester their
position, updates it when someone finishes, announces their turn when their buff
task starts, and reports completion or cancellation. Duplicate requests do not
restart a cycle; repeated status replies are rate limited. The host sees names,
roles and waiting/buffing state on Buff others.

Combat's **Melee hands** controls select default main-hand and offhand equipment.
Monster rules can override the defaults. Without an offhand override, a weapon's
server-provided Left-hand Tether flag reserves it for the offhand. Automatic main
hand selection excludes tethered weapons, including live tether changes.
An explicit Shield or Empty hand setting overrides automatic offhand selection;
two-handed loadouts do not equip a second weapon. Profiles store the defaults as
`melee_weapon` (-1 automatic or item GUID) and `melee_secondary_equip` (omitted:
tether/keep current, 0 automatic, 1 shield, 2 dual wield, 3 empty, or item GUID).

**Force Buff** on Overview or Buffs immediately refreshes the enabled self-buff
families, ignoring their remaining timers. It retains spell eligibility, skill
margin and exclusions, and counts a family only after a successful cast is
confirmed. Repeated clicks do not restart the cycle. **Cancel refresh** cancels
the remaining queue; an already submitted cast can finish. When started from
stopped UCM, this runs buffs and configured recovery only, then stops. An existing
run resumes its activities afterward. Imported `/vt forcebuff` uses the same
refresh policy. The request is temporary and is not saved in profiles.

Record close, walkable points at corners. A live world-space line connects the
points; amber marks jumps. Add portal/object-use points while that object is
selected. Add a destination point after actually reaching the far side. Recall
spells and timed pauses are also available. Jumps record charge and facing and
use the normal player jump pipeline; movement never sets pawn location directly.
Outdoor paths can cross landblocks. Interior transitions between separate
landblocks require a portal/recall action. Individual walking segments are limited
to 400 meters. Five seconds without progress stops movement. This is recorded-route
following, not a navmesh pathfinder or automatic obstacle planner.

Starting UCM joins the nearest route waypoint using the character's world position
and height, independent of camera direction. It then follows the remaining points
in order, retaining loop/reverse behavior. Loading another route selects a new
entry point. Imported action-only entries are not spatial join targets; leading
setup actions are preserved when joining the first spatial point. Native points
in a different dungeon are excluded. Record enough corners to reach the nearby
route safely; nearest-point joining does not find a new path through walls.

Native and imported walking points share the same steering and 3D arrival
radius. On ramps, steering continues until the slope distance is inside that
radius. The route overlay samples successive floor heights every 50cm instead
of tracing the straight chord between waypoint heights; reachable-entry checks
follow that floor too. Loaded dungeon physics meshes remain available for these
queries when their rendering/collision is culled. Gaps and different floors do
not become walking connections. Record corners and explicit jumps/portals as
usual; this does not invent a route around walls or across voids.

Combat rejects monsters behind physical walls, scenery and closed doors, then
continues the recorded route. Open doors along route is on by default; explicitly
disabled profiles keep that setting. Record corners and door approaches so the
route leads around obstructions. Combat settings separate target acquisition,
missile/spell firing distance and melee approach distance. Monster Rules can
override attack distance (0 uses Combat settings). Sight is rechecked before
each attack or offensive cast, using scaled setup heights for the target.
Enable **Pursue targets outside attack range**, beside the acquisition slider,
to leave the route for a visible monster and return after combat. Acquisition
selects the monster immediately, before entering firing range. A positive
acquisition distance is the search limit even when firing distance is larger;
imported VT `ApproachDistance=0` retains stationary attack-radius selection.
The Combat page shows the live effective search distance and target, including
when pursuit is disabled or route priority prevents chasing. This also exposes
range overrides from a meta or activity state.

**Peace mode when idle** (Overview or Combat, off by default) switches to peace
when no eligible combat target is nearby, including during route following.
Buffing, recovery and corpse looting finish first. Combat resumes in the configured
mode when attacking a target; magic casting automatically restores magic mode.

Corpse opening uses the same approach and server Use sequence as manual
interaction. UCM waits for opening, complete contents and transfer confirmation
before resuming combat or navigation. Navigation priority does not suppress
nearby looting, and obstructed corpses are skipped until the route reaches them.
Enable Loot corpses and load or create matching rules; empty rules intentionally
take nothing.

After a confirmed combat kill, UCM holds ordinary route movement while the
server finishes the death animation and creates the corpse (up to eight seconds).
New nearby corpses are handled immediately through the normal loot rules, then
UCM resumes its existing return path. Recovery and combat remain available during
this wait. **Prioritize navigation** bypasses the spawn wait; disabled looting,
full inventory, out-of-range deaths and teleports do not hold the route.

Loot rules are configured in **Loot → New rule**. Give the rule a name, choose
**Keep** or **Skip**, then set any combination of:

- Item name: exact, begins with, or literal contains; case insensitive for ASCII.
  Names use the displayed material prefix, for example `Copper Bracelet`.
- Material selected by name and an optional item-type mask.
- Inclusive minimum/maximum salvage workmanship, including fractional values.
- Inclusive minimum/maximum value and burden for the whole item/stack.
- Damage rating range, which requires appraisal.
- Keep up to a quantity per weenie type (counts include owned packs/equipment).

Zero disables a numerical bound; zero quantity means unlimited. All conditions
in a rule must match. Rules run top to bottom: the **first match wins**, including
Skip or an already satisfied quantity cap. Unmatched items remain on the corpse.
Use **Edit**, **Enable/Disable**, **Up/Down** and **Remove** on rule cards. Changing
rules stops UCM; press Start to use the new set consistently.

For example, a Keep rule for Copper, workmanship 7–10, collects suitable copper
items for later salvaging. A Skip rule for an exact item name above it protects
that item from broader rules. A Keep rule with minimum value 10,000 and maximum
burden 100 collects lightweight valuables. **Use selected item's name and
material** fills those fields without memorizing IDs; clear the name to collect
all items of the material.

**Save loot profile** stores reusable loot settings under `Saved/ClientPlugins/Profiles/Loot`. Vendor lists are independently linked under `Profiles/Vendors`.
Loading one changes the current Loot selection without replacing combat,
buffs, recovery or navigation. Loading stops UCM. Every use of the same shared
file sees subsequent edits. Loot-profile files are versioned UCM JSON, not VT
`.utl` imports. Existing UCM prefix/minimum rules remain compatible.

Public server workmanship is retained from Create/UpdateObject, including
fractional combined-salvage values. Public name/material/value/workmanship checks
run before appraisal. Missing necessary details trigger bounded background ID
requests; absent properties after successful appraisal do not match. Corpse
item descriptions and loot transfers must be observed before proceeding, and
quantity limits can request a partial stack. Rule scans use actual instruction
and regex work, continuing at the current rule, condition, or item spell when
the callback is nearly full. The status shows item/rule progress, and stable
item ordering prevents snapshot reordering from restarting the scan. Other open containers are not
looted. Empty rules never open corpses or take items. Optional stacking uses the
normal compatible-stack merge. This version collects salvage candidates but does
not automatically salvage, sell, destroy, read or drop them.

Behavioral references: VT's [loot tutorial](https://www.virindi.net/wiki/index.php/VTClassic_Tutorial)
and [advanced looting notes](https://update.virindi.net/wiki/index.php/VTClassic_AdvancedLooting),
plus the supplied `eLootAction`/`LootPlugins` interfaces. The public-workmanship
optimization follows VT's distinction between public and appraisal-only fields.
Full arbitrary-property/spell-expression matching, VT profile import and the
additional salvage/sell/read/mana actions remain outside this implementation.

Metas provides a native state editor: name states, choose a starting state,
override activities, and add ordered transitions. Current conditions are vital
thresholds, elapsed time, target availability and route completion. This is a
bounded UCM rule system, not the full VT expression language.

Independent profiles contain these settings. Select them from Overview or
their editing tabs. Activity/range/threshold edits are live. Loading a profile or editing routes
and state rules stops the run. Manual movement overrides plugin steering without
stopping UCM or resetting its activities. Turn Combat off and Loot on to fight
manually with automatic looting. Steering can resume on the next plugin decision
after manual input ends. Stop UCM through its interface, UCM Micro, the bound
Start / Stop hotkey, or `/acueucm stop`. Death, logout, character changes, disabling
and reloading still stop automation. Closing its window does not stop it.
Only one plugin can run at a time. Commands `/acueucm start` and `/acueucm stop` remain.

## Automatic vital recovery

While UCM is running, Recovery restores health, stamina and mana below their
configured percentages before starting another combat attack. Known spells use
current magic skill and the shared skill buffer; server component exemptions
remain honored. Retail Heal Self uses category 67, so recovery distinguishes it
from the timed Healing skill buff. Tier VII conversion names and tier VIII
Incantations are recognized alongside the lower tiers.

Mana recovery prefers a usable Stamina to Mana spell over Mana Boost, even if
Mana Boost is a higher tier. Revitalize restores stamina once it falls below the
configured threshold; health retains first priority. Low stamina, unavailable
spells and the selected supplies-first preference still control fallback.
Imported Regular Spell handlers use this same mana preference.

Unknown healing kits and food/potions are appraised automatically when no ready
method is available. Selected supply restrictions and imported handler order
remain authoritative. Turn off **Use imported recovery order** to switch an
imported setup to UCM’s spell/consumable preference. The client confirms recovery through replicated vital
changes, even when UseDone arrives separately. If no method is available while
idle, UCM reports that instead of displaying a generic Ready status.

## Loot editor: imported rules and original values

The Loot page uses the Classic editor's rule-list/detail workflow. Choose a rule
on the left to edit its name, action, and requirements on the right. New, Clone,
Delete, Move up, and Move down act on the selected rule; the highlighted row shows
which rule you are editing. Search and paging keep large imported lists usable.
Narrow windows stack the list above the editor instead of squeezing the fields.

Select a requirement to edit its property, comparison, and value. New, Clone,
Delete, Up, and Down manage requirements. Apply requirement accepts that draft;
Apply rule saves the rule and stops UCM. Discard changes restores the saved rule.
Changing selection cannot silently discard edits. The rule list remains visible
while editing, and Apply/Discard stay outside the scrolling detail fields.

The active profile is shown above the editor. Open / save profile, Salvage
combination, and Looting settings are separate from rule editing. Summaries show
named properties and IDs, exact thresholds, include/exclude patterns, and whether
appraisal is needed. Basic filters remain summarized when collapsed. Rule order
is first-match order, and every requirement in a rule must match.

Property selectors search Classic's integer, decimal and string keys, plus ACE
server keys. Custom numeric IDs remain editable; a named key does not guarantee
that the current server sends it. Decimal bonuses remain raw multipliers: +15%
melee defense is 1.15 and +130% bow damage is 2.3. Keep counts explicitly choose
display-name matching (Classic Keep #) or item-template matching. A condition
draft must be accepted or cancelled before saving its containing rule.

**View / edit original Classic UTL copy** opens the source document separately
from the active converted rules. The document editor exposes all 31 Classic
requirement types, priority, actions and counts, custom expressions, RGB/tolerance
values, palette slots, and extra file blocks. Unknown requirement bodies are
retained in a raw editor. Save a separate copy; **Save copy and apply to active
loot** validates compatibility before changing only the current Loot profile
rules and salvage policy. Other hunt settings remain intact. Import/compatibility
also opens files that cannot be activated.

Color matching, custom expressions and third-party User actions can be viewed,
edited and saved in UTL form but still cannot execute in UCM. The editor labels
these limits; it does not silently drop them to activate a profile. Disabled
unsupported rules remain disabled until their original requirements can pass
compatibility validation; they cannot be enabled as partial native rules. The retired
DamagePercentGE requirement remains editable and never matches, as in Classic.

Reviewed against the Classic tutorial, advanced tips, and the public VTClassic
Shared Constants.cs / LootRules.cs catalog. Named Classic keys retain their MIT
notice in `ClientMods/LICENSE-VTClassic.txt`.

## Installation and source editing

Built-ins live at `Plugins/ACEClient/ClientMods/` and are staged as UFS assets,
including on Quest. Add user plugins under the application's writable
`Saved/ClientPlugins/Installed/<id>/`, with these files:

* `plugin.json`: manifest (API, identity, capabilities, settings controls).
* `main.lua`: source returning a `function(snapshot, profile)`.
* `default.json`: default profile object.

Preferences are in `Saved/ClientPlugins/settings.json`; named profiles are in
`Saved/ClientPlugins/Profiles/<id>/<name>.json`. Quest's Saved directory is in the
app's private storage; installation of user plugins there currently requires ADB.
The manager does not download packages, resolve dependencies, or run native DLLs.
Duplicate IDs are rejected; user files cannot override the built-in UCM ID. To
modify UCM independently, copy its three files into a user plugin with a new ID.
Press Reload installed plugins after editing source. A malformed manifest is
skipped with a visible diagnostic. Invalid scripts stop only their own instance.

## Public plugin API 2

[Public API 2](PublicAPI.md) exposes the UCM/native gameplay pipeline under any
plugin ID, optional reuse of the stock UCM/Item Tools policies, full snapshots,
profile/import services, native tabs/sections/editable controls, DAT icons and
bundled PNG artwork. The [API Workbench example](Examples/api_workbench/main.lua)
shows a custom automation interface. API 1 plugins remain supported.

## Companion plugins

For kill trackers, Vsense-style target lists and helpers running beside UCM, use
`mode: companion`. The [companion API guide](Companions.md) covers chat events,
character-specific saved state, interactive rows, world/Waypoint map markers,
NAV targets, local messages and native command bridges, with installable examples.
API 2 adds declarative native widgets and bounded bundled PNG loading; arbitrary
widget code remains outside the Lua sandbox.

## Minimal plugin

`plugin.json`:

```json
{
  "api": 1,
  "mode": "companion",
  "id": "vital_monitor",
  "name": "Vital Monitor",
  "short_name": "VIT",
  "version": "0.1.0",
  "description": "Shows current health in the manager.",
  "permissions": [],
  "settings": []
}
```

Optional `short_name` supplies the desktop bar label (up to four characters).
Otherwise the bar uses the first four characters of the plugin ID. The tooltip
always includes the full name and current status.

`default.json`: `{}`

`main.lua`:

```lua
return function(snapshot, profile)
    return { status = "Health: " .. snapshot.health .. "/" .. snapshot.max_health }
end
```

Companion callbacks run at most twice per second on the game thread, only while running.
They receive fresh data tables; mutating those tables does not modify game state
or the saved profile. Return `nil` or one intent table (the companion extension also
accepts the structured fields documented in its guide). Local variables outside
the function retain state until Stop. UCM metas can resume after an explicit Stop
or a manual-movement pause: variables, the call stack and fired rules survive,
ordinary state timers restart, and persistent timers include paused time. Changing
the character, profile or script, or encountering an error, starts a fresh VM.
Pending gameplay requests are cleared on resume. No background threads are created.

### Snapshot

* `time`: monotonic seconds, not a wall-clock date.
* `health`, `max_health`, `mana`, `max_mana`, `stamina`, `max_stamina`:
  current server-supplied player vitals.
* `busy`: current object-use transaction; `ready`: host action interval elapsed.
* `position`: `{cell,x,y,z}`, using locally predicted position where available.
* `moving`: plugin route movement is active.
* `nearest`: eligible nearest monster GUID, or zero; `distance`: meters.
  Uses the client's existing monster filters, excluding players, fellows and pets.
* `spells`: records with `id`, `known`, `school`, `power`, `category`, `skill`,
  `self_buff`, `name`, `icon`, `beneficial`, `caster_target`, `duration`,
  `target_type`, `flags`, `projectile`. Includes known spells and metadata for selected profile buff IDs.
  `known=false` references are never castable.
* `enchantments`: beneficial non-cooldown records with `id`, `category`, `power`,
  `remaining` seconds. Permanent effects use a large remaining value.

### Intents and permissions

All intents may include a `status` string (maximum 1,024 UTF-8 bytes).

```lua
return {action="cast", spell=123, target=456} -- requires cast; target optional for Self
return {action="attack", mode=2, power=0.5, height=2} -- requires combat; 2 melee, 4 missile
return {action="move", cell=0x7D63000D, x=25, y=97, z=12} -- requires navigation
return {action="stop", status="Finished"} -- always permitted
```

The host revalidates known spells and normal spell targeting. VR automation sends
the same ordinary cast request as desktop, rather than merely selecting a VR spell.
Attack validates the requested eligible monster and uses the existing melee/missile requests.
Power is clamped to 0–1 and attack height to 1–3. Host throttles cast/attack requests
to at most one per 3.5 seconds and route intents to at most two per second. Inspect
`ready` before advancing action-specific state. These intervals are conservative;
the server still controls cast success, range, components, equipment and attack
speed. Action requests never assert that the server completed an action.

### Native settings UI

Manifest `settings` entries specify `key`, `label`, and `type`:

* `bool`: checkbox.
* `number`: numeric editor, with `min` and `max`.
* `choice`: buttons for strings in `values`.

Values are persisted in the profile. The same controls render in desktop and VR.
The optional `tools` list can contain `spells` and `route`, adding the shared spell
family/attack-spell chooser and waypoint recorder. The advanced JSON editor exposes
other profile structures. API 2 adds native interactive widgets and bundled image
loading; companion APIs supply chat events, persistent state and local messaging.
Arbitrary native widget code remains outside the Lua sandbox.

## UCM state rules (metas)

Each state can override `buffing`, `combat`, and `navigation`. Unspecified values
come from the main profile. The first matching transition wins, with at most one
transition per tick. Supported conditions: `health_below`, `stamina_below`, `mana_below`, `elapsed`
(seconds in state), `no_targets`, `target_available`, and `route_complete`.
The Metas page edits these rules without JSON. Unknown states produce a visible error and stop. Example:

```json
{
  "initial_state": "Buff",
  "states": [
    {"name":"Buff", "buffing":true, "combat":"off", "navigation":false,
     "transitions":[{"when":"elapsed","value":180,"next":"Hunt"}]},
    {"name":"Hunt", "combat":"missile", "navigation":true,
     "transitions":[{"when":"health_below","value":50,"next":"Wait"}]},
    {"name":"Wait", "combat":"off", "navigation":false, "transitions":[]}
  ]
}
```

These are UCM-native profiles. VT `.usd`, `.nav`, `.met`, loot profiles, and VVS XML
are not imported. More complex conditions or behavior can be authored in Lua.

## Runtime and references

Each script has a separate Lua 5.4.9 VM with an 8 MiB allocator cap and 250,000 Lua
instructions per initialization/callback. Files are loaded as source, not bytecode.
Available libraries: basic functions, table, math, utf8, and bounded string
operations. No filesystem/process/network/native loading, debug, coroutine,
dynamic load, pcall/xpcall, or metatable APIs are exposed. Lua string pattern
functions are omitted because their C execution is outside the instruction hook.
Install plugins from authors you trust; these limits are not an OS process sandbox.

`workavailable()` lets a plugin cooperatively save its cursor and return before
exhausting the callback's work allowance. It becomes false after 200,000 Lua
instructions or 5 ms of regex work. Calling it never resets or extends the hard
250,000-instruction and 20 ms regex limits. Each callback gets a fresh allowance.

`spellindex(spells, inventory, time, skill_margin, unavailable_until)` returns
three tables: spells keyed by ID, component availability keyed by ID, and the
ordered castable spell list. Records reference the supplied snapshot; no spell
records are copied. It combines component stacks and checks known spells, school
skill, component knowledge and temporary exclusions. UCM shares this index within
one decision and rebuilds it for the next snapshot. Native indexing is capped at
16,384 spells, 4,096 inventory entries and 131,072 total entry/component visits per
callback, independently of the unchanged Lua instruction limit.

The decompiled VT material was consulted for behavior boundaries (profiles,
spell categories, rule scheduling, navigation and view abstraction). Its source
was not incorporated. Public references:

* [Decal](https://www.decaldev.com/)
* [Virindi Views](https://virindi.net/wiki/index.php/Virindi_Views)
* [Virindi Tank meta system](https://www.virindi.net/wiki/index.php/Virindi_Tank_Meta_System)
* [Bundle plugin APIs](https://virindi.net/wiki/index.php/Writing_Plugins_that_Interface_With_the_Virindi_Bundle)
* [Lua 5.4 manual](https://www.lua.org/manual/5.4/)

Regression suites: `ACE.Plugins.Runtime`, `ACE.Plugins.UCM`, `ACE.Plugins.Decisions`,
`ACE.Plugins.HostAndPanel`. Live server and physical headset/controller testing is
still needed before treating this preview as an unattended hunting replacement.

## Additional automation API 1 fields and intents

Snapshots additionally expose `player`, `jumping`, `action_serial`, `action_error`,
`trained_skills`, `usable_skills`, `teleport_sequence`, `last_spell`, `last_spell_confirmed`, `inventory`, `targets`, `corpses`, `container`,
`contents`, `route_objects` and confirmed `item_buffs`. GUIDs use unsigned JSON
numbers. Inventory snapshots cache until inventory/appraisal/eligibility changes.
`combat_death`, when present, describes the most recent confirmed death of the
policy's combat target: `id`, monotonic `time`, `cell`, `x`, `y`, `z`, and
`teleport_sequence`. It survives object deletion until replaced or the session
changes. Consumers must bound its age; losing visibility alone does not set it.
Inventory records expose `auto_wield_left` from the live object description.
`last_spell_confirmed` distinguishes a matching spell result from a successful
UseDone alone (which can follow a fizzle). `buff_request.started` is false until
UCM submits `buff_request_start` with its request ID; completion uses
`buff_request_done`. Queue notices use normal retail Tell packets.
Background appraisals do not open the user's inspection window; explicit user
inspection takes precedence. Automatic appraisal discovery is limited to one
request per second and existing assessments refresh after two minutes. Successful mana-stone use invalidates
equipment mana assessments immediately so it cannot spend charges using stale
values. UCM skips snapshot construction while an action is pending.

Inventory entries include ID/template/name/type/icon, slot mask, quantity,
container, equipped/usable state, ammo type and assessment status. Successful
assessments add wield eligibility, damage/modifiers, recovery vital/amount,
item mana and ratings. Unknown fields must not be treated as authoritative zeros.
Target entries have positions, distance and only server-supplied resistances.
Confirmed item buff records are session-local; restarting the client loses them.

Additional actions:

```lua
return {action="identify", item=guid}                -- inventory permission
return {action="equip", item=guid}                   -- inventory; owned, usable requirements
return {action="use_item", item=guid}                -- inventory; owned supplies, self/untargeted
return {action="merge", item=source, target=dest}     -- inventory; compatible owned stacks
return {action="open_corpse", item=guid}             -- loot permission
return {action="loot", item=guid, amount=quantity}   -- loot; current open corpse only
return {action="close_corpse"}                       -- loot
return {action="split_note", vendor=vendorGuid, item=guid, count=quantity} -- loot; owned trade notes only
return {action="sell_note", vendor=vendorGuid, item=guid, count=quantity}  -- loot; confirmed whole note stack only
return {action="use_world", item=guid}               -- navigation; selectable world object
return {action="jump", heading=90, charge=.5, forward=1} -- navigation; ordinary jump
```

## VT review and remaining gaps

Reviewed the supplied decompile's orchestration, buff/cast selection, equipment,
recovery, waypoints, loot actions, meta rules and view definitions as behavioral
references. No VT source or assets are incorporated. Also consulted its official
[standard options](https://virindi.net/wiki/index.php/Virindi_Tank_Standard_Options),
[advanced options](https://virindi.net/wiki/index.php/Virindi_Tank_Advanced_Options),
and [meta documentation](https://virindi.net/wiki/index.php/Virindi_Tank_Meta_System).

UCM now covers the main requested configuration paths, but **is not full VT
parity**. Explicit remaining work includes:

- Advanced USD assistance, external plugin APIs and VVS-generated views.
- Remaining UI/HUD expressions, full .NET formatting and third-party loot User actions.
- Full streak optimization, specialized imbue rankings, dispel drums for others,
  general crafting and ammunition manufacture. Pets, rings, ordered DOTs,
  self-dispel spells/supplies and primary/offhand choices are implemented.
- Vendor restocking, automatic fellowship management and non-fellow shared-vital
  recovery. Server-provided fellowship vitals are supported.
- Dynamic obstacle planning and verified monster weaknesses absent from appraisal.

NAV and supported MET/UTL imports, chat-triggered metas, following, auto-cramming,
salvage combining and loot Salvage/Sell/Read actions are now implemented. See the
compatibility document for exact corpus results and remaining import failures.

Live server validation still needs ordinary and custom inventories, long combat
cycles, recovery depletion, slope/corpse use, portal destinations and jump routes.
Physical VR pointer/controller usability and comfort are not established by
render tests or cross-compilation. Do not describe these offline checks as a
successful unattended hunt or complete VT replacement.

## Tell buff requests and route overlay

In **Buff others**, turn on **Accept buff requests**, enable **Buff**, and start
UCM. The default exact tell keywords are `mage`, `heavy`, `missile`, `light`,
`finesse`, `twohanded`, `unarmed`, and `melee`. Edit keywords or add aliases in this tab. Case and surrounding spaces
are ignored. Public chat and NPC dialogue never start a request. Up to eight
players can queue; a player cannot reset an active request by repeating a tell.
Completed requests have a two-minute request cooldown. Stop, logout, and character
changes clear the queue. Disabling buffs cancels requests. Automatic tells report
queue position, turn start, and completion or cancellation; progress and
unavailable-buff counts also appear locally.

All roles request six attributes, three defenses, lore, healing, run, jump,
mana conversion, regeneration, armor, and elemental life protections. Mage adds
creature/item/life/war/void skills. Weapon roles add their corresponding weapon
skill and relevant support skills. UCM chooses known **Other** spells within the
caster's skill margin and honors buff exclusions. Self-only spells are never
redirected to the requester. Normal recovery and self buffs take priority.

Item magic targets compatible equipped objects that the server has exposed for
the recipient. Keep the intended weapon equipped. Other players' clothing is
often only transmitted as appearance, without targetable item GUIDs; a full set
of armor banes cannot be guaranteed in that case. This follows the server's item
spell rules rather than pretending that casting a bane on another player buffs
all their armor. Missing spells, equipment, or failed casts produce an incomplete
summary. When a request reaches the front of the queue, a requester outside the
configured distance or no longer visible to the client is removed immediately,
notified, and skipped so the next player can receive buffs. Leaving range during
the turn also cancels the request. Each queued request expires after 15 minutes.

**Route → Show route in world** defaults on for both new and migrated profiles.
A cyan ground-projected line joins recorded points, with amber jump segments.
The active waypoint has a green ring, while other destinations use small spheres.
Circular routes include the final segment back to the first point; linear routes
reverse along the existing path. The overlay uses a normal unlit game mesh.
Portal and recall steps break the path across teleportation. Edits and current
waypoint changes refresh on the next UCM update. Geometry is retained between
updates, with bounded periodic ground traces for streamed terrain; this is a
route preview, not an obstacle-avoiding pathfinder.

## Loot Profile Editor and Virindi files

The separate **LOOT** plugin-bar button opens **Loot Profile Editor**. It has no
permission to perform game actions. Its native rules are shared with UCM; saving
rule changes stops UCM. Enable UCM and start it separately to automate looting.
The same Slate interface is available from the VR plugin manager.

Native profiles support ordered Keep/Skip/Salvage/Sell/Read rules, name/material/workmanship/value
filters, keep-count limits, integer/decimal/string appraisal properties, and spell
name/count filters. The first match decides. Use **Property and spell conditions**
for armor, set, ratings and other server properties. Native rules require missing
properties to exist unless **Missing value: use 0 (VT)** is selected. Appraisal is
requested before making a decision that depends on unknown data. Spell patterns
support bounded regex with groups, alternatives, repetition and named captures;
unsupported .NET dialect features are reported. Native loot profiles allow 2,048
rules, 64 conditions per rule and an 8 MiB JSON file. Rule lists
are filtered and paged to avoid constructing thousands of widgets.

**Import / compatibility** accepts a file path, a folder, or files copied into
`Saved/ClientPlugins/ImportInbox`. It reads `.utl`, `.nav`, `.met` and `.usd` files.
Preview shows incompatibilities before import. A supported conversion creates a
named UCM profile, preserving unrelated settings and stopping automation. Original
files are never modified. Reports under `Saved/ClientPlugins/Imports` preserve the
parsed source, converted data and every compatibility issue. Unsupported earlier
Skip rules block the entire import; they cannot silently fall through to Keep.

**Edit UTL rules / save compatible copy** opens the standalone legacy loot editor.
It supports creating, filtering, reordering, adding/removing rules and requirements,
all 31 installed VT Classic requirement types, actions, keep counts and custom
expression text. Unknown requirement bodies and extra blocks (including salvage
combination settings) are retained. **Save UTL copy** writes version-1 `.utl` files
to `Saved/ClientPlugins/LegacyLootProfiles`; those files can also be read by VT
Classic. Saving a UTL does not activate its rules. Use **Check compatibility of
saved copy** before importing it into UCM. Legacy expressions and custom actions
are data in this editor, not executable plugin code.

See [Virindi compatibility](VirindiCompatibility.md) for the verified collection,
format details, supported imports and remaining runtime gaps.

UCM 0.7 adds executable VT meta imports for supported rule sets, bracket-syntax
variables/stopwatches, state calls, embedded navigation and native command adapters.
Use **Metas** to enable the imported program and see its current state. Navigation
supports Circular (wrap), Linear (reverse) and Once modes, plus follow, portals,
recalls, pauses, checkpoints and directional jumps. All 88 files in the supplied
vtank-routes repository convert. Public metas are only partially compatible;
review the import report before running. Local/fellowship chat, fellowship operations and server-confirmation responses
request separate `say`, `fellowship` and `confirm` capabilities; re-enable UCM
to grant a changed capability list. Nothing starts automatically.


The Loot page also edits salvage workmanship groups, material overrides and value
targets. Combining is separately enabled. **Loot → Looting settings → Apply salvage
rules to existing inventory** is an explicit, default-off option to process all
owned packs while UCM runs, even with corpse looting disabled. It uses the active
profile's first matching rule and only executes Salvage; earlier Keep/Skip rules
protect items. An Ust and successful appraisal are required. Equipped, retained,
traded, tinkered and inscribed items, packs, tools and salvage bags are excluded.
The same tinkering/inscription checks protect newly looted salvage. Pending jobs
are revalidated before sending the normal server salvage request.

**Loot → Looting settings → Auto-combine stackable items** merges partial stacks
of components, ammunition, pyreals and other matching item templates across all
owned packs. This uses the existing `autostack` profile option (off by default),
works independently of corpse looting, and needs no Ust. Full stacks, equipped,
retained and traded items are skipped. UCM waits for both inventory updates
before another merge; a missing confirmation pauses inventory management for
30 seconds without blocking combat or recovery.

**Salvage combination** edits per-material groups and value targets. Materials
never mix; full, retained and traded bags are excluded. Classic assigns gaps to
the preceding group: `1-6,7-8,9,10` means values like 6.9 stay with the first
group. Below a value target, only pairs totaling less than 100 units combine;
zero disables value mode. Workmanship-only mode selects in ascending workmanship
order until reaching 100 units, with a maximum of 64 input bags per request.
The imported `CombineSalvage` option controls this setting.

Without inventory salvage enabled, destructive loot actions apply only to newly
transferred items. Pending jobs are cleared on Stop/logout. Do not stop UCM
before visiting a vendor if you intend to process its queued Sell rules.


## UCM folder library and compact controls

**Import** browses `.nav`, `.met`, `.utl` and `.usd` files by type from a chosen
folder. Set the folder once, then Refresh after adding files. Selecting a filename
validates it and creates named native profiles. **Import all files** imports the
whole folder; each file-type page can also import all files of that type. Bulk
imports preserve your current profile selection, continue past unsupported files,
and report each result in a scrollable, clearable area capped at 16,000 characters.
Reimporting the same source selects its existing profiles and preserves native
edits; meta dependencies reuse those same files. Different sources with colliding
names receive a suffix. To replace a converted profile from its source, delete
that native profile first and import again. Old anonymous `Imported` profiles
are renamed from their source filename, or to `Recovered_<section>` when no source
name exists; unchanged defaults are consolidated into Default.
Choose those profiles in Overview or their editing tabs; these selections persist
and manual switching stops automation. Unsupported imports leave the previous
configuration intact. Select Default for an empty/default profile. Imported nav
points feed the world overlay and enable Follow route; press Start to move.
Original files are never rewritten. Meta dependencies are resolved alongside
their source file and the meta selects them when its rules execute. Character,
loot and navigation loads finish before later meta commands run, including
nested action lists. This preserves options set immediately after a load.

Buff is automatic by default. The Buffs tab shows compact toggles, skill margin
and refresh threshold; **Edit buff exceptions** reveals the searchable family
list only when needed. The workflow follows VT's separation between profile
selection and route/meta editing, while retaining native modern controls.
Reference: https://www.virindi.net/wiki/index.php/BeginnerBundleGuide

Self-buff cycles start with Creature Enchantment Mastery, Focus, Willpower
(Self), Mana Conversion and Life Magic Mastery, matching VT's opening order.
Other trained magic schools follow before ordinary buffs. This also applies to
Force Buff. Exclusions and eligibility still apply; each new snapshot rechecks
current skill so confirmed buffs can unlock a higher spell tier immediately.

Automatic maintenance chooses sustained buffs (at least five minutes), so
short burst spells such as Tusker Leap do not replace regular Jump buffs.
Explicit `buffs` entries can still request short spells. Renewal windows are
capped at half the base duration, preventing a fresh short buff from immediately
becoming due again. Confirmed casts release their request delay while retaining
the busy gate and minimum action interval.

Buff planning indexes selected spells, exclusions and equipment once rather than
scanning those lists for every spell. Repeated NeedBuff conditions reuse a plan
within the current snapshot only. A plan is discarded before the next callback,
so confirmations, inventory changes and option changes are not cached across
ticks. The sandbox still enforces its original 250,000-instruction ceiling.
Regression coverage includes 2,200 spells with equally large selection and
exclusion lists, plus full inventory and infinite-loop rejection fixtures.

## Vendor supply lists and component exemptions

The **Vendors** page is available in both UCM and the Loot Profile Editor.
Open a vendor, press **Refresh vendor stock**, add supplies, and enter the total
quantity to keep in inventory. Enable **Automatically buy saved supplies**.
While UCM runs, opening that vendor buys the difference, including when a route
Use point visits it. This does not generate a route to the vendor: record the
walk and Use points as usual. Lists match server, vendor name/class, and item
name/class rather than temporary object IDs. They are saved in the selected
Vendors profile, independently of Loot and the other profiles.
Purchases use the regular client Buy protocol and wait for inventory confirmation.
When pyreals are insufficient, UCM redeems accepted trade notes for the shortfall.
Partial stacks are split first; only the confirmed new stack is sold. UCM waits
for both removal of the notes and receipt of their pyreals before buying.
Retained notes, notes offered in trade, and note types in this vendor's saved
restock list are excluded. Alternate-currency purchases do not redeem notes.
The snapshot supplies `pyreals`, `vendor_uses_pyreals`, eligible
`vendor_trade_notes` (identity, count and unit face value), and each stock item's
`unit_value` and `sell_rate`. These fields use the open vendor's server data.
Failure or timeout stops restocking; inspect currency, stock and pack space
before restarting. Inventory and stock are indexed once per decision.

The server's `SpellComponentsRequired=false` exemption bypasses component and
pea checks. Missing/true uses the existing component checks. Changing characters
or changing the server property takes effect in the next snapshot. Skill, known
spell and server rejection checks remain active.

**Recovery > Recharge equipment while UCM is stopped** implements imported
`ManaChargesWhenOff`. It only uses the configured equipment-mana supplies and
never runs hunting, buffing, navigation or metas. Disable it to stop background
recharging. Repeated failures suspend it until restarting UCM or changing setup.

## Large loot profiles and UI interaction performance

Starting UCM and editing routes, monsters or activity settings previously
revalidated every selected loot rule, including when looting was disabled.
Repeated pattern compilation, whole-profile JSON round trips and duplicate
waypoint saves made those unrelated controls stall with large loot profiles.

Pattern validation now uses a bounded cache of exact patterns and diagnostics.
Unchanged loot rules reuse an owned validation snapshot after a full content
comparison; in-place edits and changed files still receive full validation.
Native UCM controls save their existing JSON objects directly, and route actions
save the position and action details together. Starting an ordinary hunt reads
the selected profiles without saving them again. Imported metas retain dependency
materialization, saving and resume checks. Shared-file refresh, conflict detection,
size limits and transactional writes remain enforced.

Windows host-side measurements with a 1,638-rule profile (7,007 conditions,
2,668 pattern references, 304 unique patterns), with looting disabled:

- Start: 235 ms before, 60 ms after.
- Add waypoint: 416 ms before, 69 ms after.
- Save a new monster: 213 ms before, 69 ms after.

With looting enabled the revised operations measured 64, 72 and 75 ms,
respectively. These are local host-action measurements, not live-server UI
frame-time guarantees; large profile reads and saves still have a cost.

Validation: the Win64 Development editor build and ten focused automation tests
passed, covering large real/generated profiles, cache eviction, invalid edits,
shared-profile conflicts, settings UI, route actions and meta resume. The route
fixture emitted missing-position JSON warnings but no test errors. Local reports
are under `Saved/LootProfileInteractionReview/{Before,Final,UIAndSynthetic}`.
The reusable benchmark is `ACE.Plugins.LootProfileInteraction`; optionally pass
`-UCMLootProfile=<file>` to read a real profile into isolated test storage.

## Void combat eligibility and count controls

Automatic combat can select a usable wand for enabled Void curses even when
no direct-damage Void spell is eligible. Previously the loadout planner rejected
the wand before reaching the curse pass. After confirming the enabled curses,
a curse-only loadout waits for their timers and refreshes them when due. Known
spells, components, skill buffer, equipment requirements and element overrides
still determine eligibility. With a usable direct attack, combat continues
casting it between curse refreshes.

If no attack spell is usable, the status identifies the spell eligibility checks
instead of reporting missing equipment. Combat failure reasons remain visible
during the retry cooldown rather than reverting to generic waiting text.

Minimum ring targets is an integer count from 1 to 10, without a percent sign.
Global and per-monster priority sliders run from -1 (ignore) to 10. Existing
imported values are preserved; editing a slider uses its displayed range.

Validation: Win64 Development editor build and eight focused automation tests
passed. Coverage includes actual retail Void spell metadata, native spell and
inventory snapshots, direct attacks, all three curse families, curse-only
refreshes, eligibility failures and UI ranges. Reports are under
`Saved/VoidCombatReview/{CurseBefore,Final,Verified}`. The curse-only rejection
was reproduced before the fix; normal full-spellbook casting already passed.
The reporting player's exact configuration has not been verified on a live
server.
