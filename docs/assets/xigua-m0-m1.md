**English** · [简体中文](xigua-m0-m1.zh_CN.md)

# Xigua offline prototype: M0-M1

This increment implements the local interaction and persistence foundation of the
[product plan](xigua-implementation-plan.md). Hardware acceptance is pending.
It is separate from the existing quota firmware and contains no quota credentials.

This page records the M0-M1 baseline. The current source adds the
[M2 connectivity increment](xigua-m2.md); its network and AI configuration
instructions supersede the offline-only statements below.

## Scope and operation

- Boot into a newly designed Chinese card UI. UP/DOWN changes cards, OK enters.
- Hold UP for feeding entry (default 150 ml or the last amount); UP/DOWN changes
  it by 10 ml, OK saves. The initial valid range is 10-400 ml.
- Hold OK on feeding entry to switch between adding and correcting the latest
  feeding. This prototype gesture will be revised when PTT arrives.
- Sleep and tummy sessions have explicit start/stop actions. Diaper and bath
  records are available through their pages. A local timer offers visual alerts.
- Hold DOWN to return. Saved operations offer a five-second undo on the home
  screen. Undo targets the last committed operation and revision.
- History and cumulative statistics remain available offline. Unknown absolute
  time is shown explicitly; cumulative totals are not labeled as exact daily totals.
- Backlight dims after 30 seconds and turns off after 60 seconds. The first full
  gesture wakes the screen without saving or starting an action.
- Battery percentage comes from the BSP; missing readings remain unknown.

No Wi-Fi connection, provisioning, time synchronization, voice, or sound playback
is enabled in this increment. Those are later milestones, not silently simulated
features. A timer alert is visual until the audio milestone.

## Data contract and limitations

The prototype stores at most **128 events**, not the later 4,000-event target.
It never evicts old records automatically. Full storage rejects new entries.
Export, selective history deletion, and the larger journal are later work; do not
use this prototype as the only copy of important records.

The core uses explicit versioned little-endian snapshots with length/CRC checks.
The storage adapter adds two alternating NVS slots, a generation, and an envelope
checksum. One controller owns writes: apply to a candidate, commit the snapshot,
then publish the result. Failed/ambiguous commits stop further writes until reboot;
corruption falls back to a valid older snapshot in read-only mode without erasing
either slot. Invalid data is never automatically reformatted.

Successful command IDs are remembered for the latest 16 operations. Boot identity
is advanced and committed before new commands. This is bounded local replay
protection, not an unlimited synchronization history.

Same-boot durations use monotonic time. After a reboot, an unresolvable interval
is marked unknown rather than computing with another boot's uptime. A timer from
an uncertain earlier boot requires explicit cancellation/restart. The pure core
has trusted-time reconstruction support for later integration; this prototype
does not manufacture current UTC or a timezone.

## Firmware layout and data impact

The prototype retains existing NVS/PHY offsets and app start, shrinking the factory
slot to reserve a product-specific 256 KiB NVS partition at the end of Flash:

| Partition | Offset | Size |
| --- | --- | --- |
| nvs | `0x9000` | `0x6000` |
| phy_init | `0xF000` | `0x1000` |
| factory | `0x10000` | `0x7B0000` |
| xigua_data | `0x7C0000` | `0x40000` |

This supersedes the full-product draft partition table for M0-M1 only. It does
not change the upstream baseline contract or provide OTA. Initial installation
replaces the quota application and partition table. A merged write from `0x0`
can reset default NVS/PHY; it is not a promise of data preservation. Future data
layout changes require migration/export before flashing. Never erase all Flash
as an automatic repair.

## Validation and device handoff

Run `tools/validate.sh` in an activated ESP-IDF 5.5.3 environment. Focused tests
cover commands/time/snapshots, dual-slot failure cases, button release registration,
and short/long/wake gestures. The full gate also runs the repository baseline tests.
Build artifacts are under `build/`; only a verified merged image belongs at `0x0`.

On Windows, use UTF-8 Python mode for paths containing non-ASCII characters. Local
host compiler/actionlint shims belong in ignored `build/host-bin` and
`build/host-tools`; they are not product dependencies. Do not weaken tests when
the host lacks symlink privileges; report the environmental failure separately.

After an authorized flash, verify:

1. Cold boot without a network shows real Chinese text, battery state, and responsive keys.
2. Save feeding, correct it, and undo; each action has visible feedback and one expected record.
3. Start/stop sleep and tummy; reboot during a session and check the uncertain-time message.
4. Exercise diaper, bath, history, full capacity, and timer due/cancel behavior.
5. Let the screen turn off, wake it, and confirm the wake gesture performs no other action.
6. Test interrupted writes/commit recovery with disposable records before trusting persistence.
7. Inspect every label and error state for missing glyphs/clipping and repeat navigation.

Build, host tests, and physical observations must be reported separately. A successful
build or flash does not validate display, power-loss behavior, or battery life.

## Checkpoint

Current source work is on `feature/xigua-assistant` in a managed worktree. The
original `feature/quota-tool` checkout and its uncommitted changes are retained.
No commits, pushes, or flashes are part of this implementation step. Final artifact
identity and actual validation results are supplied in the task handoff; do not
treat the design document's historical results as this firmware's test result.
