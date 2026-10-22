# Virindi compatibility review

## Import and meta execution review — 2026-10-13

Compared the supplied VT decompile's `hn.cs` rule scheduler, `f.cs` All action,
`h2.cs` chat command action, `ff.cs` embedded route loader and
`uTank2/PluginCore.cs` settings/nav command handlers. VT runs an ordered action
list synchronously: a settings load is complete before the next option command.
UCM now waits for the native profile-load acknowledgement before continuing
nested actions or evaluating later rules. Empty embedded routes also send a
native route update while otherwise idle, so a meta can clear an old path.

Imports reuse existing named native profiles by source, including dependencies
exported from metas. Import All processes files between UI frames and preserves
the active selection; compatibility failures remain visible in a bounded,
clearable report. Unsupported commands still prevent conversion. This review
does not claim support for every public meta or Decal plugin dependency.

Regression coverage includes repeated and mixed-case imports, edited profiles,
anonymous-profile migration, nested USD/UTL loads followed by options and routes,
and the supplied Aun Ralirea source graph through conversion, Lua and native loads.

## Self and other buff review — 2026-10-13

Compared VT `eq.cs` (selection and timers), `fz.cs`/`gw.cs` (refresh grouping),
`gj.cs`/`u.cs` (fast-cast word trigger and movement release), `dm.cs`,
`uTank2/MySpell.cs`, and `uTank2.Resources.defaultsettings.usd`. Animation review
used retail `CMotionTable::GetObjectSequence` and `CSequence::remove_cyclic_anims`.

- Automatic self buffs now select ordinary attribute, regeneration, protection,
  trained-skill and equipment families. Untrained usable skills, fellowship
  spells and arbitrary beneficial custom families are not silently added.
  Explicit inclusions remain supported. UCM retains automatic weapon auras,
  whereas VT also supports explicit item bindings.
- New profiles use VT's 300-second refresh default. Existing explicit settings
  remain unchanged. Starting a buff adds the default 30-second grouping window
  for 30 seconds. Both spell and consumable maintenance cap oversized windows
  at half duration; server-reported augmented durations are available to the policy.
- Matching result chat can precede the character enchantment packet. A bounded
  two-second grace prevents an immediate duplicate of that exact spell without
  masking dispels or delaying a newly available stronger tier. Character buffs
  still use authoritative enchantments, and item timers still require confirmation.
- Other requests use `buff_skill_margin`, spell-ID family exclusions and selected
  protections/banes; they no longer inherit the combat castability buffer or choose
  fellowship spells. Each explicit request confirms its casts once; remote buff
  registries are not available and are not guessed.
- The fast-buff backward press/release still occurs in one update without held
  travel. It removes only a held casting tail. Unfinished cast links and following
  scarab actions survive, including rapid successive casts, matching retail's
  sequence removal rules.

Regression coverage: `ACE.Plugins.BuffParity`, `Decisions`, `Recovery`,
`CharacterSettings`, `RouteAndTellRequests`, `SchedulingParity`, and
`ACE.RetailParity.RunSpeed`. These cover policy timing, server-result handling,
real DAT spells, queue failures, and movement packets; a live visual comparison
with retail remains separate from the automated checks.

## Meta, routing and vendor follow-up — 2026-10-13

Compared the local VT decompile's `hn.cs` state machine, `ff.cs` embedded
routes, `da.cs` independent profile storage, `ca.cs` route advancement,
`v.cs` OpenVendor waypoint and `uTank2/cLogic.cs` / `cr.cs` activity and
recharge ordering with the importer, native host, UCM policy and tests.
Vendor restocking is UCM functionality built around those route interactions;
the VT core reviewed here does not contain the entire Virindi Item Tool suite.

Corrections in this pass:

- NAV type 5 now retains **Open/Close Vendor** semantics. Zero means close the
  current vendor, not Use object zero. Opening is confirmed by the observed
  vendor GUID rather than a generic UseDone. Previously converted type-5 Use
  waypoints are recognized at runtime, so existing profiles also benefit.
- Restocking completes before the route proceeds to Close Vendor. Purchases
  larger than a stock batch continue from replicated inventory counts. A
  one-item resale listing no longer hides a larger stocked listing of the same
  item. Turning restocking off cancels its outstanding purchase wait.
