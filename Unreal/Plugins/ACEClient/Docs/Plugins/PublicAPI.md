# Public plugin API 2

API 2 exposes UCM's gameplay data and native action pipeline to independently
named Lua plugins, plus structured native UI. API 1 manifests remain supported.
This is a client API; server rules, spell timing, collision and ownership still
apply. It does not load Decal DLLs or execute arbitrary native widget code.

Start with [API Workbench](Examples/api_workbench/main.lua), its
[manifest](Examples/api_workbench/plugin.json) and
[default profile](Examples/api_workbench/default.json). Copy that directory into
`Saved/ClientPlugins/Installed`, refresh Manage Plugins, enable its permissions,
then Start. The example's activities start off. It has its own profiles and does
not edit the built-in UCM configuration. The machine-readable action/field catalog is
[`ClientMods/api.json`](../../ClientMods/api.json).

## Lifecycle and ownership

Use `"api":2` with either `"mode":"automation"` or `"mode":"companion"`.
Automation explicitly starts, owns game actions, and stops on logout/character
change. Only one ordinary automation driver runs at a time; starting another
stops the previous driver. Built-in Item Tools temporarily pauses built-in UCM.
Companions auto-start when enabled and logged in, run beside the driver, and do
not stop its casting, attacks, movement or recovery. Stopping a companion never
stops UCM. Use companions for trackers and UI; automation for tools that actually
fight, navigate, equip, loot or cast. Closing either window does not stop it.

Callbacks remain `return function(snapshot, profile) ... end`. The output can
combine a game intent with `presentation`, `ui`, `chat_filters`, `state`, `markers`, `messages`, `requests`, and
`profile_patch`. Return nil for no change. Explicit empty UI/marker/message/request
arrays clear those lists; omitted presentation fields retain the previous display.
The tables given to Lua are copies. To change native state, return an intent or a
service request; mutating a snapshot does nothing to the game.

Automation uses UCM's completion/vitals/target-death wakes and action gates, not a
second timer that slows it down. Companion callbacks are limited to 2 Hz. There
is no permission to override casting delays, teleport, bypass collision or send
raw packets. Each VM has 8 MiB and 250,000 instructions per callback. Use
`workavailable()` to split large jobs across callbacks.

## Plugin title and toolbar icon

Every plugin can set a default display title and its own artwork in `plugin.json`:

```json
"presentation": { "title": "Kill Task Tracker", "icon": "tracker.png" }
```

Put the PNG at `images/tracker.png` inside that plugin's folder. It appears in
Manage Plugins and the plugin bar. The full title appears on hover and in the
window header. Standalone plugin windows do not repeat that title above the
Start/Stop controls; authors do not need to add a title row to `ui`. The embedded
login/VR manager keeps a heading to identify the selected plugin. Without a title,
the manifest's `name` is used. Without a usable
icon, the built-in emblem or generic extension sigil remains visible. Images
keep their original aspect ratio inside the 26-pixel icon area; square transparent
artwork is recommended.

API 1 companions and API 2 plugins with `ui` permission may also return:

```lua
return function(s, p)
  return {
    presentation = {title="Kill Task Tracker - 12 / 50", icon="tracker.png"},
    ui = {{id="progress", text="12 of 50 kills"}}
  }
end
```

Omitting `presentation` keeps the current values. Returning it replaces the
runtime overrides: missing keys inherit the manifest defaults; `{}` restores
all defaults. An empty title uses `name`; an empty icon uses the native emblem.
Changes update open windows and the bar without restarting. Runtime overrides
last until a character change or plugin reload; use the manifest for defaults
that must appear before Lua starts. Metadata does not change the plugin's ID,
permissions or saved files.

Plugins can also supply window defaults in the manifest or runtime presentation:

```lua
presentation = {
  title = "Kill Task Tracker",
  window = {opacity = 0.35, pinned = true}
}
```

`window.opacity` is a number from 0 to 1. It fades backgrounds and borders only;
text, item icons and interactive controls remain readable. `window.pinned` hides
the title bar and border and locks the window's position, keeping its contents at
the same screen position. The background retains the configured opacity whether
pinned or unpinned. Hold the rebindable **Quick-show pinned
windows** key (Left Control by default) to restore the header and resize controls.
Runtime changes require `ui` permission and update existing widgets without
rebuilding their controls. Player overrides in Manage Plugins take precedence
over these defaults; **Use this opacity for all plugins** overrides every plugin's
opacity. Omitted window fields inherit manifest defaults, just like title/icon.

