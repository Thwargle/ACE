# AC:Unreal / AC:VR release 102

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.08.102**. Android version code: 102. Quest installer revision: 7.

## Movement and interaction synchronization

- Report final position changes after movement input stops, including ground
  settling and collision adjustments. Players should no longer need an extra
  movement input just to send these position changes to the server.
- Report changes of cell or ground contact promptly, while retaining the normal
  interval for other movement reports and avoiding repeated stationary updates.
- Keep position reporting active when movement is cancelled while airborne.
- Suspend position reports during portal entry and refresh the arrival position
  after portal completion. Refresh it again when a delayed server materialization
  acknowledgement arrives, covering servers that ignore movement while loading.
- Clear reporting state between characters and preserve the standard retail
  movement packet format and negotiated VR reporting behavior.

These changes address client-side gaps found while investigating DreamWeave.
A separate stationary-interaction callback issue was identified in that server
fork; it requires a server-side fix and is not changed by installing this client.
Live DreamWeave confirmation remains pending.

## Installation

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
