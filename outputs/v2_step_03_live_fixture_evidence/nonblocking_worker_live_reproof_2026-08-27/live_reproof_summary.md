# V2 Step 3 Nonblocking Worker Live Reproof

Status: PASSED — DIRECTOR REVIEW PENDING

## Configuration

- Controlled fixture-ON plugin SHA-256: `4D2D8BED2B1CF97DE5A02164D677D16751E55DDE8DC680AB8361A8F26E8C41B3`
- X-Plane and xPilot were launched from a fully stopped state and shut down normally.
- Darron reported every authorized functional test passed.
- The five supplied screenshots were copied byte-for-byte with their original filenames and capture metadata.

## Functional and visual observations

- All three synthetic histories were present after cold startup.
- METAR displayed the updated hidden-cache generation as its newest entry.
- ATIS displayed the long unbroken synthetic content.
- PDC displayed limited content and reached `END OF PDC HISTORY` at maximum scroll.
- Direct METAR to ATIS to PDC switching preserved the one-visible-drawer invariant.
- Re-click close, reopen-at-top, final-limit scrolling, dragging, resizing, and compact ORB attachment passed.
- Plugin disable/enable preserved the histories and closed the selected drawer as required.
- Darron observed no stutter, recurring processing, or diagnostic spam during the controlled idle interval and Plugin Admin observation.

## Final live aggregate

- Actions completed: 1,079
- Synchronous-wall within budget: 1,048
- Frame-cadence limited: 27
- Preparation limited: 4
- Synchronous-wall failures: 0
- Render-wall failures: 0
- Timing unavailable: 0
- Cadence-contract failures: 0
- Missed eligible draws: 0
- Retained or dropped violations: 0
- Threshold warnings: 0
- Unique draw samples: 1,078 for 1,079 action references; maximum fan-out was two.

Main-thread and render maxima:

- Dispatch wall: 86 microseconds
- Matching action-draw wall: 3,631 microseconds
- Combined synchronous action wall: 3,643 microseconds
- Rail rasterization: 1,195 microseconds
- Drawer rasterization: 2,534 microseconds
- Rail upload: 103 microseconds
- Drawer upload: 210 microseconds
- Completed accessory draw: 188 microseconds

Worker and nonblocking handoff:

- Startup attempts/successes/failures: 3/3/0
- Priority requested/succeeded: true/true
- Maximum queue depth and ready-cache count: 3/3
- Jobs requested/started/completed: 87/87/87
- Cancelled or stale jobs: 0/0
- Enqueue attempts/contentions/successes: 88/1/87
- Maximum enqueue call: 76 microseconds
- Maximum ready-publication call: 6 microseconds
- Maximum contiguous worker slice: 4,473 microseconds
- Final worker threads: 0

The largest preparation wait was 2,227,032 microseconds. It was recorded separately from synchronous main-thread work. Open/switch GDI measurement and wrapping on the simulator thread remained zero because the immutable worker-prepared plans were used.

The final raster reasons distinguish user-driven presentation scrolling and scale/resize work from history generation: drawer presentation-scroll was 1,051 while drawer content-generation was zero. The long idle interval continued to receive ordinary draw callbacks, but the evidence contains no stale preparation, retry storm, recurring content generation, recurring diagnostic, or independent idle work.

The previously accepted one-time V1 flight-plan construction event remains outside Step 3 accessory classification and was not treated as a Step 3 failure.

## Final disposition

The controlled live reproof satisfies the authorized functional, visual, lifecycle, nonblocking handoff, synchronous-performance, render-performance, and shutdown requirements. No fixture-OFF deployment, rollback, commit, receipt finalization, or subsequent roadmap work was performed.
