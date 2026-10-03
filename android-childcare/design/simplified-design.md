<p align="right"><a href="simplified-design.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Simplified childcare navigation and household lifecycle

Revision 2, 2026-10-03, Asia/Shanghai. The user requested fewer mobile menus, one voice/AI entry routed by intent, person registration/management/updates and a sustainable operating lifecycle. This revision supersedes the four-root navigation in revision 1. It changes the design artifact, not the delivered APK or backend.

Interpretation pending the owner's clarification: people means the default baby plus household caregivers, and maintenance includes App version identity and updates. Registering a local person is not registering a cloud account. No cloud accounts, invitations, permission delegation or online updates are declared available without actual backend support.

## Three destinations

| Destination | User purpose | Content |
| --- | --- | --- |
| Care | Say/type what is needed | One conversation composer, current baby/caregiver, active sleep, intent/result cards, recording and media controls |
| Records | Inspect and correct facts | Date/range, same-source totals, timeline, edit/delete/restore and export |
| Household | Maintain people and the application | Baby profile, caregiver registration/current actor, profile updates/archive, service/data status, version/backup/storage |

Q&A, Story, library, reminders and handoff stop being separate root menus. They are intents/results inside Care; focused editors, sound selection and media details are secondary surfaces opened from an active task. Typing and voice use the same routing. Suggested phrases fill the composer and never execute automatically. Back dismisses the active sheet, then returns to its root. A root change starts at the top; refreshing the same conversation preserves its useful position.

## Unified input and intent routing

The composer reads “Record something, or say what you need.” Voice has explicit ready, permission, capture, recognition, editable transcript, cancellation, interruption and failure states. Transcript insertion does not send. Microphone denial/unavailable recognition always leaves system typing usable. Backgrounding releases capture; retain existing text, never claim lost raw audio was recovered.

| Intent | Route | Confirmation and offline behavior |
| --- | --- | --- |
| Feeding/diaper/bath/tummy-time | Structured care-event preview | Show child, actor, quantity/type and occurrence time; confirm before durable write; offline outbox |
| Start/end/backfill sleep | Persistent sleep-state or interval preview | Confirm mutation, preserve active timer across navigation/process loss; no active sleep produces an explanation |
| Day query/handoff | Local query/result card | Explicit local/confirmed range, timezone and freshness; online enhancement optional |
| General question | Owned Q&A task | Show text and selected record context before actual submission; unavailable service retains draft |
| Story | Owned Story task | Same entry, distinct task identity and result/media owner; text and speech have independent failure states |
| Play/stop/pause | One native player | Choose a real local item or supported service catalog; mic capture coordinates audio focus |
| Reminder | Time/title confirmation form | Persist locally; notification permission/scheduling state explicit |
| Register/update person | Explicit profile form and change preview | No free-form model mutation; local transaction and versioned identity |
| Ambiguous/multiple intents | Clarification card | Ask which action and target; retain transcript; perform no side effects |

Do not silently route an ambiguous request to a generic answer, guessed person or arbitrary record. Mutating/deleting/importing intents always preview their target and change. A user can cancel without a write. An ordinary informational response can be read without another confirmation. Routing can use tested deterministic local commands and capability-gated model interpretation; the UI must not promise a nonexistent classification endpoint.

Each conversation item has a stable request ID, intent, profile/actor, original input, state, result and media owner. One entry does not imply one shared mutable last response. Q&A and Story retain separate typed requests. Late results update only their original card. Switching baby/person or page never reassigns old work. Cancellation invalidates callbacks; retry follows actual backend idempotency. Persist submitted state before network work, recover interrupted tasks after process loss, and never autoplay after recovery.

A result card explains its actual status: awaiting confirmation, saving, saved locally/pending sync, working, cancelled, interrupted, retryable failure, conflict or unavailable service. The prototype labels AI/media samples as layout demonstrations. Production uses actual capability/status results, not demonstration replies.

## Local person registration and management

First launch creates the existing default baby ID with nickname only. No invented birthday, caregiver, age or login is required to record. Unknown actor is a real unset value, not a fake family member. Optional registration can be completed later.

