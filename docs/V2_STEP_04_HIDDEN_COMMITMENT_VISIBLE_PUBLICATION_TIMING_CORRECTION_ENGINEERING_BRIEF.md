# Step 4 Hidden Commitment and Visible Publication Timing Correction

Date: 2026-08-29

## Authorized scope

This correction implements the Product Owner-approved implementation-and-offline-proof Contract Gate and its lifecycle-cancellation clarification. It does not authorize deployment or controlled-live proof.

## Source diagnosis

- `modules/overlay/src/OverlayWindow.cpp` began one command timer at observation and retained an awaiting-first-frame identity while no visible accessory frame was required.
- A repeated zero-raster synchronization could publish `Committed` while the earlier first-frame state remained unresolved.
- `brain/src/BrainOwnedRuntime.cpp` applied one 500,000-microsecond check to aggregate `commandElapsedMicroseconds`, regardless of click, hidden, visible-publication, or cancellation applicability.
- `plugin/src/XVatsimPlugin.cpp` serialized only the ambiguous aggregate and click interval.

## Corrected contract

- Hidden mechanical commitment terminally reports `CommittedNoVisibleFrameRequired` exactly once.
- Later visible eligibility begins a separate, mechanically correlated publication attempt and never reopens the hidden command.
- Awaiting-first-frame state is established only for a required visible frame and retains one stable timer across repeated synchronization.
- Facts explicitly identify command-terminal versus visible-publication-terminal accounting.
- Diagnostics separately carry issue-to-commit, visible-eligibility-to-first-frame, click-to-terminal, intentionally-hidden, and issue-to-cancellation intervals with applicability markers.
- Brain liveness evaluates only applicable intervals with a strict failure boundary at 500,000 microseconds.
- A lifecycle cancellation with both click and cancellation breaches produces one aggregate failure classification for that terminal fact.

No METAR authority, IFR/VFR targeting, lookup spotlight, history, drawer, ORB, Arrival, worker ownership, or mouse-callback authority changed.
