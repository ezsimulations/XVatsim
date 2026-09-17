# Diagnostic log volume gate

Gate: `DIAGNOSTIC-LOG-VOLUME-01`

Baseline: V2.0.1 with the uncommitted YMML Brain-evidence repair.

## Live evidence

The 15 September 2026 diagnostic log exceeded 193 MB and the prior daily log
exceeded 203 MB. The dominant record was `flight-loop-complete`, emitted once
for every callback even when the callback completed normally in tens or
hundreds of microseconds. Brain controller receipts and change-driven runtime
events were not the source of the excessive volume.

## Required retained evidence

Normal logs continue to retain:

- complete Brain controller vote receipts when their evidence changes;
- worker dispatch, completion, cancellation, failure, and backlog evidence;
- route, authority, ATIS, PDC/private-message, and lifecycle changes;
- slow-refresh records and individual flight-loop outliers;
- periodic flight-loop distributions with sample count, average, minimum,
  bounded P95 and P99, maximum, threshold counts, and cadence counts;
- diagnostics-writer queue, drop, formatting, and storage-failure counters.

## Flight-loop policy

Routine callback timings are reduced in memory without allocation or file I/O
on the callback path. The log receives one `flight-loop-summary` per 60-second
window. A callback at or above 10 ms is an outlier. Detailed outlier records are
rate-limited to one every five seconds during a sustained problem, while every
outlier is still counted in the summary. The existing 33 ms slow-refresh record
and 40 ms X-Plane debug warning remain active.

The summary records:

- first and last callback sequence;
- number of observed callbacks;
- average, minimum, P95 upper bound, P99 upper bound, and maximum duration;
- maximum refresh and diagnostic-submission duration;
- callback counts at or above 1 ms, 5 ms, 10 ms, and 40 ms;
- number of detailed outlier records emitted;
- accessory-path and active-cadence counts.

Partial windows are flushed when the plugin is disabled or stopped. Daily
date-stamped logs and three-date retention remain unchanged.

## Acceptance

- Routine callbacks do not produce individual log lines.
- A 60-second window produces exactly one statistical summary.
- A 10 ms callback remains individually diagnosable.
- Repeated outliers cannot flood the file faster than one detail row every five
  seconds, and the summary preserves their complete count.
- Brain vote receipts remain complete and change-driven.
- The diagnostics producer stays bounded and non-blocking.
- Release plugin build, the diagnostics performance probe, the Australia Brain
  evidence probe, and the complete regression suite pass.

## Deployment boundary

The candidate was installed for controlled online testing. The Product Owner
confirmed the full-flight result and authorized committing it on 17 September
2026.