Caregiver registration: Household → Register caregiver → enter required name/call-name and relationship, optional note → review “local person, not account” → save locally → select as current recording actor. Empty names and storage failures retain the draft with inline reason and retry. Only a successful local transaction shows saved. The prototype persists sample family profiles in this browser; it does not update the Android DB or cloud.

Profile details show current actor, local/cloud support status and revision. Update keeps the same person ID, previews changed fields and increments the profile revision. Switching actor changes new record attribution, not baby ownership or existing history. Archive asks confirmation, removes the person from new-record selection and retains references on prior records; clear current actor when archived. Restore, when implemented, reuses the old identity. Never let archive masquerade as revoking a cloud account permission.

Baby updates preserve the same default baby ID and event links. Nickname is required; birthday optional and cannot be future; notes optional. A changed birthday updates displayed age, not historical event timestamps. Timezone updates explain statistical day-boundary changes. Multiple children/households are outside the agreed initial scope unless the owner changes it.

Implementation stores people, revisions/archive state, actor attribution and durable operation queues in the same transactional local data layer as care records. Profile sync is enabled only when a real contract supports it; otherwise show “local profile.” No UI field is silently represented as synchronized. A future account/invitation flow requires a separately defined identity, authorization, revocation and recovery contract.

## Sustainable operation and version maintenance

Household → Version and maintenance groups actual installed package/version/signature channel, update source, last successful check, local storage/cache, backup/export and restore. Get installed identity from the package manager. “No update channel” is distinct from “Latest version.” A check failure preserves the previously known version and displays its age. An unconfigured/unavailable release endpoint does not show a functional Check update button or fake success.

When updates are actually supported: inspect manifest/version/compatible package and signing identity, download incrementally with progress/cancel/retry, verify size/hash/signature, then hand off to the Android system installer. User performs the required system authorization. Never silently install or promise online updates without a configured verified channel. Until then explain trusted local APK installation and offer backup first. Keep the distinct childcare package and stable signing identity; do not borrow the adult app identity.

Local lifecycle: persist profiles/records/outbox atomically, restore active sleep, preserve drafts, reconcile tasks, schedule bounded sync/reminders, and expose storage failures. Cache cleanup excludes care history and profile records. Paginate history and conversation; bound media/network buffers rather than loading entire files. Offline use does not require a permanent service connection.

Upgrade migration is versioned and transactional, with a pre-migration backup and explicit rollback path. On failure retain the original database/queued operations and surface recovery; never reset silently. A backup preview shows profile/record counts, schema/version, timezone, pending status and exclusions (no credentials). Restore verifies integrity, previews merge/replace/conflicts and obtains confirmation before writes. Respect server generations so a stale backup cannot revive cleared data. Unsupported restore is described honestly rather than displaying a working completion flow.

Operational statuses include last successful sync/backup/check, pending/conflict count, interrupted tasks, reminder permission and usable storage. Avoid a new technical menu per status: show a concise group and open details only when actionable. Do not promise perpetual background execution.

## Visual and implementation handoff

Retain warm cream/forest/peach tokens and 48 dp minimum targets. Three bottom labels are Care/Records/Household. The unified composer is the main action and stays above the bottom navigation; conversation content scrolls independently so voice and Send remain reachable. A compact baby/sleep status row supplies context. Use grouped lists for people and maintenance. At large font sizes allow wrapping, stacked actions and scrollable forms. Native controls use system IME/date/time; TalkBack descriptions identify intent, actor, target and status.

Review [prototype.html](prototype.html). Existing revision-1 screenshots/native review remain historical evidence; the APK has not been declared converted to this revision. Prototype registration really persists only demonstration browser profile data; AI, capture, playback, notification, sync/update/restore states remain simulations or explicit unavailable handoffs. Required implementation acceptance covers ambiguity, confirmation/cancel, duplicate writes, mode/request ownership, profile save failure/retry, actor archive/history, process death, upgrade/restore and actual phone permissions/media.