- Once the host's real use/transfer locks clear, vital recovery runs ahead of
  vendor opening, purchase and trade-note waits. An unrelated recovery UseDone
  or error cannot confirm/fail a vendor transaction. Inventory quantities,
  split identity, sale proceeds and bounded timeouts remain the checks.
- Pausing obstructed navigation no longer freezes the meta state machine or
  watchdog. Its recovery state can continue, and a new explicit EnableNav
  command can retry the route. A previously active navigation override alone
  cannot undo the pause.
- The public Lua API includes `close_vendor` with the current vendor GUID and
  navigation permission. It sends ordinary NoLongerViewingContents and refuses
  to close a different vendor if the selection changed.

Remaining material gaps, not claims of parity:

- Public quest metas still request missing adapters such as `/mt loot` and
  `/mt lootp` for named quest objects, named `/mt cast`, `/r`, arbitrary key
  simulation, login queues, and other plugins' commands. Missing companion
  USD/UTL/MET files, species databases, unresolved templates and malformed
  source commands also prevent activation. Imports continue to report these
  issues rather than silently dropping the requested behavior.
- General crafting/ammunition manufacture, all assistance-item types,
  automatic fellowship management and jiggle/helper options remain incomplete.
  Existing component splitting, summon refills and equipment mana maintenance
  do not cover the whole original suite.
- Classic color-tolerance requirements, arbitrary loot expressions and User
  actions remain unsupported; full external multi-client integrations are not
  replaced by the local plugin messaging API.
- Ordinary NPC/meta Use waits and the single pending-recovery record still
  have broader scheduling limits than VT's independent rules. They need
  action-specific completion correlation before unrelated game actions can
  safely overlap their waits. This pass removes the vendor-specific blocking
  paths, not every pending-action gap described in the earlier reviews.

Validation: Windows Editor build succeeded. All 14 targeted suites passed,
including new `VirindiWorkflows` and the native `RouteAndTellRequests` packet
checks (the latter reports 34 warnings from fixtures without player positions).
Coverage includes new and previously imported vendor waypoints, multi-batch
buying, note splits/sales, healing during waits, unrelated acknowledgments,
stale vendor closure, blocked-route recovery, linked meta dependencies and
Aun Ralirea startup. Reports are in `Saved/UCMProfileRefresh/ParityFinalTests`.

Fresh corpus results: **226/227** installed profiles supported, **52/91** public
meta collection files supported, **88/88** public NAV files supported; no parse
errors. The installed exception remains `Unlimited_IBControl.met` with external
integration requirements. Reports are in the same review directory under
`ParityMetaCorpus`, `ParityRouteCorpus`, and each isolated user's
`Saved/Logs/vt-native-compatibility.json`. These are conversion and automated
workflow results, not completed live quests. Quest shared sources are synced;
Linux/Quest packaged gameplay and live-server timing remain unverified here.

## Fast buffs without backward travel — 2026-10-12

Rechecked VT `gj.cs` (spell words/result state transitions), `u.cs` (Backward
input), and `hr.cs`/`hg.cs` (key press/release bookkeeping). VT starts Backward
on eligible spell words and releases it when the cast ends; it does not request
a fixed backward distance. Retail `CommandInterpreter::HandleKeyboardCommand`
applies and sends each edge immediately through `SendMovementEvent`.

UCM previously converted this into up to 120 ms of held movement. Even words
and a result in the same network batch forced a complete movement frame, so
successive buffs translated the character toward nearby portals. The native
implementation now taps and releases in the same update, before integrating
movement. Both ordinary MoveToState packets carry the same position, facing,
stance, and movement stamps. This is a stationary adaptation of VT's input
technique, not a claim that VT itself uses a zero-duration key hold. Spell
eligibility, server acknowledgments, busy retries, windups, and manual input
priority remain intact. Blocked or stale taps are discarded.

Regression coverage checks the packet pair, delayed/coalesced/missing results,
40-buff cycles at 10/30/60/90/144/240 Hz without accumulated player translation,
and jump, portal, manual-input, and UI guards. A live dungeon run remains the
acceptance check for server latency and portal proximity.

