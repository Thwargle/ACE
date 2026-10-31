# AC:Unreal / AC:VR release 145

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.10.31.145**. Android version code: 145.
Quest installer revision: 9. These notes cover all changes since v144.

## UCM weapon and element selection

- Bring automatic loadout selection closer to VTank's priorities for matching
  slayers, confirmed vulnerabilities, elemental rends/cleaves, and monster
  element preferences, including secondary weapons.
- Correct swapped Fire/Cold preferences in the bundled monster database and
  combat settings. Existing native profiles migrate their previous choices once.
- Keep the selected wand, attack spell, and vulnerability on the same element;
  retain the matching ammunition when choosing a launcher, including prismatic ammo.
- Preserve explicit monster-rule requirements and use server-supplied resistance
  data when available. Damage and usable skill break otherwise equivalent choices.

## Loot profile builder

- Add AND/OR requirement groups and multi-value alternatives, including named
  weapon subtypes such as Sword or Unarmed independently of the weapon's skill.
- Add literal Contains, Exact, Starts with, pipe-separated Any/All, and VT regular
  expression text matching, with an explicit case-sensitivity option.
- Expand the standard VTank property catalog and requirement support, including
  palette color/slot matching and the 63 standard armor color groups.
- Correct missing property aliases, secondary/plural names, and armor resistance
  mapping. Preserve imported regex behavior and existing rules' AND defaults.
- Suppress item-match chat messages from UCM's automatic inventory appraisals and
  salvage scans. Manual inspection and confirmed pickup notifications still work.

## Chat, plugins, and examination

- Scale the chat entry area, Send button, and target selector with the chat font.
- Add Lua Plugin Text Scale (75%-200%) under User Interface Options, independent
  of chat and overall UI scaling. Update open plugin panels without losing drafts.
- Keep chat's expanded peek size and vertical position out of saved layouts, so
  logging out or saving UI while expanded preserves the normal window geometry.
- Show Unenchantable in the examine panel using retail's ResistMagic threshold,
  including Fist of Enlightenment and other qualifying items.

## Validation and packaging

Focused regressions cover shipped Lua combat decisions, loot editing and profile
reloads, property mappings, inventory notifications, plugin text scaling, chat
layout persistence, and examination formatting. Platform build and package
verification results accompany the release locally. Automated checks do not
replace live-server hunts or physical-device acceptance.

Packages exclude retail DATs, personal settings, server data, and generated
MeshCache files. Android phones/tablets remain a preview using a separate APK
from Quest. Existing signing identities and installer revision are retained.
