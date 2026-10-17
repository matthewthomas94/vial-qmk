---
id: VQ-2
title: Bridge host slots - lock to the selected host, pair without kicking saved hosts, recover from keyboard unplug
status: ready
priority: high
execution_mode: implementation
depends_on: []
run_id: null
canceled: false
worker_model: strong
worker_effort: high
worker_sizing_rationale: "ESP32 Bluedroid address, bonding and privacy behaviour is ambiguous and only partly testable off-hardware, and a wrong choice can break existing pairings."
worker_provider_notes: "none"
---

## Description

The Hidpress S3-Zero USB-to-BLE bridge
(`keyboards/hidpress/bridge-diagnostics/s3_zero_usb_bridge/`, mainly
`bridge.cpp`) keeps three host slots. The keyboard picks a slot by sending
reserved consumer usage 0x0011-0x0013 (hold encoder push and turn). Dropping
the current host when the slot changes is correct and must stay. The user hit
three problems on 2026-10-17:

1. **Switching back to a saved slot does not reliably reconnect.**
   `selectSlot()` opens a 15 s `switching` window in which only the selected
   slot's host is accepted, then falls back to accepting any saved host. After
   going slot 1 -> 2 -> 1, the Mac did not reconnect inside the window and
   stayed disconnected.
2. **Choosing an empty slot kicks saved hosts repeatedly.** `startPairing()`
   advertises openly. Saved hosts (the Mac in slot 1) reconnect, finish
   encryption, then get disconnected through `rejectHost`. This
   connect-then-kick loop likely makes macOS back off from reconnecting,
   which feeds problem 1.
3. **Unplugging the keyboard latches red.** `EventKind::Error` (from
   `HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR`), or `opened[slot]` still set on
   re-attach, sets `failed`, which stops forwarding until the bridge restarts.
   A messy physical unplug can produce a transfer error. Red also hides the
   pairing and switching LED states.

Target behaviour (similar to how ZMK keyboards handle profiles):

- Selecting a saved slot: the bridge stays on that slot until the slot changes
  again (no timeout fallback to other hosts), only that slot's host can
  connect, and it should reconnect without the user clicking Connect.
- Selecting an empty slot: pairing mode for that slot as today (flashing
  magenta; BOOT held 5 s still re-pairs the selected slot), but hosts saved in
  other slots are not connected and then disconnected while it waits.
- Keyboard unplug/replug while the bridge runs: release all inputs, close the
  interfaces, and return to amber then green on replug with no restart. Red
  remains only for errors that genuinely need a restart.

The mechanism is the worker's call; evaluate and document it. Candidates: an
advertising filter accept list for saved slots; a distinct local BLE address
per slot (as Logitech Easy-Switch does) so a saved host never sees another
slot as its device; directed advertising. Account for macOS resolvable
private addresses: check whether the address reported in
`onAuthenticationComplete` and stored in `slots[]` stays stable across
reconnects, or should be replaced with the bond's identity address.

Keep unchanged: QMK firmware, the slot usages 0x0011-0x0013, the HID report
map and report routing, boot-time behaviour (start on slot 1, accept any saved
host), and the power/USB-role guards in `startHost()`.

## Acceptance criteria

- [ ] Selecting a saved slot keeps the bridge on that slot with no time limit; only that slot's host can connect; the LED flashes white until it does.
- [ ] While waiting on a selected or empty slot, hosts saved in other slots are not connected and then disconnected. If the BLE stack makes this impossible, the README states why and what the closest achievable behaviour is.
- [ ] Selecting an empty slot still enters pairing mode for that slot, a newly paired host lands in that slot, and BOOT-5 s re-pairing still works.
- [ ] A USB transfer error or detach from the keyboard is handled as a disconnect (inputs released, interfaces closed, `opened[]` cleared), and the next keyboard attach resumes forwarding without a bridge restart.
- [ ] The README's Host Slots section, LED table and upgrade notes describe the new behaviour, the chosen BLE mechanism, and whether existing pairings survive the upgrade or exactly what must be re-paired.
- [ ] Slot and pairing decision logic that can run off-device has host-side tests next to `tests/report_router_test.cpp`; those tests and the README's `arduino-cli compile` command pass. Do not upload to any device.
- [ ] The README gains a short manual test list: Mac on slot 1, a second device on slot 2, switching 1 -> 2 -> 1 several times without clicking Connect, empty-slot pairing, and keyboard unplug/replug. These need the user's hardware: if everything else is complete, finish as `verification_blocked` with blocker "physical BLE/USB test on the S3-Zero with the user's Mac and a second device" and resume "user uploads the build and reports the manual test results".

## Required inputs

- [x] Integration base: the slot firmware exists only on branch `claude/hidpress-s3-bluetooth-bridge-eb0170` (worktree `.claude/worktrees/vibrant-bhaskara-8f595f`, head `d93f103599`), not on `vial`. Decide whether to fast-forward `vial` to that branch before dispatch. The main checkout's untracked, older `keyboards/hidpress/bridge-diagnostics/` copy (holding private git-ignored `backups/` flash images and `logs/`) blocks the fast-forward and must be preserved or moved first, not deleted.
  - Resolved 2026-10-17: user approved merging the branch into `vial` (merge commit `a0d252ad62`; a fast-forward was no longer possible). The git-ignored `backups/`, `logs/` and `build/` folders were kept in place under `keyboards/hidpress/bridge-diagnostics/`. Work from `vial`.