Validation: Windows Editor build succeeded. `RunSpeed`, `RouteAndTellRequests`,
`HostAndPanel`, `EquipmentBuffCycle`, `Recovery`, and `PvPCombat` passed.
The combined report is `Saved/UCMFastBuffTapReview/summary.json`; the request
suite retains its existing 24 missing-position fixture warnings. Quest shared
sources were synchronized and checked; no packaged release was produced.

## Target handoff, route return, and idle looting — 2026-10-12

Follow-up on the reported multi-second handoff: the server's combat MoveTo
shares the controller's use-approach state. Death released UCM's physical
retry gate, but that remaining approach still blocked its next decision.
UCM now releases the movement-only approach to its defeated target on health,
death motion, or removal. It leaves other destinations, pickups, and real
UseDone waits intact. An unsent attack/cast waiting for a stance change is
also discarded when its target dies, so it cannot hold the policy until an
obsolete request is ready. Pending casts and equipment changes still honor
server acknowledgments; this does not shorten server swing or recoil timing.
The real-policy/loopback regression reproduced the blocked handoff before
the fix, and now covers approaches plus unanswered/successful/failed appraisal
in melee, missile, and magic. Reports: `Saved/UCMHandoffReview`.
The Windows Editor rebuild and all six targeted suites passed:
`RouteAndTellRequests`, `TargetHandoff`, `Recovery`, `SchedulingParity`,
`HostAndPanel`, and `RetailParity.PvPCombat`. The requests fixture retains its
24 existing missing-position JSON warnings. Quest source is synchronized.
The reported live-server five-second pause still needs in-game confirmation;
this pass does not claim to reproduce its exact wall-clock duration.

Compared the supplied VT decompile's `uTank2/cLogic.cs` rule ordering and
`SchedulePoke`, `cm.cs` idle-peace rule, and `ca.cs` waypoint advancement.
VT applies idle peace as a prerequisite of ordinary loot actions/approach,
after normal recovery and attack selection. Its route cursor consumes reached
walk points in one evaluation rather than waiting an idle poll at each point.
Priority looting remains an explicit override, not evidence that an area is safe.

UCM's corresponding changes:

- Monster appraisal no longer reserves the action gate while its response is
  pending. A short scheduled continuation uses the existing neutral-resistance
  fallback; later appraisal improves selection. Equipment and loot appraisal
  remain blocking because their requirements and rules depend on those values.
- `continue_work` now actually schedules its 50 ms continuation. Canceling an
  obsolete attack and confirming an idle stance no longer add a generic 500 ms
  action delay. Outstanding casts, equipment changes, and server attack/stance
  acknowledgments retain their existing synchronization.
- Idle peace is checked before looting, including opening a corpse and taking
  items from an already open corpse. Visible living monsters within acquisition
  range prevent this transition, including ignored monsters and manual combat.
  Recovery and required buffs still run first. This proximity guard is UCM's
  explicit safety policy rather than a claim that VT's last-resort rule uses it.
- Return paths use the configured waypoint tolerance, capped at 80 cm, instead
  of demanding a 40 cm target and a 20 cm route corridor. The 3D corridor retains
  floor separation and the observed path's bends. Reached walk points advance
  in a bounded loop; use, pause, jump, portal, and checkpoint actions retain
  their own conditions. Native and imported routes share this behavior.
- Arrival is latched until another movement intent arrives, so a small position
  correction cannot restart steering toward a point the player just reached.

Automated coverage exercises the host/network handoff in all three combat modes,
unanswered appraisal, recovery priority, idle peace on/off, nearby monsters,
corpse opening/pickup, route return, and native/imported route advancement.
Live-server unattended behavior remains an acceptance check.

Validation: Windows Editor build succeeded; nine targeted suites passed:
`LongCombatRouteRejoin`, `Recovery`, `RouteAndTellRequests`, `RouteJoin`,
`SchedulingParity`, `TargetHandoff`, `UCM`, `UCMLog`, and `RetailParity.PvPCombat`.
The 2,048-point route fixture now expects both already-reached points to advance
in one decision. `RouteAndTellRequests` also emitted 24 missing-position JSON
fixture warnings. Reports are in `Saved/UCMFlowReview/Results` and the corrected
route-only rerun in `Saved/UCMFlowReview/Retry`. Quest source is synchronized;
no new release or live Linux/headset gameplay validation was performed.

## Combat action synchronization — 2026-10-11

