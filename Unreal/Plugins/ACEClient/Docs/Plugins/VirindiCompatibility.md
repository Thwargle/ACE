# Virindi compatibility review — 2026-10-04

## Current coverage — 2026-10-05

Full parity remains incomplete. This section is the current status; the dated
sections below retain earlier checkpoints and their historical counts.

The latest installed corpus accepts **223/224** files (including 71 empty metas),
the public meta corpus **43/91**, and the public NAV corpus **88/88**. These are
whole-file conversion results, not successful unattended hunts or quest runs.
The remaining installed failure is Unlimited_IBControl.met, which depends on
external multi-client/UI commands and unresolved/template input. Public failures
also include missing companion profiles/databases and unsupported requested
behavior; the importer preserves those failures rather than silently skipping it.

Since the initial runtime checkpoint below, the implementation adds:

- Pea splitting from validated gameinfodb.ugd recipes, component thresholds,
  fast-buff movement after the local spell words, and eligible pet summoning with
  ownership/cooldown/charge confirmation. No spell-speed or server-state bypass.
- Arc/bolt preferences, ring density/range rules, primary and secondary
  vulnerabilities, Yield, Imperil, Gravity Well, Broadside, Fester, Weakening
  Curse and Festering Curse. Durations require actual server confirmations;
  UseDone alone never marks an offensive enchantment as applied.
- Explicit primary weapons, caster-only rules, shield/dual-wield/no-offhand
  strategies and explicit offhand GUIDs. Mainhand/offhand changes wait for
  authoritative equipment updates, with bounded retries. The Rules panel exposes
  these choices and the equipment pool includes shields.
- Group debuff modes, cached spell-family selection and effect indexes. The
  128-monster/64-wand/512-spell fixed-family regression completes its scan within
  one tick under the unchanged 250,000-instruction budget. Automatic elemental
  weapon plans retain bounded, resumable work slices.
- Species/max-HP catalog predicates (with an explicit gameinfodb.ugd dependency),
  live shield predicates, and server appraisal overrides for available qualities.
- Numeric/string helpers, midpoint-to-even rounding, exclusive random upper
  bounds, UTF-16 string lengths and chainable stopwatches. Character int64,
  decimal, Boolean and string properties are retained from login and live
  private/public packets, including custom IDs. Missing properties return numeric
  false, while an explicitly empty string stays a string. Relog clears old data.
- Resumable meta variables, fired rules, call stack and route after an explicit
  Stop or manual pause. Ordinary state timers restart on Start; persistent state
  timers include stopped time. Interrupted actions are cleared. Profile/script/
  character/session changes and errors discard the resumable VM.
- Typed world-object expression results, internal-type queries, reference
  identity for lists and normal host intents for selecting/casting/using objects.
  Nearest-monster queries compare actual distances and exclude blacklisted targets.
- Physical attacks hitting the environment feed per-target failure counts. VT's
  strict count-greater-than-limit behavior and default 4/120-second settings are
  retained. Confirmed damage resets misses; evades, incoming damage and player
  chat do not count. Blacklisted targets yield to navigation and become eligible
  after the timeout. Attack feedback without a GUID is accepted only while the
  associated target remains selected and the attack window is current.
- Single-target offensive spells now share target failure accounting. Result
  deadlines begin at local spell words, permanent server target failures exclude
  that target immediately, and fizzles/resists do not count as obstruction misses.
  Actual magic damage or enchantment confirmation resets the count. Multi-target
  spells and self/beneficial spells are excluded; released targets and stopped
  plugins discard pending results.
- Corruption, Destructive Curse and Corrosion follow VT's ordered DOT strategy.
  Where projectile metadata has no duration, the policy uses VT's documented-in-
  source 31/16/31-second family durations. Only server magic confirmation starts
  an effect timer. DOTs refresh at expiry, never at the ordinary precast threshold,
  and are no longer mistaken for repeatable bolt attacks.
- CastDispelSelf and UseDispelItems import and execute. Temporary elemental
  vulnerabilities are checked against spell/item power, Chorizite and skill
  availability, plus item shared cooldowns. Dispel success requires an enchantment
  removal; a use acknowledgement is insufficient. Failed/unconfirmed attempts
  back off, and pending work is discarded on settings replacement or meta resume.

