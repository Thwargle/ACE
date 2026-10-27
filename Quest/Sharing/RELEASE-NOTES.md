# AC:Unreal / AC:VR release 141

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.27.141**. Android version code: 141. Quest installer revision: 9.

These notes cover all changes since **v140 / 2026.10.27.140**.

## Vendor purchases

- Match retail's complete Buy request by including the vendor's currency field,
  including zero for ordinary pyreals. The missing field could prevent servers
  that read the complete request from processing purchases, leaving the cursor
  waiting on an hourglass. This addresses a protocol mismatch found while
  investigating the grocer purchase reports on Frostcull.
- Apply the correction to individual purchases and carts through native vendor
  UI, VR, UCM restocking, and Item Tools, which share the same purchase sender.
- Use the current vendor's advertised currency, including after switching
  between alternate-currency and ordinary vendors. Selling retains its separate
  retail packet format.

## Validation and distribution

- Add packet regressions for ordinary and alternate currencies, vendor changes,
  single-item and cart purchases, unsigned stock IDs, selling, and cursor
  completion. The previous Buy request fails the retail packet contract.
- Ship matching Windows, Linux, and Quest packages without personal settings,
  account data, server databases, or retail DAT files.
- Automated checks do not replace live Frostcull confirmation, native Linux GPU
  testing, or physical Quest/PC VR acceptance.