Compared ACE's `Player_Magic`, `Player_Melee` and `Player_Combat` handlers with
retail `ClientCombatSystem::PlayerInReadyPosition` and VT's equipment/action
manager (`ga.cs`) and casting engine (`gj.cs`). VT returns after requesting a
stance/equipment change and gates casting on magic mode. UCM previously sent a
stance request and cast together, using the client's optimistic mode as proof
that the server was ready. ACE can defer the change behind `NextUseTime`, causing
`CombatMode mismatch NonCombat` warnings or rejected spells.

Automated actions now retain their intent while waiting for a server combat-mode
property update. Changing out of physical combat first cancels repeats and waits
for terminal `AttackDone`; a repeat's successful `AttackDone(0)` is insufficient.
ACE's already-started swing still executes its damage hooks after cancellation,
so changing mode or removing either weapon early could produce `no weapon` and
`DamageType.Undef`. Ownership checks now allow UCM to remove or change hands on
its equipped offhand item. Fast-buff retries also recheck confirmed magic mode.

Consecutive casts and same-mode target handoffs do not acquire a transition
wait. Missing confirmations time out only the affected activity; Stop discards
queued intents. Packet regressions cover these transitions, including transient
peace updates and cancellation between repeated attacks. Live-server validation
of the reported October 10 session remains necessary.

Validation: Windows Editor build passed. Six targeted tests passed:
`RouteAndTellRequests`, `EquipmentBuffCycle`, `Recovery`, `Decisions`,
`SchedulingParity`, and `InventoryOrder`. The packet tests also verify that
stopped-UCM mana maintenance retains its stance handshake. Shared Quest sources
were synchronized; no Linux or live-server run was performed in this review.

## Current review — 2026-10-09

Compared the local VT decompile's `uTank2/cLogic.cs` scheduler, `cr.cs`
recharge rule, and `hi.cs` combat spell selector with UCM's policy, settings
adapters and Classic loot editor. The October 7 feature-gap list below remains
applicable; passing the regression suite is not evidence of complete VT parity.

Corrected a newly identified scheduling gap: a pending route portal or recall
previously returned before recovery and combat for up to 45 seconds, even when
the host reported that the character was ready. VT schedules normal recharge
ahead of navigation, and waiting for a destination does not justify monopolizing
the scheduler. UCM now permits recovery/combat during that wait, including with
navigation priority enabled. Actual busy/jumping states still gate actions.
Pending actions retain their kind and cannot be resubmitted by the navigation
phase. Only teleport-sequence and destination confirmation advance a portal or
recall; an unrelated healing/attack completion cannot advance it. Timeout still
pauses navigation alone.

Additional scheduling gaps identified, not yet corrected:

- The single pending-recovery record can wait up to eight seconds before
  reevaluating health, even if it was originally restoring stamina or mana.
  VT's `cr.cs` reevaluates health before stamina/mana whenever its item-use lock
  permits. A fix needs independent confirmation bookkeeping so interrupting a
  mana recovery neither loses its result nor resubmits it repeatedly.
- Pending vendor purchases/trade-note operations and ordinary route Use actions
  still return before normal recovery. Their completion/failure checks use a
  general action serial. Allowing unrelated actions without correlating the
  original request could falsely complete or reject the pending operation.
  Portal waits are independently correlated and therefore were safe to fix now.

Reconfirmed feature gaps in source: Classic color-tolerance rules (14–16), loot
expressions and third-party User actions are preserved/editable but cannot run;
enabled AutoFellowManagement, DoJiggle and random helper buffs lack adapters.
VT's mixed bolt/streak decision uses a target-specific streak tracker and a
difficulty-derived threshold (`hi.cs`); UCM's score-based choice does not reproduce
that strategy. General crafting, full assistance-item coverage and external
plugin/multiclient integrations also remain incomplete. Do not silently ignore
these requirements when activating imported profiles.

Validation: Windows Editor build succeeded; all 32 `ACE.Plugins.*` automation
tests passed. Scheduling regressions cover portal/recall, navigation priority
on/off, actual busy state, urgent healing, combat, unrelated UseDone, confirmed
arrival and timeout with continued recovery. Existing buff, recovery, resource,
loot/editor, vendor, failure-isolation, navigation and VT adapter tests passed.
Reports: `Saved/UCMParityOct09/Tests/index.json`; build/test logs are in the same
review directory. These are source and automated-policy checks, not live-server
unattended runs or new Linux/Quest package verification. Earlier corpus totals
below are historical and were not remeasured in this pass.

