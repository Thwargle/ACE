# Desktop zoom and field-of-view review — 2026-09-26

Implemented in the shared client source and synchronized to Quest. Windows editor/game and Android development builds pass. This change has not been published or installed on a headset.

## Retail comparison

Reference tree: `ThirdParty/acclient-AI-RE/2013-09 11.4186/acclient-src/src`.

- `GAME/acclient/gmClient.c`: the game FOV preference ranges from 10 to 160 degrees. `gmConfigUI.c` supplies the 90-degree default.
- `PORTAL/smartbox/SmartBox.c`, `GetOverrideFovDistance` and `CreatureMode::Draw`: divide the game FOV by `(viewport aspect - 0.1)` for the vertical projection. Convert that result to Unreal's horizontal angle. Multiplying the already-converted horizontal FOV gives a different lens curve.
- `PORTAL/smartbox/CameraSet.c`, `Closer` / `Farther`: scale the offset by `1 ± elapsed * adjustmentSpeed * 0.2`; default adjustment speed is 40. A fresh input uses the current frame interval. Near steps below 0.5 AC units are rejected without entering first person. Far steps are limited per offset axis (10 AC units horizontally, 450 vertically), rather than by a ten-unit sphere.
- `PORTAL/smartbox/CameraManager.c`, `UseTime`: interpolate camera translation by `stiffness * elapsed * 10`, clamped to one. The default stiffness is 0.45. Unreal's spring-arm lag alone does not interpolate arm-length changes.
- `GAME/game_ui_misc/gmSmartBoxUI.c`, `PostInit`: portal space selects `CreatureMode::UseSmartboxFOV`. Its ordinary lens therefore follows the saved preference too.

## Changes

- Store FOV in degrees, expose the retail 10–160 range, and convert the preference before applying the viewport aspect ratio. Explicitly maintain the calculated horizontal FOV so Unreal does not apply another vertical-axis adjustment.
- Preview FOV immediately while Config is visible. Show the current degree value beside the slider. Apply saves it; Reset restores the applied value. Closing the panel or changing tabs ends an unapplied preview. Existing multiplier preferences are migrated as `90 * old multiplier` when no degree preference exists.
- Mouse-wheel and keyboard zoom share the elapsed-frame calculation, bounds, and first-person behavior. Accumulated wheel detents retain individual multiplicative steps. Zooming closer while in first person does nothing; zooming farther restores the retail departure offset. Numpad 5 remains the first-person toggle.
- Blend zoom distance with the retail translation stiffness before spring-arm obstruction handling. Preserve the overhead camera's separate bounds and collision behavior.
- Portal space uses the saved FOV. The existing cotangent-based world-reveal blend now ends at the configured FOV instead of reverting to the default.
- Headset projection remains controlled by the VR runtime; this preference controls the desktop camera.

## Validation

Evidence: `Unreal/Saved/ReleaseValidation/sep26-camera-review`.

- `Report2/index.json`: CameraAndEdges, UIScreens, and WorldEntry all pass; zero failed or skipped tests. The camera fixture reports one no-world-context warning on temporary actor destruction; WorldEntry reports its three expected unsafe-spawn recovery warnings.
- Numeric retail projection references cover 4:3, 16:9, and 21:9. Portal blend endpoints and monotonicity cover preferences from 10 through 160 degrees. Camera-component tests verify the aspect constraint and portal preference.
- Zoom tests cover 30/60/144 FPS, keyboard/wheel target agreement, interpolation, rejected near/far steps, first-person entry/exit behavior, and angled offset bounds. Existing overhead and world-entry camera checks remain passing.
- UI tests cover live preview, switching tabs, Reset, pending changes, Apply's preference path, persistence to disk, and legacy preference migration. The rendered settings panel was inspected for label placement and clipping.
- `build-editor-final.log`, `build-win64.log`, and `build-android.log`: successful development builds. `Quest/Sync-ClientSource.ps1 -CheckOnly`: zero differing files.

These checks compare source behavior and automated fixtures; a live side-by-side camera-feel comparison with the retail client has not been performed in this pass.