Titles allow up to 80 characters on one line. Icons are PNG filenames only (up
to 96 characters), with no paths or URLs. PNGs are limited to 2 MiB compressed and
1024 by 1024 pixels. Toolbar and UI images share one cache per plugin: at most 32
assets and 16 MiB decoded. Missing or invalid images use the fallback; use Refresh
in Manage Plugins after replacing image files on disk.

## Native window controls

The manifest's `presentation` object and runtime `presentation` output accept:

```lua
presentation = {
  controls = "toggle",       -- "start_stop" (default), "toggle", or "none"
  start_label = "Start tracker", stop_label = "Pause tracker",
  settings = false,          -- hide the complete Settings and profiles section
  status = false             -- optional: hide the host's status text too
}
```

The toggle starts/stops the actual plugin; labels follow its running state.
Labels are single-line strings of at most 40 characters; empty strings use
Start/Stop. `settings` and `status` default to true. Defaults and overrides use
the same inheritance/reset rules as title/icon. Changes preserve the open
window and its control identities. These options apply to custom plugin panels;
specialized built-in panels keep their own layout. Companions auto-start when
enabled, making `controls="none"` useful for passive HUDs. Hiding settings only
changes presentation; it does not remove the profile API or change permissions.

## Read and filter chat

`chat` permission already provides `snapshot.chat_events`, including `raw_text`,
formatted `text`, `sender`, `color` (chat message type), `time`, and `serial`.
Use these events to parse server-specific responses without hiding a chat window.

The separate **`chat_filter` permission** allows display-only filtering. Return a
`chat_filters` array to replace your plugin's active rules:

```lua
chat_filters = {{
  id = "quest_dump", contains = " solves (", sender = "", chat_type = 0,
  window = 0
}}
```

This matches ACE/GDLe-format `/myquests` rows in main chat. It does not hide other
Broadcast messages. Custom server formats may require different literals. The
matching uses incoming `raw_text` before speaker/timestamp decoration. Rules run
natively before a row is drawn, so a response does not flash until the next Lua
callback. Hidden messages still reach **all** plugin readers and an enabled chat
log file. Filtering never changes server state or messages, chat window settings,
or existing chat history.

Each rule has a unique `id` (1–64 characters) and at least one nonempty `contains`,
`prefix`, or `exact` string (each at most 256 characters). All supplied tests must
match. Optional `sender` is an exact match; `sender=""` restricts matching to
messages with no sender. `case_sensitive` defaults to false. `chat_type` optionally
matches a numeric message type, 0–63. `window` is 0 for main chat, 1–4 for extra
windows, or -1/omitted for all five. `seconds` optionally expires the rule after
more than 0 and at most 300 seconds; omitting it keeps the rule until cleared.
Matching is literal, not Lua-pattern or regular-expression matching.

At most 32 rules may be registered per plugin. Omitted `chat_filters` retains
them; `{}` clears them. A replacement is validated before applying any of its
rules. Stop, disable, reload, logout, or character change removes that plugin's
filters. One plugin cannot remove another plugin's rules. Any matching active
rule can hide a line in its chosen window.

For outgoing custom server commands, API 2 already supports the separate `say`
permission and `requests={{id="bonus",service="chat",operation="send",text="/bonus"}}`.
This sends text to the server; it does not grant local shell access. `/b` and
`/fship` can be sent the same way. Register filters in the same output or before
the request so the first response is covered. Sending is limited to once per
second; use `events` result records to check acceptance.

See the runnable [Chat Tools example](Examples/chat_tools/main.lua) and its
[manifest](Examples/chat_tools/plugin.json): optional two-minute quest polling,
on-demand server commands, readable responses, and no native Start/Stop or
Settings and profiles section.

## Inventory-style icons

Use `item_icon` for the same square background and composited artwork as the
inventory and Supply Ledger. The original `icon` control remains a plain DAT icon.

```lua
local item = s.inventory[1]
local row = {
  id="supply", type="item_icon", size=32, text=item.name,
  icon=item.icon, item_type=item.type,
  overlay=item.icon_overlay, underlay=item.icon_underlay, effects=item.ui_effects
}
```

