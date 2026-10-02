<p align="right">
  <a href="memory-budget.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Memory and Resource Budget Rules

These rules are mandatory when changing buffers, queues, task stacks, caches,
fonts, image decoding, network protocols, audio, radio lifetimes, dependencies,
or partitions. They apply to the ESP32-C3 board with 8 MB Flash and no PSRAM.
The rules define a review and acceptance contract; a document does not itself
enforce task scheduling or allocation limits in firmware.

## One application budget

Maintain one current resource budget per application under `docs/assets/`,
linked from its design or handoff document. Update it in the same change as
resource-affecting code. General rules belong here; application sizes and
concurrency decisions belong in that budget. The baseline demo and derivative
applications need not have identical allocations or partitions.

Every budget must contain:

| Item | Required information |
| --- | --- |
| Identity | Source checkpoint, image/ELF identity, effective configuration and partition layout |
| Allocation ledger | Owner, maximum bytes/count, memory capability or Flash partition, lifetime, release point and allowed overlap |
| Execution phases | Resources alive in each phase, allowed background work and transition cleanup |
| Headroom | Numeric reserve for each constrained heap/pool, largest next allocation and reason for the reserve |
| Evidence | Measured workload, sampling method, minima, stack margins, recovery and unverified cases |

Label entries as **configured**, **estimated**, **measured**, or **unverified**.
An allocation estimate is not a measured peak; a successful short request is
not evidence for the maximum supported payload.

## Count the right memory

- Separate link-time `.data`, `.bss` and IRAM, live system heap, DMA-capable heap,
  LVGL's own pool, task stacks, and Flash. Report bytes or KiB consistently.
- Do not treat the chip's nominal SRAM or a linker's unused region as usable
  runtime heap. Different capabilities and contiguous blocks constrain allocation.
- Measure internal byte-addressable heap with
  `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`; measure DMA with the capabilities
  actually required by the driver. DMA and internal heap overlap; do not add
  their free sizes. Static pools and heap-allocated task stacks also must not
  be counted twice in totals.
- A static LVGL pool consumes RAM even when LVGL reports it as unused. Its
  internal fragmentation and failures require separate pool measurements.
- Determine font/image placement from the ELF/MAP and mapped addresses.
  Flash-resident constants consume Flash; decoding, glyph caches and dynamic
  loading may consume RAM. A large font is not automatically a large RAM user.
- Reserve application Flash and cache/storage Flash separately, following
  [firmware layout](firmware-layout.md). Moving assets between partitions does
  not create capacity; a new cache partition is not a baseline requirement.

## Resource ownership and concurrency

1. Give every buffer, queue item, audio device, temporary partition and task an
   explicit owner. Define ownership transfer and cleanup on success, failure,
   timeout, cancellation, page exit and shutdown.
2. Serialize expensive phases unless their overlap has its own measured budget.
   A resource reservation must cover payload construction, temporary copies,
   network/client setup, parsing and cleanup. Taking an HTTP mutex after
   allocating a large JSON tree or catalog shadow does not bound the peak.
3. Give microphone capture and speaker playback one coordinated audio owner.
   Stop I/O before releasing codec/I2S/DMA resources. Do not initialize idle
   audio ahead of a TLS handshake unless that overlap passes acceptance.
4. For cache-based speech, finish and validate the complete temporary audio,
   close networking and retire parser/request buffers before starting PCM.
   Streaming network audio needs a separately accepted overlap budget.
5. Defer background cloud sync, catalog refresh, scans and provisioning when
   their preparation or execution conflicts with the active phase. Deferral
   must preserve pending user records and retry them after resources recover.
   Bound radio windows and release tasks, timers and handlers when they close.
6. Shared reply storage must carry operation identity, mode and validity.
   Drain or reject stale results after mode changes or cancellation. Reusing
   storage must never make Story display Q&A text or change recorded statistics.