## Current review — 2026-10-07

This section supersedes the historical gap lists below. Full VT parity is still
not established. This pass compares the local VT decompile's `uTank2/cLogic.cs`
activity ordering, `hv.cs` loot transfer/queue handling, `ar.cs` salvage activity,
and `c7.cs` bag selection with UCM's policy, host validation, importer and editor.
Classic's public `UTLBlock_SalvageCombine.cs` and `LootRules.cs` provide the rule
and workmanship semantics:
http://www.virindi.net/repos/virindi_public/trunk/VirindiTankLootPlugins/VTClassic%20Shared/

Implemented and corrected in this pass:

- Explicit, default-off **Apply salvage rules to existing inventory**, under
  Loot / Looting settings. It scans owned packs using the active profile's
  first-match rules, including imported requirements. Only Salvage executes on
  pre-existing possessions; earlier Keep/Skip rules protect them. Corpse looting
  can remain off. Importing a profile alone does not enable inventory salvage.
- Automatic salvage now requires appraisal and preserves tinkered/inscribed
  items as VT's `hv.cs` does. Equipped, retained and traded items remain excluded.
  Host validation repeats these checks before sending CreateTinkeringTool. An
  ordinary item having 100+ uses no longer incorrectly counts as a full salvage
  bag. A pending transfer or destruction still requires inventory confirmation.
- Inventory rule evaluation resumes within the existing instruction budget and
  completed scans are cached against inventory, appraisal and profile revisions.
  Failed/ambiguous items are not automatically resubmitted by the inventory scan
  during that run. Other UCM activities retain their existing failure isolation.
- Bag combining preserves material-specific overrides, Classic's fractional
  workmanship grouping, value thresholds and full-bag exclusion. Selection now
  sorts by workmanship as VT does, excludes trade-unavailable bags/tools, and
  treats the UI's zero value target as disabled. The original `CombineSalvage`
  setting now imports and runs through its missing command adapter.
- The existing value-mode fallback deterministically chooses the two smallest
  bags totaling less than 100 units. Classic tries up to twelve random pairs;
  UCM retains the same under-100 constraint without randomly missing a valid pair.

The broader source review confirms that vendor restocking/trade-note funding,
summon refills, mana-stone filling and equipment recharge, recovery ordering,
buff tiers/Force Buff, other-player buff queues, default/per-monster hands,
door retry and route failure isolation are implemented. They are no longer
correctly described as missing by the earlier October 5 lists.

Remaining confirmed compatibility gaps:

- Classic color-tolerance requirements (types 14–16), arbitrary loot expressions,
  and third-party User actions cannot execute. The original-format editor retains
  their fields and can save copies; activation reports incompatibility. Exact
  palette requirements and supported computed/stat requirements do execute.
- Enabled AutoFellowManagement, DoJiggle and random helper buffing lack adapters.
  Fellowship vital recovery exists, but arbitrary non-fellow vital broadcasts and
  full other-player dispel/drum assistance do not have equivalent runtime paths.
- General crafting/ammunition manufacture and every VT assistance-item type are
  not implemented. Supported pea splitting, consumables, mana and summon refills
  do not constitute a general crafting system.
- Streak selection exists, but does not reproduce VT's full streak-chain/HP
  strategy. Equipment selection is not a demonstrated replica of every VT
  hybrid, imbue and damage-efficiency ranking.
- External multi-client/login queues, injected VVS/UI layouts, simulated key
  commands and cross-client broadcasts still require their original integrations
  or new adapters. Missing companion profiles, databases and template values
  remain explicit import failures. `/og summon` and supported native command
  conversions are already implemented; not every external command is missing.
- Complete .NET expression/formatting/regex behavior remains broader than the
  bounded adapters. Monster resistance and other-player equipment decisions also
  depend on information actually supplied by the server.

