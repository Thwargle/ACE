# Partial-stack vendor sales

The sell cart preserved a chosen quantity but still referenced the original
inventory object's GUID. The server sells that object; a smaller amount in the
Sell packet does not create a partial stack.

Retail reference: `ThirdParty/acclient-AI-RE/2013-09 11.4186/acclient-src/src/GAME/game_ui_misc/gmVendorUI.c`,
`VendorSellUI::AcceptDragObject` and `VendorSellUI::ItemAttributesChanged`.
Retail first splits into the source container, then replaces the sell-list entry
with the new server-created item. `Source/ACE.Server/WorldObjects/Player_Commerce.cs`,
`VerifySellItems`, confirms that accepted sales resolve to whole inventory objects.

Changes in the shared desktop/VR client:

- Request the selected split amount in the original container and wait for both
  the new contained object and the source stack's reduction before staging it.
- Ignore pre-existing identical stacks; stop rather than guess if multiple new
  matching stacks appear. Limit the wait and abandon pending staging when the
  vendor closes, changes, or the sell list is cleared.
- Preserve the quantity when an item is dropped on a vendor who must first open.
- Keep whole-stack and whole-backpack sales intact. Splitting a staged stack via
  the selected Sell button waits before selling the confirmed new item.
- Reject Sell packets that would pair a partial quantity with an unsplit object.

Regression coverage exercises the real binder drag/drop and outgoing packet
encoding using isolated loopback transport: one MMD from a stack of 250, the new
GUID and exact amount in the Sell packet, preservation of the other 249, reply
ordering and containment, pre-existing identical items, duplicate input,
whole-stack sales, timeout, and vendor close. Existing buying and backpack-sale
checks run in the same UI suite. Additional checks cover reducing the quantity
of an already-staged stack, resuming that explicit sale only once, and cancelling
pending staging with Clear All.

Build/test evidence is recorded under
`Unreal/Saved/ReleaseValidation/sep26-vendor-split/`.
The Windows editor and Android development builds passed. The final
`ACE.RetailParity.UIScreens` suite passed with no errors, warnings, or skipped
tests (`Report-final/index.json`). Quest's shared source check reports no
differences. The loopback tests do not send transactions to a real account.
This change is not yet published and has not been exercised on a live server.
