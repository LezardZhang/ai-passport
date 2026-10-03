<p align="right"><a href="caregiver-timeline-plan.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Caregiver timeline implementation plan

Goal: a daily vertical timeline in Android and the management page, showing the actual caregiver, occurrence time and activity, including milk volume and diaper details.

Architecture: preserve the existing SQLite/outbox and service/profile identity fences. Add optional `data.caregiver` containing a stable opaque `id` and a bounded `name` snapshot; explicit null means unknown. Old clients omitting the field retain an existing snapshot. Local family contacts remain local; record attribution synchronizes. Management users enter a caregiver name per activity; existing snapshots offer suggestions. This is attribution, not account authentication or a shared contact directory.

Display: ascending actual timestamps with deterministic ID ties; unknown times come last in a separate labeled group. Date filtering includes overlapping sleep and explicitly labeled undated records. A time column and continuous vertical rail join activity cards. Names are never inferred from the current login or selected caregiver for historical records.

Global constraints: preserve unrelated backend work, old APKs, private configuration and signer. No publication, push, data clear or firmware changes. Existing user authorization permits same-signer Android upgrades. Execute inline using executing-plans and meaningful TDD, then one fresh-context review. Current checkout is required because the Android project and backend contract are untracked concurrent work. Do not create unsolicited commits or repeat design approval gates for this continuation.

## Task 1: wire attribution and query ordering

- [x] Write failing backend tests for caregiver roundtrip/validation, old-client edit preservation, date/instant ordering, stable pagination and null times.
- [x] Implement optional caregiver schema, capability declaration and opt-in timeline/date query; retain existing default list order and frozen receipts.
- [x] Run the focused backend test suite. Expected: all new tests pass.

## Task 2: Android entry and vertical history

- [x] Write failing pure formatter/voice tests and Android persistence/sync assertions.
- [x] Add CareTimeline formatting, vertical rail, explicit caregiver picker including historical snapshot, rotation retention and ascending SQLite pagination.
- [x] Include attribution in pushes only when the backend advertises support; do not silently drop named attribution on unsupported services. Pull remote snapshots; explicit null clears, absent field retains local legacy attribution.
- [x] Run host and isolated emulator suites. Expected: attribution survives edits, rename, sync and restart; native timeline renders.

## Task 3: management entry and timeline

- [x] Write failing Node presentation tests for feeding/diaper/unknown values and ordering.
- [x] Add safe text-only activity cards, daily filtering/pagination, caregiver input and simple feeding/diaper form. Preserve adult editor, durable outbox, stale editor context and unknown data.
- [x] Run backend suite, web harness and visual preview. Expected: both named actions render and persist through existing sync APIs.

## Task 4: delivery

- [x] Build version 0.4.0-local/code 4 with the existing signer. Run complete repository gate and one independent review; fix material findings.
- [x] Update paired docs/resource budgets and retain evidence. Install with `adb install -r` on the identified Samsung without changing private records.
- [x] Report build/host/device results separately; distinguish emulator checks from human speech and locked-phone acceptance. Backend production deployment remains separate.

Review focus: omission versus explicit null, historical names after member rename/deactivation, editing actual caregiver explicitly, timezone/offset ordering, page boundaries and unknown-time groups, stale web editor fences, unsupported servers preserving named local data, text injection and no silent overwrite of remote fields.
