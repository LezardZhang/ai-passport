<p align="right"><a href="caregiver-timeline.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Caregiver timeline

Android 0.6.6-cloud and the management Records page show a vertical daily timeline. A time column and connected rail join activity cards: Dad at 08:30 fed 150 mL, Mom at 09:10 changed a diaper. Records use their actual occurrence time, ascending order and stable ID ties. Undated records appear in an explicitly labeled group after dated records; they are excluded from daily totals. Sleep overlapping the selected day remains visible.

## Registration and entry

The Android Family page maintains baby details and a durable caregiver roster, with one title field in “I am the baby’s ___”, editing and deactivation. A title such as Dad, Grandpa or Grandma is enough; separate names and contact numbers are not requested. New records use the title as the existing caregiver-name snapshot, so cloud timelines display the same title without a server schema change. Existing members prefer their relationship and fall back to their stored name. New records initially use the selected caregiver. Feeding and diaper buttons open a confirmation form on the Records page; the unified Care composer also routes spoken or typed care actions to that form. The user can choose another caregiver for a retrospective entry or explicitly leave attribution unknown. Generic toilet entries do not invent a diaper type.

Every record retains a name snapshot. Renaming or deactivating a roster member does not rewrite earlier records. Editing an existing record initially retains its original snapshot; selecting a different caregiver is an explicit change. Rotation restores the editor and its caregiver choice. Milk volumes may be fractional; empty amounts, unknown diaper kinds and undated imported records remain unknown when their caregiver is edited.

The management baby page has matching feeding/diaper entry and caregiver-name editing, with suggestions from existing record snapshots. It retains the adult record table and editor. Names are rendered as text, including user-supplied names and notes. The Android roster is local: record attribution synchronizes, but phone numbers, roster management, authentication, invitations and a shared caregiver directory are not added to the server contract.

## Synchronization and compatibility

The personal API accepts optional `data.caregiver` as `{ "id": "opaque-local-id", "name": "Dad" }`; ID may be null for management-entered names. Names are nonblank and at most 40 characters; IDs are at most 128. Explicit null clears attribution. When an older client omits the field in an edit, the server preserves the current snapshot. Existing operation receipt identities and semantic digests remain unchanged.

The capability `childcare_caregiver: true` allows Android to upload attribution. A named record stays pending with an explanatory error if a service lacks this capability. A v3 operation already frozen without attribution is replayed unchanged, then followed by a new attribution operation after acknowledgement. Conflict recovery that keeps the local record also keeps its explicit caregiver choice, including clearing the name. Server/profile identity fences remain in force.

`GET /records?order=timeline` opts into ascending occurrence order. Paired `start`/`end` dates and an IANA `timezone` select a daily window; defaults remain compatible with earlier clients. The web client pins `at_revision` for pagination and resets pages when the date changes. Android uses 100 rows per page. No SQLite schema or server migration is required for this extension.

An actor-only edit preserves the original occurrence timestamp, interval, independently supplied sleep duration, source, media, unknown fields and nullable feeding/diaper details. Historical sleep with missing interval times stays historical rather than becoming an active timer. Dates use actual instants rather than lexical timestamp order; ambiguous or nonexistent local wall times require a different explicit time.

## Verification and operation

See [delivery evidence](delivery.md) for artifact hashes, exact test counts and physical-phone acceptance. New protocol and form regressions reproduced attribution loss in conflict recovery, replacement of unknown/custom details, invented times and recalculation of untouched sleep data before the repairs. Tests use disposable databases and local fixtures. Physical-phone checks use read-only navigation and a same-signer upgrade; synthetic records are never inserted into the household database.

The signed APK retains version 3's signer and SQLite schema 3. Install using `adb install -r`; never clear app data to upgrade. Preserve the private signing key for subsequent upgrades. Existing voice Send/ASR lifecycle, multiple service URL/key configurations and model discovery remain available.

The management changes are implemented and tested locally. Production deployment is a separate operation. Until its server advertises the new capability, named records remain on the phone awaiting synchronization. Large production histories, server sort capacity and web page accumulation have not been load-tested; limits and allocation owners are documented in the [Android resource budget](resource-budget.md) and server resource budget (coordinated source: `../../backend/contracts/resource-budget.md`).
