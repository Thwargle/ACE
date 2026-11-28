# AC:Unreal / AC:VR v170

Windows, PC VR, Linux, Quest and the Android preview share version **2026.11.27.170**.

- Imported AC2 trees now use simpler meshes at a distance. Nearby trees keep their full detail.
- Far scenery loads nearby objects first and builds ready scenery between streaming updates. In the Sawato desktop benchmark, the same 142,214 instances appeared in 3.0 seconds, down from 33.4 seconds.
- Windows batches visibility checks for crowds and scenery. In a fixed-camera Yaraq test with 32 characters and AC2 trees, average frame rate rose from 124 to 140 FPS at 1080p on an RTX 5090.