Windows Editor build46 and all 13 plugin suites in ucm-full-tests41.log pass.
The native source builds on Linux (ucm-full-linux7.log) and Quest Android
(ucm-full-android7.log). The shared Lua policy's subsequent cooldown-key fix passes
the Windows suites and Quest source-sync check. Public meta validation is
ucm-full-public41.log; corpus reports are ucm-full-installed.json,
ucm-full-public-metas.json and ucm-full-public-nav.json under Saved/Logs.

## Earlier runtime checkpoint — 2026-10-05

This pass compares the UCM policy with the local VT source (`ch.cs`, `fk.cs`,
`fo.cs`, `hi.cs`, `a5.cs`, `ai.cs`, `f9.cs`, and the default settings database).
The following now execute through the regular client requests:

- List construction, mutation, lookup, shallow copy and reverse, with zero-based
  indices and cycle/size checks. Fellowship membership/leadership queries,
  server-name queries, inventory regex lookup, give-item expressions, character
  integer properties, vital/skill queries, coordinate parsing and self-selection.
- Wand equip and spell-cast expressions retain VT's result codes and check
  learned spells, configured skill margins and DAT scarab requirements. Item
  buff bindings also check supplies. The large-spellbook regression retains the
  existing 250,000-instruction budget; redundant spell scans were removed.
- Coordinate distance expressions now return meters, as VT does. The former
  implementation divided by 240 twice. Coordinate axis queries retain map units.
- Corpse ownership, pet-owner attribution, fellowship loot sharing, the
  100-second reservation wait and exclusion of another player's rare corpse.
  First-observed times reset with the session and are removed with the object.
- Automatic melee/missile power, slash/pierce and multi-strike rules, shield vs
  dual-wield differences, and trained Recklessness limits. Manual power remains
  available. Idle buff top-off runs after combat/loot; consumable confirmation
  requires an observed duration increase, including during Force Buff.
- Kit eligibility uses VT's success estimate, healing modifier and remaining-use
  ranking, configured magic-stance permission and optional peace-mode switching.
  Actual success still requires a server vital update.
- Fellowship recovery selects the lowest eligible vital percentage in range,
  prioritizing health then stamina then mana. It ignores dead members, stale
  updates (10 seconds), and blocked sight lines, and waits for a vital increase
  or bounded failure timeout. The update subscription is independent of the
  desktop/VR panel subscriptions and ends when disabled/stopped. Third-party
  non-fellowship vital broadcasts are not implemented by this adapter.

At this earlier checkpoint, the installed corpus accepted 216/224 files, up from 211/224 before this pass.
The eight remaining failures request external multi-client/UI tools, pea/component
crafting, fast casting, ring/debuff/pet strategies, or malformed/template commands.
These remain explicit whole-profile failures. The count includes 71 empty metas;
it is not a count of completed hunts or quests. Full parity is still incomplete.

`ucm-full-tests10.log` passes all 13 plugin suites. New regressions cover the
expression return values, list references, scarab availability, corpse policies,
power selection, idle top-off, kit thresholds and fellowship recovery. The corpus
report is `ucm-full-installed.json`. Further platform/corpus validation is recorded
below after completion.

## Coverage follow-up — 2026-10-05

Additional behavior verified against the local VT decompile and exercised through
the UCM runtime:

- Assistance type 8 (reusable mana stones) is supported alongside type 6
  (disposable charges). `RefillWornMana` and its item-mana percentage control
  equipment refills. The runtime waits for observed equipment mana improvement;
  a successful UseDone by itself cannot confirm a refill. Failed or missing
  confirmations use a cooldown and bounded retries. Loading another character
  setup clears the pending refill.
- `ReadUnknownScrolls` takes eligible unknown scrolls before ordinary loot rules,
  following VT's current-school-skill >= spell difficulty minus 15 check. Known,
  unlearnable and already-carried scrolls retain normal loot behavior. Reading
  waits for the actual inventory transfer and corpse closure; it does not use
  existing inventory items speculatively. The Loot page exposes this option.
- `getisspellknown` checks the actual learned spell IDs, including IDs without
  loaded DAT spell metadata. The host caches these IDs with its spell snapshot.
  `wobjectfindnearestbynameandobjectclass` supports the class-first, case-sensitive
  regex lookup used by VT. Unsupported literal regex dialects fail conversion.
- Buff and hunt spell difficulty margins import independently. Option-variable
  getters retain distance conversion even when a one-unit probe exceeds the
  allowed range; for example, AttackDistance uses 240 meters per map unit.