Full inventory snapshots now expose `icon_overlay`, `icon_underlay`, and
`ui_effects` alongside `icon` and `type`. A companion needs `data` permission for
that full snapshot. Item icon fields are unsigned 32-bit numbers; texture IDs
include the `0x06000000` prefix. `size` is 16–128 pixels (default 32); optional
appearance fields default to zero. The native compositor applies the type
background, underlay, icon, overlay, and enchantment effects in inventory order.
Returning new values updates the retained icon rather than rebuilding its row.

## Reuse or replace the built-in policies

Optionally add `"policy":"ucm"` or `"policy":"itemtools"` to an automation
manifest. This loads the exact shipped Lua policy as `builtinpolicy` and its
configuration defaults as `builtinprofile`. Each plugin gets an independent
function/VM state. Defaults are not silently applied to the author's saved
profile: copy the wanted values into your own default.json.

```lua
return function(s, p)
  local intent = builtinpolicy(s, p) or {}
  intent.ui = {{id="status", text=intent.status or "Waiting"}}
  return intent
end
```

`policy` is optional. Write your own decision function and return the same native
intents to replace the algorithms. Both approaches get the same cast confirmation,
fast buff movement, equipment requirements, attack cancellation, target-death
wake, inventory transfer, route collision/door use and action-failure handling.
The built-in policies are readable in `ClientMods/ucm/main.lua` and
`ClientMods/itemtools/main.lua`; the UCM defaults define its full configuration
including spells, recovery, summons, equipment mana, vendors, monster rules,
loot/salvage and meta state machines. No reserved plugin ID is needed.

`mana_charges_when_off=true` also opts an enabled automation plugin with inventory
permission into the native stopped-state gear recharge service. With no driver
running, the first eligible plugin supplies its settings. Only the stock recharge
policy runs then; the author's stopped callback is not invoked.

The sandbox also supplies `lootmatch`, `spellindex`, `regexmatch`, `regextest`,
`string.contains`, `ucmcommand` (legacy alias `vtcommand`) and `workavailable`.
These are the same bounded helpers used by UCM. A command returned by
`ucmcommand()` is a policy command record, not a host intent: pass it to a
policy-command request or process the record in your own Lua code.

`spellindex(spells, inventory, time, skill_margin, unavailable_until, components_required)`
returns `by_id, supplied, usable, groups`. The first two are keyed by spell ID;
`usable` retains all eligible tiers in snapshot order. `groups` provides the
same records as `offense` (non-beneficial usable spells), `instant_self`
(usable caster-targeted spells with zero duration), `other_buffs` (usable
beneficial timed spells that are neither caster-targeted nor self buffs),
and `by_category[category]` (all usable spells in that family). `buffs` contains
known self buffs and beneficial timed Item Enchantment spells, including those
blocked by skill, supplies or retry delay, so a policy can apply its separate
buff margin and report unavailable families. Groups retain every tier and
reference the original records; they do not choose which spell to cast.
Indexing remains subject to the per-callback native work and memory limits.

## Permissions and snapshots

Declare only the permissions you need. Enable in Manage Plugins grants that
manifest's permission set; adding permissions requires enabling it again.

- `cast`: casting, force-buff control.
- `combat`: attack, cancellation, stance, power-bar updates.
- `navigation`: movement, facing, jumps, recalls, world use, route output/recording,
  automation selection and logout.
- `inventory`: appraisal, equipment, item use/application, moving, merging, giving.
- `loot`: corpse use/transfers, salvage, selling, purchases and trade-note funding.
  Item Tools' `it_*` intents require both inventory and loot.
- `say`, `fellowship`, `confirm`: outgoing chat/tells, fellowship operations/vitals
  subscription, and responding to a current server confirmation respectively.
- `chat`, `state`, `ui`, `markers`, `waypoint`, `messaging`, `commands`: the
  [companion capabilities](Companions.md). API 2 automation can use these too.
- `profiles`: write your own configuration/profile library and import/export data.
- `data`: opt a companion into the full gameplay snapshot. Ordinary automation
  already receives it. This is read access; it does not grant any game actions.

Companions may request the observational permissions, profiles, data and say;
they cannot request gameplay-driving permissions. Request a selection through a
UI button to select an observed world object or, in API 2, an owned inventory item.

