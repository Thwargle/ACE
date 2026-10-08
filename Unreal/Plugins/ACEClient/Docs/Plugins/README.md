# Client plugins â€” API 1 (preview)

AC:Unreal hosts portable Lua plugins. The same source and native management panel
are used on Windows, Linux, PC VR and standalone Quest. This is a new API, not a
loader for Decal DLLs or Virindi View Services assemblies.

## Users

**Waypoint** adds clickable chat coordinates, a movable navigation arrow and a
world/dungeon map, with optional GoArrow atlas import. See [Waypoint](Waypoint.md)
for controls, data sources and the UtilityBelt compatibility findings. This visual
plugin requires no Start action or game-action permissions.

Open **Plugins** on the login screen, **Game Play â†’ Plugins / UCM** in desktop
options, **More â†’ Plugins / UCM** in VR, or type `/plugins`. Each plugin shows its
version, requested actions, status, and profile controls. Enable grants its listed
capabilities. Start is separate, available only after entering the world. Nothing
automatically starts at login. A capability change requires enabling again.

In desktop play, a compact **Mods bar** appears at the left edge. Each enabled
plugin has a button: click it to show or hide that plugin's own window. Several
windows can stay open together. Gold text indicates an open window; `RUN` marks
a running plugin. Closing a window does not stop its plugin. Use its Stop control
or `/ucm stop` to stop automation. The `+` button opens plugin management.

Drag the bar or a window by its title strip. Positions are saved locally and
clamped to the viewport when the window size changes. The bar and its windows
hide with the desktop interface (Alt+Z by default) and are removed on logout.
Disabled plugins are removed from the bar. In VR, use More â†’ Plugins / UCM.

UCM is **Unattended Combat Manager**. The 0.7 interface has Overview, Buffs, Buff others,
Combat, Recovery, Route, Loot, Rules, Metas and Profiles pages. Desktop plugin
windows can be resized using the lower-right grip; position and size are saved.
In VR, More â†’ Plugins / UCM opens UCM directly, with a separate management button.

### UCM Micro, combat and route recovery

**UCM Micro** is a compact companion window on the plugin bar and in plugin
management. Its checkboxes control the same active UCM profile: running,
buffing, combat, looting, navigation, vital recovery, buff requests, vendor
restocking, metas, and peace while idle. Closing Micro does not stop UCM.
UCM still needs its normal permissions; Micro grants no additional capabilities.

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
while recovery independently chooses its highest usable spell. Untrained skills that the retail DAT says are
usable (such as Run/Jump) are also eligible. Other skill buffs require training.
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

Health, stamina and mana each have a recovery slider. UCM prefers eligible Life Magic recovery spells,
then assessed supplies (including healing kits and potions). Enable **Prefer kits
and consumables** on the Recovery tab to reverse this order. Unavailable or failed
methods fall back to the other kind; failed methods cool down for 15 seconds.
Changing this toggle overrides imported recovery order and saves with the setup.
The status below it shows the active preference; **Use imported recovery order**
can explicitly restore the imported priorities. Stamina to Mana is allowed only with sufficient
stamina. It observes replicated vital increases and server action failures before retrying.
Rejected recovery spells temporarily yield to other tiers or supplies.
Filled mana stones/charges can replenish low equipped-item mana. Empty supply
pools permit automatic selection; adding supply types restricts the pool.
Assessments are refreshed periodically, so equipment mana is not instantaneous.

### Combat and items

Choose melee, missile, magic, or auto. Inventory appraisals provide requirements,
damage, speed, modifiers, slayer and element data. Individual weapon IDs can be
added to an equipment pool; an empty pool permits all eligible inventory weapons.
Ammunition is matched to the launcher and assessed damage. Equipping uses ordinary
server actions and waits for the equipped state. Private/custom requirements that
are unavailable to the client are not guessed.

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

**Save loot profile** stores rules alone under `Saved/ClientPlugins/LootProfiles`.
Loading one copies its rules into the current UCM setup without replacing combat,
buffs, recovery or navigation. Loading stops UCM. Whole UCM profiles also retain
their own copy of loot rules. Loot-profile files are versioned UCM JSON, not VT
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

Named profiles contain all these settings. Save As creates a copy; Load selects
one. Activity/range/threshold edits are live. Loading a profile or editing routes
and state rules stops the run. Manual movement overrides plugin steering without
stopping UCM or resetting its activities. Turn Combat off and Loot on to fight
manually with automatic looting. Steering can resume on the next plugin decision
after manual input ends. Stop UCM through its interface, UCM Micro, the bound
Start / Stop hotkey, or `/ucm stop`. Death, logout, character changes, disabling
and reloading still stop automation. Closing its window does not stop it.
Only one plugin can run at a time. Commands `/ucm start` and `/ucm stop` remain.

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
loot** validates compatibility before changing only the current setup's loot
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

## Minimal plugin

`plugin.json`:

```json
{
  "api": 1,
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

Callbacks run at most twice per second on the game thread, only while running.
They receive fresh data tables; mutating those tables does not modify game state
or the saved profile. Return `nil` or one flat intent table. Local variables outside
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
Power is clamped to 0â€“1 and attack height to 1â€“3. Host throttles cast/attack requests
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
other profile structures. Arbitrary widget code, texture loading, chat hooks,
inter-plugin messaging and arbitrary custom widget code are not part of API 1 yet.

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

Profiles now browses `.nav`, `.met` and `.utl` files directly from a chosen
folder. Set the folder once, then Refresh after adding or modifying files. Select
a filename to validate and load it; select it again to reload edits. Route and
Meta tabs also have selectors. Current filenames and source paths are shown.
Each selection is independent, persisted with the current UCM setup and stops
automation before switching. Unsupported files leave the previous setup intact.
`[None]` clears that selection. Loaded nav points immediately feed the existing
world route overlay and enable Follow route; press Start to begin moving. You can
turn Follow route off independently. Original files are
never rewritten. Meta dependencies are resolved alongside their source file.

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
name/class rather than temporary object IDs. They are saved with the UCM setup;
Save/Load Loot Profile also includes them. Old loot profiles clear the supply list.
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