- Valid empty CondAct tables and empty commands import as empty programs/no-ops.
  Missing tables and malformed programs are still rejected. Empty metas are
  separately labeled in the coverage report, not counted as tested quests.

The installed corpus accepts 211 of 224 files: 78 NAV, 72 MET (71 empty),
60 USD and one UTL. Compared with the preceding 137, the increase consists of
71 valid empty metas and three character setups. The public meta collection
accepts 42 of 91 files, up from 41; the public route collection accepts all 88.
These are conversion counts, not live completion rates. Whole-file validation
continues to reject unsupported requested behavior rather than skipping it.

All 13 `ACE.Plugins` suites pass in `ucm-coverage-final-tests.log`. Added cases
cover scroll transfer/read confirmation, duplicate/known/unlearnable scrolls,
mana confirmation and timeout, spell knowledge without DAT metadata, class/name
lookup, regex rejection, empty metas and option getter units. Windows Editor,
Linux Development and Quest Android Development builds pass; Quest source sync
also passes. Logs and corpus reports use `ucm-coverage-*` under `Saved/Logs`.

The remaining installed failures include a third-party multi-client controller
meta, component/pea supplies, fellow/all-corpse policies, fast casting, automatic
attack power, helper-vital recovery, ring/debuff/pet strategies and healing-kit
success thresholds. Public profiles additionally need expression-list support,
external-plugin adapters and missing companion files. The broader limitations
below still apply. Live server and headset validation remains outstanding.

## Native UCM commands and saved conversions — 2026-10-05

The supported `/vt` command adapters also accept `/ucm` with the same arguments:
`jump`, `tapjump`, `start`, `stop`, `forcebuff`, `cancelforcebuff`, `echo`,
`setmetastate`, `mexec`, `opt set`, `enablecombat`, `enablebuffing`, `enablenav`,
`enablelooting`, and `meta/nav/loot/looting/settings load`. For example,
`/vt jump 90 true 600 strafeleft` imports as
`/ucm jump 90 true 600 strafeleft`. `/ucm help` lists the chat syntax.
The command compiler is shared by chat, NAV command waypoints, MET actions and
command-producing expressions. The old Lua `vtcommand` API remains an alias of
`ucmcommand`, so previously saved profiles continue to work. No commands are sent
to Decal or injected into the server chat stream.

Chat commands require an enabled UCM. A manual jump or expression runs on its own
when UCM is stopped; it does not silently start the saved hunt. Force Buff uses
the existing manual refresh controls. Options change the saved setup when stopped
and the session's options when running. Meta state changes require a running UCM.
Loading a file from chat follows the folder selector: it saves and selects a new
conversion, stops automation, and asks the user to Start. In-profile load actions
continue running against their bundled, validated dependency library.

Both importing and selecting a legacy file save a native JSON setup in
`Saved/ClientPlugins/Profiles/ucm` (or the configured plugin storage root),
available from UCM's Profiles tab. Linked profiles are compiled into that setup,
including cyclic references; their original names remain library lookup keys.
Command text stored in compiled actions uses `/ucm`; item names, chat payloads
and chat-matching expressions are preserved. Command-producing expression
prefixes are converted without stripping concatenation whitespace; dynamically
assembled legacy commands remain accepted by the shared compiler.

Existing names receive `_2`, `_3`, etc. Exclusive creation protects earlier
copies. The original NAV/MET/UTL/USD files are never written, and compatibility
reports also receive unique names. The saved native copy can be reloaded without
the original source directory. Unsupported commands/options still block an
import with an explicit compatibility report; renaming a command does not claim
support for unimplemented Virindi or external-plugin features.

Validation: `ucm-commands-final-tests.log` passes all 13 plugin suites and parses
224 installed profiles with 137 supported conversions. Regressions compare
legacy/native command instructions, execute one-shot jumps and dynamic command
expressions, preserve cyclic dependencies, reload saved copies after invalidating
their legacy source, and verify repeat imports preserve earlier bytes. Windows
Editor, Linux Development and Quest Android Development compile successfully
(`ucm-commands-build-final.log`, `ucm-commands-linux-build.log`,
`ucm-commands-quest-build.log`). Live server/headset command tests remain pending.

## Scope and evidence

This is file interoperability with UCM, not a Decal DLL loader or a VVS runtime.
The installed VirindiTank and ClassicLooter files, supplied utank2 decompile, and
public Mag-Tools source were read as behavior references. No original plugin
binaries, source, personal profiles, or downloaded routes are bundled.