Validation so far: 27 plugin suites pass in
`Saved/UCMBuffQueueHandsReview/salvage-final-tests.log`. The installed VT folder
converts 224/225 files; the remaining `Unlimited_IBControl.met` requests external
tools and unresolved template dependencies. The public meta collection converts
52/91 files. All 88 public NAV files convert, and all 48 walk-only routes pass
their synthetic full-cycle traversal after correcting the fixture's obsolete
assumption that a +5m offset must join point one. The fixture now supplies an
explicit visible entry; the independent nearest-point RouteJoin suite also passes.
Routes with interactions are not counted as completed end-to-end runs. The new
CombineSalvage adapter passes LegacyAdapters. These are conversion/test counts,
including empty metas and companion files, not completed unattended quests.
Corpus JSON and focused tests are saved
in that review directory. Windows Editor compilation passed. Live hunts, server
timing, VR comfort and Linux/Quest packaged execution remain unverified this pass.

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

The supported `/vt` command adapters also accept `/acueucm` with the same arguments:
`jump`, `tapjump`, `start`, `stop`, `forcebuff`, `cancelforcebuff`, `echo`,
`setmetastate`, `mexec`, `opt set`, `enablecombat`, `enablebuffing`, `enablenav`,
`enablelooting`, and `meta/nav/loot/looting/settings load`. For example,
`/vt jump 90 true 600 strafeleft` imports as
`/acueucm jump 90 true 600 strafeleft`. `/acueucm help` lists the chat syntax.
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
available from UCM's Profiles tab. The setup references reusable Character, Routes,
Loot, Monsters, Metas and Vendors files. Imported meta dependencies (including
cyclic references) are compiled into the Metas file; their original names remain
library lookup keys. See the README's reusable UCM profiles section for migration
and sharing behavior.
Command text stored in compiled actions uses `/acueucm`; item names, chat payloads
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

Self-buff order was verified against VT `eq.cs` (`BuffController`), whose
`b()` builds Creature Enchantment Mastery, Focus, Willpower, Mana Conversion
and Life Magic Mastery first; its cast selector consumes that list in order.
UCM uses those family priorities for normal and forced cycles, followed by the
other trained magic schools. Server-reported current skills determine the next
usable tier; no predicted skill gains are substituted for network updates.

Recovery ordering was checked against VT `m.cs` / `he.cs`: Regular Spell uses
Heal Self, Revitalize Self and Stamina to Mana. UCM keeps direct Mana Boost as a
fallback. VT `gj.cs` returns its cast state to idle on a matching completion;
UCM now releases its fixed request delay on server UseDone, retaining busy and
pending-action checks. UCM's automatic maintenance deliberately excludes buffs
under five minutes; explicitly selected short buffs remain supported with a
bounded renewal window. DAT-backed regressions cover Tusker Leap (10 seconds),
regular Jump VIII, conversion followed by Revitalize, and mana-boost fallback.

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

## October 5: reported AunRalirea profile

The local AunRalirea `.met`, `.usd` and `.utl` now convert without adapter
issues. Its meta passes repeated sandbox ticks; this is offline validation,
not a completed in-game quest. `/og summon on/off/true/false` converts to UCM's
`SummonPets` option; `settings loadchar` and `lootprofile load` use the existing
validated character/loot loaders. Original files remain unchanged.

Added mana-stone loot quantities, reusable tank minimum mana, conversion-heal
comparison multipliers, and equipment recharge while automation is stopped.
Ghost thresholds optionally suppress stale combat targets locally rather than
deleting server-owned world objects. Disabled AutoFellowManagement and DoJiggle
are accepted; enabling these still requires an adapter. Unknown third-party
commands remain errors instead of being silently skipped.

The wider ImportInbox scan still finds unsupported external integrations in
ChaseOswald (`/mt loot`), DiemosFlag (the malformed `enablenavfalse` command), and
IBControl (Chat Helper, Virindi Window Tool, simulated keys, and other commands).
These files are not claimed as supported. The MenhirRings full-route corpus
fixture also fails its assumed start-point traversal; this pass does not certify
that route. Reports and build/test logs use `ucm-vendor-adapters-*` in Saved/Logs.


## October 6: Phaelae and RedRatCave regression pass

`PhaelaeCustom_v6.utl` contains 1,434 rules and no custom expressions. Its final
SalvageCombine block declares 760 characters but only has 758: the final CRLF
was omitted. Import restores only that exact missing final policy terminator.
Truncated rule bodies and incomplete policy data remain errors. The original
file is never modified.

