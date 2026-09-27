# Optional automatic updates

The launcher Updates page now offers **Automatically download and install updates**. It is unchecked for new profiles and existing profiles without the new setting. The choice is saved in the device's existing encrypted launcher profile and restored when the launcher reopens.

With the option enabled, the launcher checks on startup and every 15 minutes while idle. A newer release follows the existing streamed download, size/hash verification, and platform installer path. Windows restarts in the same desktop/VR mode; Quest retains its required system permission and installation confirmation. Manifest compatibility and published version numbers are unchanged.

Automatic checks, downloads, and installation cannot start after login begins or during gameplay. Starting an automatic download opens the Updates page. Cancelling pauses automation for the remainder of the app session (Check now or explicitly re-enabling the option resumes it). Failed downloads and cancelled/failed Quest installation attempts cannot create a retry or permission-dialog loop. Opting out during an automatic download/verification cancels that work and invalidates pending callbacks. Manual updating remains available.

Manual and automatic installation display an eight-second, cancellable notice before handing off to the platform installer. The notice explains that the game will close during installation. Quest users are told to confirm the system prompt, wait for installation to finish, and reopen AC:VR from their library; Windows users are told the game will reopen automatically. The same explanation remains visible during Quest installation. Cancelling the notice retains the verified update for a later attempt.

Validation records are in `Unreal/Saved/ReleaseValidation/sep26-auto-update/`. Coverage includes default/migrated profiles, save/reload, the actual checkbox and reopened launcher, responsive desktop/headset captures, lobby-only scheduling, verification/cancellation, and one-shot installation attempts. The viewport fixture emits its existing diagnostic warning about viewport size.

The closure-notice follow-up is validated in `Unreal/Saved/ReleaseValidation/sep26-update-notice/`. It additionally covers deferred manual/automatic installation, the eight-second deadline, duplicate install presses, preserved desktop/VR restart mode, cancellation/opt-out, and desktop/headset notice layouts.

This change does not publish a release or install on a headset. Actual replacement of an installed client using this new option remains a release smoke-test step.