Sources:
- https://github.com/lino-ranta/vtank-routes
- http://www.virindi.net/wiki/index.php/Public_Meta_Repository
- https://github.com/Mag-nus/Mag-Plugins

The public meta collection consists of files extracted from the 52 ZIP links
listed directly on the wiki; forum attachments and missing dependencies are not
included. All 91 files parse: 66 MET, 19 UTL and 6 USD. **41 files currently convert
completely: 22 MET and all 19 UTL.** Supported metas also execute an initial and
32 repeated synthetic sandbox ticks. This does not traverse every state or prove
an end-to-end quest run. The other 50 files are blocked with specific diagnostics.
All 88 standalone NAV files from vtank-routes convert, including Circular, Linear,
Once and Follow modes. Public UTL round trips preserve every requirement and extra
block. The current installed-profile corpus parses all 224 files: 78 NAV, 73 MET,
one UTL and 72 USD. Of those, 137 convert: all 78 NAVs, the UTL, one MET and
57 USD character setups. Accepted USDs also execute an initial and 32 repeated
synthetic runtime ticks. This is adapter coverage, not proof of every combat
strategy or of a complete live hunt.
Empty state tables are not runnable programs. Counts describe the files currently
present in the audited folder; the earlier larger collection is not assumed present.

## Navigation

Walking, pauses, portal GUIDs, named world objects, recall spells, commands,
checkpoints and directional jumps use normal client actions and movement.
Circular routes wrap; Linear routes reverse; Once routes stop at the end. Position
conversion preserves VT coordinate units, negative dungeon-local coordinates and
height. Indoor NAV coordinates resolve in the current dungeon block. Portals and
recalls wait for a server teleport sequence and the end of portal space, including
at the final waypoint. Checkpoints also wait for the server position.

Follow routes can retain the other player's observed path around corners.
The path is bounded to 512 observations and resets after teleport/disappearance;
it is not an obstacle-solving pathfinder. Doors can be opened on the active
walking segment. Navigation priority, distance, idle peace and main-pack cramming
are supported options. Movement never writes a pawn position or bypasses collision.
The route overlay updates for embedded and loaded routes. NAV has no indoor cell
IDs, so conversion alone cannot establish that a dungeon route is traversable.
Host movement uses the profile's waypoint tolerance, including imported
NavCloseStopRange. It no longer stops at a fixed 70 cm when the route requires a
closer arrival. An unreachable waypoint height retains the progress watchdog.

## Buffs, combat and recovery

Automatic self buffing includes usable innate skills, as well as trained skills.
Creature, life and equipment-compatible item buff families are confirmed before
advancing; item auras target the player rather than every carried item. Buff-other
requests have bounded confirmation waits and report unconfirmed families instead
of treating them as successful. The host also stops on a lost cast acknowledgement.

Weapon/spell choices use known requirements and resistance data. A launcher's
elemental override is evaluated against its ammunition, not the bow's intrinsic
piercing damage type. Unknown compatible ammunition is appraised; unavailable
loadouts stop instead of attacking with previously equipped, incompatible ammo.
Healing kits require trained Healing and remaining uses, allowing another
consumable or recovery spell when a carried kit cannot be used.

## Meta programs and commands

The bounded interpreter supports ordered once-per-state rules, compound actions,
All/Any/Not, calls/returns, embedded routes, ordinary state timers, movement
watchdogs, chat captures, monster/range conditions, inventory counts, pack space,
level/burden, vendor state, death recovery, buff-needed checks, enchantment time,
portal events and distance from navigation points.

Expressions support variables, stopwatches, arithmetic/comparison/Boolean
operators, sequencing, regex comparison, quoted/escaped strings, world lookup,
object names/coordinates, player coordinates, distance, string conversion and
try-use/try-apply actions. String comparisons are case-insensitive. Boolean
operands are eager and &&/|| have equal precedence, as in the reference evaluator.
Supported VT option get/set actions convert map distances to/from meters.

Native command adapters include named use/apply/select/give, inventory-only and
world-only searches, combat mode, facing, jumps, recall aliases, fellowship
creation/recruiting/openness/quit, logout, local/fellowship chat, force buffing,
visible echo notices, server-confirmation responses and supported VT options.
Commands are parsed into constrained intents; no imported command executes an
OS shell or arbitrary client command. Exact names precede partial-name matches.
Confirmation responses require the `confirm` capability and the exact currently
pending server type/context; they do not click arbitrary screen coordinates.
Existing installations must re-enable UCM to grant added capabilities.

