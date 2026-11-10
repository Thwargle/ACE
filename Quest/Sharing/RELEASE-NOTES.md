# AC:Unreal / AC:VR release 154

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.10.154**. Android version code: 154.
Quest installer revision: 10. These notes cover changes since v153.

## UCM

- Retain successful equipment assessments through unequipping, object refreshes
  and relogging, with separate caches for each server and character.
- Honor forced weapon elements when choosing between elemental wands.
- Add VT-style mob matching expressions, including name/species, regex, AND and OR.
- Let health recovery take priority over pending stamina/mana recovery once the
  native action lock clears; improve streak finishing against low-health targets.
- Allow healing and combat while waiting for ordinary route interactions, without
  repeating the pending use or accepting an unrelated action's completion.

## Retail networking and interfaces

- Correct trade acceptance packets, both players' acceptance indicators, and
  offered-item ordering and duplicate handling.
- Improve fellowship/friends updates and allegiance information replies.
- Show housing guest-list replies and refresh maintenance/payment information.
- Correct contract updates, repeat countdowns and journal sorting.
- Support server instruction popups and clean up stale confirmations on logout.
- Improve book reading and add desktop book writing, page creation and deletion
  with author permissions and server acknowledgements.
- Correct Last Attacker selection and compact skill/attribute/vital updates.
- Keep character properties, item icons, innate spells, enchantments and plugin
  cooldown data synchronized with server updates. Refresh Lua inventory data
  without discarding successful equipment assessments.
- Update the Lua API documentation with the supported behavior and limitations.

## Quest installation and diagnostics

- Include Diagnose-Quest.cmd in the installer and installer-only update bundle.
  It collects startup logs, installed-version/data checks and available Android
  crash/ANR details into a ZIP, without modifying the app or copying account files.
- Reject malformed DAT directory cycles, excessive nesting and oversized resource
  reads instead of allowing unbounded recursion/allocation during loading.

## Verification and scope

Validation covers the Editor, packaged Windows and native Linux, APK contents and
signatures, isolated Windows upgrades, and the Quest helper's fake-ADB tests.
No physical Quest/phone acceptance test or live-server parity replay was performed.
The reported Quest v152 startup hang has no confirmed root cause; the helper is
included to gather the evidence needed to diagnose it. Diagnostic game logs may
contain chat; review the ZIP before sharing it.

Packages exclude retail DATs, personal settings, server data and generated MeshCache
files. Android phones/tablets remain a preview APK.
