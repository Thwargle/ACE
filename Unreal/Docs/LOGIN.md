# AC:Unreal / AC:VR login

The shared login lobby runs on Windows desktop, PC VR and native Quest. **Launch** connects the current client and continues to the existing retail character-selection screen.

## Using the lobby

- **Browse servers** searches the community directory used by ThwargLauncher. Select an entry, review its address and description, and choose **Add to my servers**. Website and Discord buttons open the server's own links. Refreshing the directory never changes a saved server or sends credentials.
- **Custom server** accepts a name, host, port, ACE/GDLE type, description and optional community links. **Edit server** changes a saved entry. Removing a server leaves your saved accounts intact.
- **Accounts** are shared across all saved servers. Switching servers keeps the selected account. Select an account to restore its credentials, or use **New account**, fill the two fields and **Save account**. Launch also saves the entered account. Removing an account only removes this device's saved login; it does not delete the server-side account.
- **Game files** configures the directory containing `client_portal.dat` and `client_cell_1.dat`. `client_highres.dat` is detected and used when present. Windows desktop includes a folder picker; VR supports editing the folder path with its keyboard. Quest defaults to the app's installed DAT directory. Switching an installation that is already loaded or loading requires a client restart so cached textures and geometry cannot mix between installations.
- The layout scales automatically with the window or fullscreen viewport; there is no fullscreen button on the login screen. Narrow windows stack the cards and scroll. VR uses a dedicated 1100 × 900 surface, with the connection status kept visible at its bottom. Existing native Quest text entry is used for all fields, including masked password entry.

## Persistence and protocol

Profiles remain in `Saved/Login/LastLogin.dat`, encrypted with Windows DPAPI or Android Keystore AES-GCM. The original single saved login is migrated into one server/account pair. No credentials are stored in source, the public server catalog or release files. An app uninstall or Android keystore reset can remove saved credentials; this file is not intended as a portable password export.

The downloaded catalog is cached separately as `Saved/Login/Servers.xml`. Saved servers and the cached directory remain usable if a refresh fails. Directory entries are validated, unsupported emulator types and duplicate endpoints are excluded, and external links are limited to HTTP/HTTPS.

ACE keeps its existing `AccountPassword` authentication (type 2, separate account and password fields, preserving password case). GDLE uses `Account` authentication (type 1, `username:password` followed by empty crypto/extra-data buffers). Retail lowercases the entire `-a` argument, including the GDLE password; the client matches its ASCII case normalization on the wire without changing the saved credentials. Only the authentication payload contains that combined string; log messages and subsequent character operations retain the plain username. GDLE's connect acknowledgement includes the recipient ID assigned by the server; ACE retains its zero-ID cookie acknowledgement. Shared gameplay and VR capability negotiation are unchanged.

Retail `NetError` / `NetErrorDisconnect` packets now report the server's rejection immediately after validating the packet checksum, instead of timing out. A late cleartext login rejection cannot terminate an authenticated session. Error text includes the configured endpoint and the original string/table IDs, never the password.

Server ports are imported exactly from `server_port` or the legacy `connect_string`, persisted unchanged, and used for login (with port + 1 for the handshake acknowledgement). There are no server-specific port overrides. During the September 19, 2026 check, the current community list specified Harvestbud at `harvestbud.gdleac.com:9010`. The local older ThwargLauncher list labeled `harvestbud.gdleac.com:9000` as Harvestbud, but that endpoint identified itself as **Reefcull**. Use the current Reefcull entry for a Reefcull account; do not change Harvestbud's port to work around an account rejection.

Protocol references:

- [ThwargLauncher launch arguments](https://github.com/Thwargle/ThwargLauncher/blob/master/ThwargLauncher/ThwargLauncher/GameLaunching/GameLauncher.cs)
- [Community server directory](https://github.com/acresources/serverslist/blob/master/Servers.xml)
- [GDLE ConnectionRequest parser](https://github.com/esoterick/gdle-linux/blob/master/Source/Network.cpp)

## Validation

`ACE.Launcher.DirectoryAndProfiles` covers both catalog schemas, exact published ports, migration, shared account selection and encrypted multi-account persistence. `ACE.Launcher.ResponsiveUI` exercises add/edit/select/remove flows and renders desktop, headset-sized and narrow layouts into `Saved/Automation/Launcher`. `ACE.Launcher.ViewportPlacement` runs in a launched client (`-game`) and checks the actual player-screen slot and visible login-control bounds; direct render-target tests do not exercise viewport anchoring. `ACE.Network.LoginHandshake` checks ACE/GDLE authentication bytes, captures actual connect acknowledgements on loopback, and covers validated, corrupt, truncated and reordered rejection packets. The existing local credential and missing-DAT regression tests remain applicable.

`ACE.Network.LiveLogin` does no networking unless explicitly launched with `-ACELiveLogin`. It uses the selected encrypted local server/account profile, stops at character selection, and disconnects without entering a character. `-ACELiveLoginProfile=<path>` optionally selects a separate encrypted diagnostic profile. A live GDLE check on September 19, 2026 reached Reefcull's character list through the real client session. Native Quest login interaction and in-world GDLE compatibility still need headset testing. Render-target screenshots are layout checks, not a substitute for headset comfort testing.