Literal profile loads resolve same-folder dependencies case-insensitively,
including cyclic meta references, before activation. Limits are 64 files,
4 MiB per source and 8 MiB aggregate source. Missing/unsupported dependencies
block the whole import. Dynamic profile names only load files already validated
in that library. Loading a route clears the old pending route; loading a loot
profile replaces its salvage policy. Nothing starts automatically at login.

## Loot classification and actions

First matching rule wins; an unsupported earlier Skip rule cannot fall through to
Keep. Decal classes derive from server item categories and flags. Supported
requirements cover integer/decimal/string properties, spell names/counts, exact
palettes, material/workmanship/value, skill/level/free slots, minimum damage,
buffed melee/missile/property values, potential tinkered damage/defense/offense
and combined ratings. Missing properties use the imported rule's zero semantics.
Appraisal precedes decisions that require data not yet known.

Regex matching uses ICU with .NET capture numbering and both named-capture
spellings. It is case-sensitive unless the pattern specifies otherwise; VT's
expression regex operator supplies its required case-insensitive flag. Patterns
are limited to 2,048 characters, 32 capture groups, 4,096-character input,
2 ms match time and 64 KiB regex stack. Aggregate regex work is bounded per tick.
Lookbehind, conditional/balancing groups, extended/comment mode and numeric
backreferences mixed with named captures remain unsupported, with import errors.

Keep/Skip/KeepUpTo, Salvage, Sell and Read work through normal inventory actions.
Only items transferred by UCM's current loot session are queued for destructive
post-loot actions. Split stacks resolve their new server GUID. Changed, equipped,
retained or traded possessions cannot be silently substituted. Ambiguous merges
stop for review. Sales wait for an open vendor. Server errors and action timeouts
stop instead of repeatedly selling or destroying items. Pending post-loot jobs
are session-local and do not survive Stop/logout.
Final-item ownership confirmation and post-loot actions survive a corpse closing
automatically. A recently auto-closed corpse is temporarily suppressed while its
world removal catches up. Imported KeepUpTo rules count exact item names across
WCIDs, as VT's `hv` / `f9.a` inventory counting does; native rules retain WCID scope.

SalvageCombine workmanship groups, material overrides and value thresholds import
from UTL. Combining is a separate opt-in control. The loot editor exposes those
groups and thresholds as well as calculated/character conditions, rule order,
actions and property filters. Matching partial bags combine only with the same
material; requests wait for removal of their source GUIDs. The editor and runtime
validate ranges, ownership and request bounds. Rule files retain unsupported
source data for round-trip export; saving an editable source is not activation.

## Character settings follow-up — 2026-10-05

The profile library now lists `.usd` files alongside NAV/MET/UTL files. Loading
settings replaces prior imported collections and supported defaults rather than
leaving the previous character's supplies or exclusions active. Loading through
a meta clears its temporary option overrides and cancels the previous attack.
Native state overrides do not mask imported settings.

Implemented and exercised through UCM's Lua runtime:

- Ordered recovery handlers, including vital, magic/other stance, percentage
  intervals and the final-handler fallback. Failed recovery waits for a bounded
  acknowledgement window, temporarily skips that handler and tries the next.
  Normal recovery remains active even when no-target thresholds are lower.
- Exact named food, healing kits and equipment mana supplies, extra buff families,
  exemplar-based family exclusions and consumable buffs. Consumables require an
  observed enchantment; UseDone alone does not confirm success. Fellowship-only
  consumables check actual fellowship membership from the host snapshot.
- Imported equipment GUIDs, including signed GUID conversion and explicit item
  buff bindings. Missing owned items are skipped. Redirectable weapon/armor auras
  still target the player once, honoring UCM's requested behavior; non-redirectable
  spells retain their explicit item target. This deliberate aura behavior differs
  from VT's historical per-item refresh loop.
- Exact case-insensitive monster names, default rules and bounded expressions
  using `name`, `typeid`, `range` and `metastate`. Meta priority counts and combat
  selection use the same matcher. Supported damage-element choices, attack
  exclusions, priorities and streak enable/disable preferences are retained.
- `/vt jump` commands retain heading, Shift, charge and optional strafe direction.
  The common 700 current-heading convention is supported, as with NAV jump nodes.

