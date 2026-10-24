# AC:Unreal / AC:VR release 134

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.23.134**. Android version code: 134. Quest installer revision: 9.

These notes cover all changes since **v133 / 2026.10.23.133**.

## UCM equipment buffing

- Skip Covenant shields and other equipment that cannot receive enchantments,
  using the same appraisal property as retail rather than item-name exceptions.
- Remove unenchantable equipment from pending buff queues, including items
  identified after the queue was built and saved explicit item-buff bindings.
- Appraise unknown equipment before switching to a wand or attempting its buffs.
  Failed appraisal attempts remain bounded by the existing retry limits.
- Prevent repeated failed item-buff attempts and unnecessary weapon/wand switching
  so UCM can proceed into combat with the selected shield.

## Journal contract progress

- Restore the retail contract-row fonts, margins, columns, and selection artwork.
- Show each contract's own progress text and remaining count in the status column.
  Long progress text wraps and increases the row height so kill counts stay visible.
- Keep contract descriptions in the notes area and truncate long names within
  their own column, preserving space for progress.
- Adjust scrolling for taller rows so entries are not skipped at larger UI scales.

## Validation and distribution

- Regression coverage includes unenchantable equipment at the retail property
  threshold, carried and equipped shields, late appraisal, explicit buff bindings,
  confirmed buffs, equipment handoff, and continued combat.
- Rendered journal checks cover real DAT contract text, small and large remaining
  counts, scrolling, and 100% and 150% UI scales.
- Ship matching Windows, Linux, and Quest versions. Packages exclude account
  settings, personal saves, server data, and the original game's DAT files.