All requirement types used by this supplied profile convert to executable UCM
conditions, including exact palette, buffed properties, ratings, expected
missile damage and calculated tinkered melee targets. Tests preserve every
original requirement, disabled rule and salvage group through UTL re-export,
then exercise the full converted profile against matching and unmatched loot
under the existing VM limits. The rule browser now reports active and disabled
counts separately. Color-tolerance rules, arbitrary custom expressions and
third-party User actions remain compatibility limitations; this result does
not claim that every Classic profile can execute.

RedRatCave's 12 imported points retain linear reversal semantics. Coverage also
checks a combat detour at the reported 0x01D9010B coordinates, returns around an
observed corner, nearest visible joining, and ordinary host movement at multiple
frame rates and camera angles. This is offline regression coverage; a live
melee run in the reported dungeon remains the acceptance check.

VT references for this pass: the supplied decompilation's `f7.cs`/`bo.cs`
(attack-height settings), and the public Classic `LootRules.cs` snapshot in
Saved/Automation (rule IDs, exact palette behavior, comparisons and advanced
requirements). Automatic height/skill selection and the Micro panel are UCM
convenience features; outgoing actions still use ordinary client movement,
equipment and targeted attack messages.

## October 12: relog timers, inventory arbitration and combat decisions

Confirmed self/owned item-enchantment timers now persist per character and
server, with UTC expiration so logging out cannot extend them. This follows
VT's `dm.cs` persistence of confirmed item casts. Character enchantments still
come from the server. Expired timers, changed death counts and newly worn armor
invalidate the appropriate estimates; Force Buff remains an explicit refresh.
Buffs cast before timer persistence was installed cannot be reconstructed.

UCM yields while an inventory drag or container/drop request is outstanding and
resumes after its server receipt (with a bounded timeout for missing replies).
Dragging during an already-started equipment swap leaves the item unchanged.
Automated weapon removal chooses a pack with capacity, including side packs.
Physical attack retransmission throttling no longer blocks unrelated policy
decisions; target-death notifications wake combat, while spell, stance and
equipment acknowledgments remain required.

Automatic heights use the retail wire values High=1, Medium=2, Low=3, selected
from the monster's scaled DAT bounds and relative elevation. Automatic attack
power defaults on and uses VT's `hi.cs` weapon/thrust/slash rules; explicit power
settings remain available. Standard ACE appraisal packets omit elemental
resistances. The bundled `ucm/monster-elements.json` therefore supplies ordered
preferences for 1,470 named monsters, derived from VT GameInfoDB's species and
monster overrides (`e0.cs`, `gv.cs`). Weapon and ammunition selection uses those
preferences only when server appraisal provides no resistances; explicit monster
rules still override automatic choices. Unknown creatures retain neutral
resistances. The reproducible converter is `Unreal/Build/Import-VTMonsterElements.py`;
the catalog records its public source URL. This is a fallback database, not a
claim to know custom-server resistances that were never transmitted.

Regression coverage includes a fresh Lua VM using restored bane timers, expiry,
new armor and death invalidation, manual transfer receipts, physical/spell target
handoff, actual DAT attack heights, elemental weapon switching, custom-server
resistance overrides and weapon-dependent attack power. Offline tests do not
measure live server latency or certify an overnight hunting run.


## October 13: independent profiles and meta navigation

Reviewed the supplied vTank decompilation: `PluginCore.cs` settings loadchar and
profile selectors, `da.cs` character/navigation storage, and `ff.cs` embedded
route actions. VT keeps character, loot and meta selections separate. An embedded
route replaces the current navigation contents; loading character or loot data
does not clear it. UCM's native `profile_load` previously cleared RuntimeRoute
for every dependency load, while Lua kept its embedded route. This split left
the navigation policy without matching native route geometry after a USD/UTL load.
Only NAV/MET loads now invalidate that native route.

UCM now stores one current selection and independently reusable Character,
Routes, Loot, Monsters, Metas and Vendors files. Imports export embedded routes
and validated dependencies to named profiles; meta execution selects those
references without stopping automation. Repeated loading reuses them, including
user edits. This named export is a UCM convenience: VT's embedded-route action
clears its filename and replaces in-memory navigation. Source files are preserved.