## Remaining gaps — full VT parity is not achieved

- Advanced assistance, general crafting/ammunition manufacture, vendor restocking,
  mana-tank looting/refilling and recovery while automation is off. VT's precise
  spell/conversion efficiency estimates are not yet replicated.
- Dispel drums for other players, complete streak optimization and VT's
  specialized hybrid/offhand/imbue rankings. Basic explicit/automatic offhand,
  pets, rings, DOTs and self-dispels listed above are implemented.
- Ghost-object deletion based on missing HP updates, automatic fellowship
  management and third-party non-fellow
  vital recovery. Server-provided fellowship recovery is implemented.
- External-plugin commands such as /og, /mf lmq, /ch, /ub bc and VVS-generated
  views. Missing companion profiles/databases cannot be inferred safely.
- Character-specific settings load/save commands, remaining chat/HUD/VVS
  expression functions, full .NET
  number formatting/locale behavior, regex dialects, color-based loot requirements,
  custom loot expressions and third-party User actions.
- Appraisal does not always expose monster resistances or other players' equipment
  GUIDs. UCM cannot promise complete weakness knowledge or cast on hidden armor.
- Actual long hunts, live latency, server quest/vendor confirmations and headset
  portal/jump traversal still require gameplay validation. Offline tests cannot
  establish unattended completion or frame-rate gains.

Unsupported requested behavior is not represented as a successful no-op. A profile
must pass whole-file and dependency checks before activation. A supported
conversion means its represented nodes have adapters; it does not prove every
live gameplay outcome matches the original suite.

## Validation

`ACE.Plugins` covers the sandbox, host permissions/profile persistence, loot and
buff decisions, inventory scale, route/tell requests, imports and legacy adapters.
Regressions include failed-component fallback, split-stack GUIDs, server
confirmation waits, exact/partial lookup, follow corners, route reloads,
calculated loot rules, regex limits/capture numbering, and expression semantics.

Use `-VTProfileCorpus=<folder>` with `ACE.Plugins.VirindiProfiles` to audit a local
collection; results are written to `Saved/Logs/vt-native-compatibility.json`.
Validation logs for this pass are `ucm-gap-final-tests.log`,
`ucm-gap-tests10-installed.log`, `ucm-gap-nav.log`, `ucm-gap-build-final.log`,
`ucm-gap-linux-final.log` and `ucm-gap-quest-final.log` under `Saved/Logs`.
Windows Editor, Linux Development and Quest Android Development builds pass.
The October 5 review adds a continuous synthetic buff/fight/appraise/loot/salvage/
resume sequence, recovery eligibility, elemental ammunition, tight waypoint
tolerances and lost-acknowledgement regressions. Host steering integrates desktop
and VR axes at 30/90/144 FPS with 17.5 cm and 70 cm stopping tolerances.

All 12 `ACE.Plugins` suites pass in `ucm-review-final-tests.log`. Whole-route
synthetic traversal covers 60 walk-only NAV files (12 installed, 48 public),
including circular wraps and linear turnarounds, for 7,847 waypoint visits.
Examples include `RogueDungeon1.nav` and `gromnieCamp1.nav`. Routes containing
interactions retain separate action/acknowledgement fixtures; they are not counted
as full gameplay traversals. The actual installed `VirindiSpells.utl` accepts its
three intended scrolls and rejects unrelated loot. The public collection still
parses 91 files, with 41 supported conversions; all 88 public NAVs convert.
Reports are `ucm-review-installed.json`, `ucm-review-public-nav.json` and
`ucm-review-public-metas.json` in `Saved/Logs`. Platform build logs use the
`ucm-review-*-build.log` names, with `ucm-review-build-final.log` for Windows Editor.
Headset comfort, live latency, long hunts, vendor/quest confirmations and actual
portal/jump traversal still require live testing. Offline tests do not establish
unattended quest completion or headset frame-rate gains.

The character-settings follow-up uses `ucm-parity-final-tests.log` (13 suites),
`ucm-parity-installed.json`, `ucm-parity-public-metas.json`, and
`ucm-parity-*-build.log`. `CharacterSettings` covers stance/order/fallback,
untrained kit rejection, lost acknowledgements, exact named supplies, excluded
families, item bindings, consumable confirmation, fellowship requirements,
monster predicates and element/streak selection. The installed corpus improves
from 80 to 137 supported conversions; the public meta corpus remains 41 of 91.
