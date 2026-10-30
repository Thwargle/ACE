# AC:Unreal / AC:VR release 144

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.10.30.144**. Android version code: 144.
Quest installer revision: 9. These notes cover all changes since v143.

## Retail interface fixes

- Make the main chat expand/restore button reliably take priority over chat text,
  including at scaled sizes, so clicking it does not start an unintended tell.
- Keep vendor Add to List on the Items tab with the selected item intact.
  Repeated clicks accumulate quantities without switching to the Buying tab.
- Keep combat, use, examine, minimize, close, logout, and radar-lock icons visible
  when the pointer drags off them or their authored rollover artwork is absent.

## Spell hotbar and archery

- Correct the equipped wand's innate spell icon, selection frame, and tooltip.
  Refresh it when equipment changes and include it when cycling spell selections,
  including empty favorite banks and fallback after unequipping.
- Correct manual power/accuracy slider hit detection and dragging across UI scales.
  Normal missile attacks honor the selected accuracy mark.
- Show the floating charge meter in Advanced Combat UI and hide the normal combat
  panel there. Advanced attacks use the held charge on release, as in retail.

## Loot profile builder

- Add named weapon-skill IDs to the UCM and standalone Loot Profile editors,
  including modern weapon and magic skills plus custom numeric IDs.
- Distinguish a weapon's attack skill from its wield-requirement skill, preserving
  VTank rule comparisons and imported profile behavior.

## Android phones and tablets

- Save control layouts across restarts, including positions, sizes, bindings,
  hidden buttons, and added buttons.
- Preserve preferred layouts when folding/unfolding or opening pinned panels;
  temporary screen fitting no longer replaces saved positions or visibility.
- Save the last valid dragged position if the app loses focus or backgrounds.
- Allow related mobile preferences, including opacity, sensitivity, pinned chat,
  and plugin visibility, to persist through the same settings path.
- Use AC:Unreal Mobile update and installation messages tailored to phones and
  tablets, including Android permission prompts and reopening after installation.

## Validation and packaging

Focused regressions cover retail DAT/Slate pointer interactions, spell selection,
missile packet accuracy, loot-profile editing and reloads, update behavior,
touch/VR menus, and mobile settings saved through the actual Android INI hierarchy.
Build and package verification results accompany the release locally.
Live-server and physical-device acceptance remain separate from automated tests.

Packages exclude retail DATs, personal settings, server data, and generated
MeshCache files. Android phones/tablets remain a preview using a separate APK
from Quest. Existing signing identities and installer revision are retained.