Regression coverage exercises migration and selection persistence, shared edits,
native/Lua route consistency, per-monster default inheritance, and profile UI at
wide and narrow sizes. The optional `-UCMMetaFixture=<folder>` test runs the supplied
AunRalirea.met/.usd/.utl graph through conversion, Lua and native profile loads.
Offline startup coverage does not certify a completed live Aun Ralirea quest.

The supplied Aun Ralirea meta intentionally begins with an empty route after
loading AunRalirea.usd and AunRalirea.utl. It requires a fellowship `!start`
message and at least 10 Town Network Portal Gems before transitioning to its
travel state. Regression coverage injects those conditions and verifies both
the selected Route profile and native route geometry. Importing the meta makes
its routes available immediately; it does not bypass this authored start gate.

## October 14: meta selection, diagnostics and monster exclusions

Rechecked `hn.cs` (ordered rules and state changes), `ff.cs` (embedded routes),
`d1.cs` and `f7.cs` (monster rules and target eligibility), and PluginCore's
meta controls. Explicitly selecting or reusing an imported meta now enables its
rules, including when the prior selection had metas disabled. Start still
respects a subsequent explicit disable. Bulk import keeps the original active
selection and switches. Negative monster priority now excludes that monster,
including previously imported profiles and ring-target counts.

The Metas tab shows the active state, pending conditions, inventory counts for
item conditions, and linked character/monster/loot/meta files. It provides
Restart from Default and Go to state controls. Diagnostics describe conditions
without evaluating expressions again; evaluation can have side effects. An
empty meta route no longer tells the user to record a route. Live profile edits
supersede older session option overrides for the edited fields.

The local AunRalirea.usd fixture explicitly gives `<DEFAULT>` priority 2 with
attacks enabled and Aun Ralirea priority 4. This differs from the author's help
text saying it only attacks Aun Ralirea; UCM preserves the file's actual values.
For an Aun-only hunt, edit the linked Monsters profile's default rule to Ignore
or a negative priority, and retain the positive Aun rule. Configure your own
weapon/wand in the linked Character profile: imported equipment GUIDs belong
to the original character. The meta still requires `!start` in fellowship chat
and 10 Town Network Portal Gems. Its turn-in is manual, as authored.

The optional real-file regression now replays town-network cells, both hunting
routes (711 and 796 points), the five-minute fallback, quest-item detection,
NPC approach and the final empty route through Lua and the native host. These
are offline state/route checks, not a live traversal or completed quest claim.

## Automatic mana-stone filling

Checked the supplied vTank `hv.cs` loot classifier, `dy.cs` mana-source queue,
`a0.cs` refill action, and `defaultsettings.usd`. VT evaluates normal loot first,
then automatically takes unwanted magical items with workmanship and at least
`ManaTankMinimumMana` (default 1,000). Tinkering and inscriptions exclude donors.
Demand is the number of empty configured stones minus queued mana donors. Only
confirmed loot receipts enter the donor queue; existing inventory is not treated
as disposable junk. UCM now follows that flow without a manual source pool,
including empty loot profiles, Skip/NoLoot and excess KeepUpTo items. Its ordinary
owned-item, trade, retained-item and equipment protections still apply.

`ACE.Plugins.ResourceMaintenance` covers pickup confirmation, same-WCID carried
items, donor demand, appraisal, minimum mana, loot-rule precedence, post-pickup
protection changes, filling, and recharging equipped items. These are offline
policy checks, not a live server hunt.

## Buff-wand equipment handoff

The supplied vTank `gz.cs` enchantment reader sends only positive-duration effects
to its regular-spell callbacks (`m_d`); permanent equipment effects are stored
separately in `m_b`. `eq.cs` subscribes its buff controller to that regular stream.
UCM uses the same distinction for automatic buffs, NeedBuff and consumable buffs.
A wand's permanent Focus/Willpower/Mana Conversion effects must not substitute
for the timed buffs that remain after switching back to combat equipment.

`ACE.Plugins.BuffEquipmentHandoff` replays those equipment layers appearing and
disappearing, normal and forced buff cycles, melee/dual-wield/missile restoration,
continued attacks, and rebuffing after a real dispel. These are offline policy
checks; they do not claim a live server reproduction.