7. Keep button/UI callbacks non-blocking and obey LVGL locking. Do not hold a
   UI lock while waiting for networking or PCM; define lock ordering and bounded
   cancellation waits when introducing another resource lock.

## Allocation and payload limits

- Set finite maxima for input, response bodies, JSON nodes/records, queue depth,
  audio duration and cache size. Include terminators, alignment, allocator
  overhead, encoded expansion and simultaneous old/new `realloc` buffers.
- Prefer bounded chunks and ownership transfer where compatible with the
  protocol. A pointer queue saves copying only when its lifetime is safe.
  Do not place large payloads on task stacks or permanently enlarge buffers
  to mask a peak-lifetime problem.
- Check every allocation and task/queue creation result. Partial initialization
  must unwind without leaks or publishing invalid data; retry only after cleanup.
- Size stacks from the deepest accepted workload and high-water evidence,
  with an explicit margin. Record the API's units for the locked ESP-IDF version;
  do not reduce all stacks based on an idle reading.
- TLS receive sizes must support the provider's actual records. Changes to
  TLS buffering require protocol and allocation evidence. Do not disable
  certificate checks, watchdogs or stack protection to obtain headroom.

## Headroom and diagnostics

For each constrained capability/pool, budget the maximum remaining allocation
at a phase boundary, including permitted concurrent work. Admission requires:

```text
free_before >= remaining_peak_allocations + reserve
largest_before >= largest_next_allocation + fragmentation_margin
```

The application budget must give numerical reserves and justify them from
driver/UI activity, payload maxima and stress evidence. A reserve is an
engineering target, not a chip guarantee. Unknown peaks or capabilities cannot
be marked PASS. Recheck admission before later large allocations as the heap
changes; use phase arbitration as well as metrics to prevent competing work.

Capture free bytes, largest suitable block and minimum free bytes before,
during and after expensive phases, plus DMA, LVGL pool and relevant task-stack
headroom. Record firmware identity and phase/operation identity with the values.
`heap_caps_get_minimum_free_size()` aggregates individual heap low watermarks;
it is not a synchronized measurement of all heaps. Largest-block samples are
also not a continuous lifetime minimum. State these limits in the evidence.
See [ESP-IDF heap diagnostics](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c3/api-reference/system/heap_debug.html).

Allocation-failure diagnostics should retain requested bytes, capabilities and
phase. Hooks must not allocate, block or trigger recursive logging. Enable
tracing or corruption checks for bounded diagnostics and account for their own
overhead. Routine logs must not expose keys, private recordings or reply bodies.

## Change acceptance

Before adding or enlarging a resource, update the ledger and phase budget. If
the reserve cannot be demonstrated, reclaim memory or serialize work first.
Feature cuts require an explicit product decision; preserve record integrity,
statistics consistency and mode ownership during optimization.

For resource-affecting firmware changes:

1. Run focused tests for boundaries, failed/partial allocations, cancellation,
   ownership transfer and cleanup; then the [complete gate](build-and-test.md).
2. Measure the matching firmware on the identified device at maximum supported
   workloads. Include repeated phase/mode transitions, long replies/full lists,
   reconnects, retries and opening/closing provisioning where applicable.
3. Compare equivalent idle states after warm-up and completed cleanup. Require
   no downward memory trend, no allocation failure, no stack/pool exhaustion,
   and compliance with the documented reserves and largest-block requirements.
   Declare cycle count and any bounded retained cache; unexplained loss fails.
4. Verify user-visible correctness and audible/display behavior separately.
   Preserve NVS/data and follow the authorized flashing policy. Deliver Build,
   Host tests, Device tests and Unverified separately.

Existing deficits must remain visible in the application budget. A bounded
repair may reduce a known deficit before full compliance is possible; record
before/after evidence and remaining gaps. Do not declare sufficient capacity
or add unrelated features while the deficit remains. Static checks verify
document structure and firmware checks verify image fit; neither currently
enforces runtime reserves or proves memory stability.