Full snapshots include every field UCM receives: known spells and eligibility,
enchantments/debuffs/cooldowns, current/base vitals, skills and quality maps,
appraised inventory and worn equipment, all packs and capacities, reachable
monsters/corpses, route visibility and movement breadcrumbs, vendor stock/currency,
fellowship vitals, confirmations, portal events and combat results. Item Tools'
transfer eligibility, vendor room/price and inventory failure fields are included
as well. `identified=false` means a property may be unknown; it is not evidence
that the property is zero. Requests for new appraisals still require inventory.

Item `icon` fields contain full DAT texture IDs for rendering. In
`int_properties`, the Decal compatibility keys `218103809` (Icon), `218103849`
(IconOverlay), and `218103850` (IconUnderlay) instead contain the packed texture
index, without the `0x06000000` type prefix. Imported vTank rules compare these
indices; for example, the level-4 overlay is `27703`, not `0x06006C37`.
These public fields are available before appraisal. Extended custom texture
indices retain all 24 index bits.

Beneficial `enchantments` include `duration` (the server's full duration, including
augmentation bonuses) and `remaining` (time left now). Permanent effects have a
negative duration. Use remaining time and power/category to decide whether a
refresh is needed; base spell duration alone does not describe the active timer.

Inventory entries expose `salvage_tool_available` for an owned Ust that is usable
from its current pack and not offered in trade (including its containing pack).
This remains true for a retained Ust: retention protects it from destruction,
but does not prevent using the tool. `resource_available` keeps the stricter
retention check for items being salvaged, sold or otherwise consumed.

