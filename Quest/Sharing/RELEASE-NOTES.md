# AC:Unreal / AC:VR release 159

Windows desktop, PC VR, native Linux, standalone Quest, and the Android phone and tablet preview share version **2026.11.17.159**.

## Far terrain and scenery

- Thanks to **gvbz** for contributing far-distance terrain rendering and its camera and scenery controls.
- Enable **Far Terrain** in the graphics options to see the landscape beyond the usual view range. The terrain radius reaches 96 landblocks. Distant scenery has its own radius of up to 24 landblocks, with separate options for rooms, textures, and the horizon.
- Far terrain is off by default. Adjust its settings, then press **Apply** to update the world. This also lets you change several options in one pass.
- Distant land now picks up cloud shadows and blends into the weather fog. The update also reduces speckling on distant scenery.

## Skies, storms, and lighting

- The optional Legions sky follows the AC1 clock and weather, with the sun and moons following AC1 sky directions. Clear skies show the asteroid belt, while storms change the clouds, lighting, and visibility. Rain falls around the player.
- On desktop, choose your own Legions installation or `portal.dat` in the graphics options and select **Import**. On Quest, use **Install-AC2-Sky.cmd** from the revision 11 installer on your PC and then import the sky in the headset's graphics settings. Each player supplies their own Legions data.
- This release also updates dungeon fixture shadows on mobile renderers.

## Verification

- Automated render comparisons cover the AC1 and imported Legions skies at the same AC1 summit, including clear daylight and rain by day and night. The native Linux package passed its launch, weather, scenery, and updater regressions.
- The Quest installer and sky-transfer checks used a simulated USB device. A physical headset playtest is still pending.
