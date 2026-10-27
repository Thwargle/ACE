# AC:Unreal / AC:VR release 140

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.27.140**. Android version code: 140. Quest installer revision: 9.

These notes cover all changes since **v139 / 2026.10.27.139**.

## UCM appraisal and combat

- Preserve assessed weapon and ammunition data when wielding, attaching,
  firing, reloading, or removing an owned item's world visual. These updates
  no longer cause unnecessary reassessment between looting and combat.
- Reset appraisal retry counts after successful assessments. Repeated successful
  assessments no longer trigger a false failure and five-second combat pause.
- Allow the first assessment immediately after startup, including on Linux,
  instead of treating an unassessed object as a recently requested assessment.
- Continue invalidating assessments when the server replaces or removes an item;
  replacement descriptions can be assessed without waiting on the old request.
  Missing appraisal data still blocks unsafe salvage and equipment decisions.
- Stop UCM's selection-based background scanner from automatically assessing
  other players, including fellowship members. Enemy assessments remain active.

## Chat settings

- Add an independent **Assessment notices** checkbox for the main chat window
  and each extra chat window under **Options > Chat**. Uncheck it to hide
  incoming assessment spam such as "tried and failed to assess you."
- Save each window's choice and keep the filter separate from Gameplay and
  Errors, so ordinary gameplay feedback and error messages remain available.

## Validation and distribution

- Add regressions for repeated successful and unanswered equipment appraisals,
  ammunition firing/reloading, item replacement/removal, selected-player
  assessment suppression, and chat filter routing and persistence.
- Ship matching Windows, Linux, and Quest packages without personal settings,
  account data, server databases, or retail DAT files.
- Automated and packaged-client checks do not replace live-server comparison,
  native Linux GPU testing, or physical Quest/PC VR acceptance.