API 2 adds `api`, `unix_time`, `server`, `state`, `events`, `error`, `capabilities`,
`plugins`, `automation_owner`, `profile_name`, `waiting_action`, `log`, and the
lightweight nearby `objects` list. `chat_events` has independent per-plugin
cursors and works with UCM disabled. Large bursts are delivered oldest first
in batches of up to 128 events / 64 KiB of UTF-8 message text per callback;
unread events remain queued for later callbacks. `chat_serial` advances only
through the delivered batch, while `chat_latest_serial` identifies the latest
received event. `chat_pending` and `chat_more` expose the remaining backlog.
The shared backlog retains up to 32,768 events / 8 MiB of UTF-8 message text;
`chat_gap` and `chat_dropped` report actual unread overflow, not pagination.
See [companion chat delivery](Companions.md#callback-and-snapshot) for the field limits and
session scope. Chat events are empty without chat permission, regardless of
whether a full snapshot is requested.

`action_error` remains the numeric server result associated with `action_serial`.
`activity_failure` is a separate optional object with the native failure's
`activity` and `status`. The stock policy also receives this through
`profile.ucm_activity_failure`. A rejected loot/route/equipment activity does not
shut down an API 2 automation plugin. Your policy decides how to retry or pause;
a Lua exception or quota violation still stops that plugin.

## Game intents

Return one action per callback. Do not treat submission as completion: observe
`ready`, `busy`, serials, inventory revisions and cast confirmations, or use the
shipped policy's pending-action logic. Native code rechecks targets/ownership and
server state before sending an action.

The field list for every action and its permission is in `api.json`. The actions
cover:

- `cast` with spell/target and optional `fast_cast`; attack (`mode` 2 melee or 4
  missile, `height` 1-3, `power` 0-1), `cancel_attack`, `combat_mode`, `attack_bar`.
- `move` with AC `cell,x,y,z`, metre `arrival_radius`, optional pursuit `target`;
  `face`, `jump`, `recall`, `use_world`, `close_vendor` (current vendor GUID),
  `select`, `logout`.
- `identify`, `equip` (`offhand`/`unequip`), `use_item`, `apply_item`, `store_item`,
  `give`, `merge`.
- `open_corpse`, `close_corpse`, `loot`, `salvage`, `combine_salvage`, `read`,
  `sell`, `buy`, `split_note`, `sell_note`. `combine_salvage.items` is a comma
  separated list of 2-64 distinct salvage-bag GUIDs, with an owned `tool` Ust.
- `it_give`, `it_buy`, `it_sell`, `it_split`, `it_open`, `it_key`,
  `it_refresh_chest`, `it_close`, `it_pickup`: the Item Tools transaction paths.
- `say`, `fellowship`, `confirm` (current `type,context,accept`). Recall destinations
  are marketplace, lifestone, allegiance, house and mansion; spell recalls use cast.
- `profile_load` for validated imported dependencies (`kind`: nav/utl/met/usd),
  `plugin_command`, `buff_request_start`, `buff_request_done`, `force_buff_done`,
  `pause_navigation`, `pause_loot_approach`, `activity_failed`, `notice`, `stop`.

Policy metadata is also public: `activity`, `meta_state`, `targeting_status`,
`route_point`, `route_join_pending`, `route_track_movement`, `helper_updates`,
`continue_work`, `runtime_route`, `runtime_loop_route`, `runtime_reverse_route`.
The runtime route uses the same ground-following overlay as UCM (2,048 points).
Fellowship updates require fellowship; route output requires navigation.
Coordinates are AC local metres/cell IDs, not Unreal centimetres. GUIDs are unsigned
32-bit numbers, even though some native code stores them in signed integers.

## Native UI

`ui` is a list of controls. Every control, including containers, needs a unique
string `id`. Nested layouts support 256 total controls, eight levels, and 128 roots.
The native window retains its normal move/resize controls. Text-input focus uses
the same native editable widgets and gameplay-key blocking as the game's panels.

```lua
ui = {
  {id="main", type="section", text="Hunting", expanded=true, children={
    {id="fight", type="checkbox", text="Combat", value=true},
    {id="range", type="slider", text="Acquire within (m)",
      min=1, max=240, step=1, value=60},
    {id="name", type="input", text="Monster name", value="Red Rat"},
    {id="mode", type="select", text="Strategy", value="Automatic",
      options={"Automatic", "Melee", "Magic"}},
    {id="select", type="button", text="Select target",
      action="select", target=1234}
  }}
}
```

Controls: text, button, checkbox, slider, number, input, select, progress, icon,
image, row, column, section and tabs. Containers use `children`; tabs use each
child's text as its tab title. Sections have `expanded`; their expansion state
and selected tab are retained while the window is open. Common options are text,
tooltip, enabled, padding, size (font/icon), bold and color (green/red/yellow/blue).
Text `size` is a point size from 10 to 36 (default 16), including intermediate
sizes such as 11, 13 and 15. Updating it on an existing control takes effect
without replacing the control or losing focus in neighboring inputs.
Progress is a fraction 0-1. Select takes up to 256 string options. Number and
slider accept min/max/step; native code clamps/snaps the returned value.

Buttons default to `{type="click",id=...}` events. Optional `action` is event,
select, waypoint or command; a command button uses `command="/acueucm ..."`. Native
buttons revalidate their current target at click time. Inputs and other editable
controls emit `{type="change",id=...,value=...}`. Input commits on Enter/focus loss;
`live=true` also emits while typing. Input is capped at 1,024 characters. The host
reuses controls with the same `id` and `type` across callbacks. Text, progress,
and other rows keep updating during mouselook or while a dropdown is open.
Use stable IDs: removing a control or changing its type replaces that control.
Focused text edits are preserved; an open select keeps its current options until
it closes. Handle events and return the updated value yourself. Event delivery
does not automatically save configuration.

`icon` uses a numeric DAT `icon` ID (available on inventory/spell records).
`image` uses `asset="my_icon.png"` from your own plugin's `images` folder. No
absolute paths, URLs or arbitrary disk image reads are accepted. PNGs are bounded
to 2 MiB compressed and 1,024 pixels per side; each panel caches at most 32 images
and 16 MiB decoded pixels. Images are UI artwork, not world material overrides.
Arbitrary C++/Slate, browser scripting and native injection are not exposed.

## Services and profiles

Return `requests={{id="my_request",service="profiles",operation="list",group="Routes"}}`.
Results arrive in snapshot.events as `{type="result",id=...,ok=boolean,error=...}`
plus operation data. At most four requests per callback, with batches at least
0.5 seconds apart. Queue/retry on a failed rate-limit result. There is no implicit
retry of destructive or outgoing actions. The inbox is bounded to 64 events and
384 KiB; consume it each callback. IDs correlate requests; they are not idempotency
keys. Plugin chat/messages and this inbox are separate bounded streams.

- **profiles**: list, read, save, load, delete. `group` defaults to Default; `name`
  identifies the JSON file. Names/groups use 1-64 letters/digits/underscore/hyphen.
  Save takes `data` or uses the current profile. Read returns data; load also makes
  it the active configuration and saves that choice for restart. List returns
  names. Delete moves the file to a unique archive, never erases it permanently.
  Files live under `Profiles/<plugin-id>/Library/<group>`. Plugins cannot write
  another plugin's settings. Files allow 8 MiB, like UCM; returned data allows
  256 KiB. Read optionally accepts field to retrieve one top-level section.
  Load returns profile_name; the active data arrives in the next callback's
  profile, without a duplicate copy in its events.
- **import**: list returns files in the user's configured import folder. Inspect
  returns the lossless parsed document; convert returns UCM-compatible data and
  issues, including validated dependencies. Unsupported imports return ok=false.
  Neither changes the running configuration or original file. Export_loot takes
  a parsed/edited UTL document in data and a safe name, writes a new `.utl` under
  `Exports/<plugin-id>`, and chooses another filename if one already exists.
  Activate converts and applies the imported sections to your own configuration;
  supply profile_name for its native saved name. This handles large imports
  without returning another full copy through the event inbox. Source files
  remain untouched, and unsupported conversions are rejected before activation.
- **route**: record adds a walk/jump/pause/use/portal point to your own active
  profile. Uses the same AC coordinates, object class/name matching and player
  facing as UCM recording. Optional charge (0-1) and seconds. Requires automation
  and navigation permission. Save the profile separately to persist edits.
- **force_buff**: start or cancel, automation/cast only. Start optionally accepts
  only=true for a buff-only cycle. Pair with the shipped policy or implement
  consumption of force_buff_request/force_buff_only and force_buff_done yourself.
- **policy**: command with text such as `/acueucm jump`; compiles into your own driver's
  command queue, using the same meta adapters. Requires commands. This does not
  enable UCM or edit its configuration. Execution retains each action's permissions.
- **waypoint**: set with coordinate text or clear. Uses the existing NAV destination.
- **window**: show/hide/toggle your own desktop window. Does not stop your plugin.
- **chat**: send text, optional recipient for tell or channel=2048 for fellowship;
  otherwise local/server-command text. Requires say; one send per second, at most
  500 characters, one line. For `/myquests`, the commands bridge still works.
- **log**: append text and optional level, or clear. snapshot.log contains the last
  200 entries. API 2 also logs native activity errors/status milestones there.
  This is a session ring, not an unbounded disk log. Use bounded state for any
  selected history that should survive logout.

`profile_patch={...}` merges top-level fields into your own active configuration
and requires profiles. Unchanged patches do nothing. Changed native configuration
notifies the policy to re-evaluate pending work. A patch alone is not a disk save.
For reusable Character, Route, Loot, Monster, Meta and Vendor profiles, use separate
groups, read them, merge their data, and patch the wanted sections. This leaves
composition and your interface under the plugin author's control.
Profile patches and service request trees allow 32,768 nodes, 2,048 entries per
array, twelve nesting levels and 256 KiB of text per field tree. Individual strings
allow 4,096 bytes. Other structured output retains the API 1 limits of 4,096 nodes,
1,024 array entries and 64 KiB text, plus the UI/state-specific limits above.
The same VM memory limit still applies.

## Markers, NAV, state and messages

API 2 retains all [companion APIs](Companions.md): per-character/server persistent
state (64 KiB, atomic writes), independent chat with overflow detection, selected
object actions, existing NAV arrow targets, object-attached world markers,
Waypoint-map annotations, and local plugin-to-plugin messages. Markers are on the
Waypoint map, not the retail compass. The receiving plugin chooses what messages
mean; sending a message cannot grant permissions or start its automation.
`/acueplugin <id> start|stop|<text>` works for API 2 automation as well as companions.
The `/acueucm` command bridge intentionally controls built-in UCM; use the policy
service when the command should belong to your own automation plugin instead.

## Validation and portability

The host uses the same native gameplay implementation for UCM and API 2. The
PublicAPI regression creates a third-party ID with UCM disabled, verifies full
snapshots, target-death wakes, native route geometry, force buffs, failure
isolation, state restoration, nested UI events, profile archive/permission checks,
and the shipped policy module. Existing UCM/VM/companion regressions remain in use.
Source is shared with Windows/Linux/VR/Quest. Platform-specific live gameplay and
server behavior still require testing; an editor automation pass does not prove
all plugins or servers behave identically.
