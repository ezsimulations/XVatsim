# FAA/NASR Frequency Decision Hardening Recap

Date: 2026-05-22

## Summary

XVatsim hardened controller relevance by adding endpoint-scoped FAA/NASR
frequency facts as decision evidence for airport local and terminal APP/DEP
controllers.

This was driven by the KONT/KLAX SoCal problem: different airports can share
the same public TRACON name, but use different frequencies. A string-only
SimAware terminal-name pass is not trustworthy enough by itself.

The new decision shape is:

- Source 1: brain-owned radio board candidate exists.
- Source 2: SimAware-derived endpoint terminal/airport authority facts.
- Source 3: FAA/NASR endpoint frequency facts for the departure and arrival
  airports.

The brain evaluates candidates one by one. Modules only provide facts.

## Architecture Boundary

The repo must stay clean:

- The plugin remains the X-Plane shell only.
- No feature-specific decision logic belongs in `plugin/src/XVatsimPlugin.cpp`.
- The brain owns workflow, relevance, accept/reject, caching cadence, fallback
  eligibility, and final UI display truth.
- Modules are workers. They do not decide, they do not talk to each other, and
  they do not publish UI state.
- Worker modules say, effectively: "I collected the data you asked for."
- The brain decides what that data means.

When a new fact source is needed, the clean path is a new removable module with
one clear job. That makes bugs easier to isolate and lets a capability be
removed later without patching unrelated code.

## Added Worker Fact Source

New module:

- `modules/airport_frequency_catalog`

Purpose:

- Parse FAA/NASR `FRQ.csv` rows.
- Keep only rows for the active departure and arrival airports.
- Classify useful roles: `ATIS`, `GND`, `TWR`, `DEL`, `APP_DEP`, `CTAF`, and
  `UNICOM`.
- Cache by airport pair and source generation.
- Return facts to the brain.

It does not decide whether a controller displays.

## Brain Decision Change

Updated brain worker:

- `brain/src/BrainControllerRelevanceWorker.cpp`

New behavior when FAA endpoint role facts are available:

- Exact endpoint frequency + compatible role can accept a terminal candidate
  even when SimAware text ownership misses.
- Exact endpoint frequency miss can reject a terminal candidate that would have
  passed by broad/shared SimAware text alone.
- If FAA endpoint role facts are unavailable, the brain falls back to the
  existing terminal-authority behavior instead of failing solely because one
  source is missing.

KONT/KLAX guardrail:

- KONT SoCal APP/DEP frequency: `127.000`.
- KLAX SoCal APP/DEP frequency: `124.300`.
- `SCT_APP 124.300` is rejected for KONT when KONT FAA APP/DEP facts are
  present.
- `SCT_1_APP 127.000` is accepted for KONT.
- A callsign text mismatch can be rescued by exact KONT FAA APP/DEP frequency.

## Verification

Focused checks passed:

- SoCal KONT frequency-disambiguation scenario.
- Existing KSDF FAA frequency proof scenario.
- Existing KONT terminal-authority scenario.

Full verification:

- Full regression harness passed: `250 / 250`.
- Release plugin build passed.

Installed X-Plane plugin:

- Path: `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- SHA256:
  `10937952D56614905E144EBD9DD1C4CE203349A4AEFB88B85B16A2766016CC42`
- Previous installed build backup:
  `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl.bak-20260522-124450-C11962103BA2`

## Important Live Runtime Boundary

The decision path, cache types, parser worker, and harness proof are in the
repo and the current build is installed.

The live FAA/NASR source-acquisition path still needs a future approved gate.
The plugin must not grow decision logic for this. The clean next step is a
brain-owned request that runs a worker/source adapter to provide current
FAA/NASR `FRQ.csv` facts, then commits those facts back to the brain cache.

Do not claim a live flight has proven FAA/NASR frequency disambiguation until
the live source-acquisition path is wired and the diagnostics show the
departure/arrival FAA frequency cache is populated.

## Next Session Must Remember

- Callsign text is a clue, not final truth.
- Frequency alone is not final truth either.
- Radio board + FAA endpoint frequency is high confidence.
- Radio board + FAA endpoint frequency + SimAware endpoint authority is
  extremely high confidence.
- If two of the three evidence sources fail, reject and hide.
- Keep the work scoped to the active route endpoints and current radio board.
- Do not build a global alias engine.
- Do not add module-to-module shortcuts.
- Do not put decision code in the plugin.

## Post-Recap Display Contract Note

After this FAA/NASR recap, live UPS2183 KONT -> KDFW exposed a separate display
contract issue involving `SCT_APP 128.050`.

Important distinction:

- Controller Relevance correctly accepted `SCT_APP 128.050` as
  `CURRENT_POLYGON`.
- The bug was downstream display ownership: non-center final display rows were
  not preserving accepted-completion relation facts, and old standby/next/online
  display assumptions could make a current-polygon row look orange/standby.

The source fix is documented in `docs/NEXT_SESSION_HANDOFF.md` and reinforced
in `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`.

Do not use radio tuning as polygon proof. Do not fix display color by painting
a tuned row green. The brain-approved relation decides row color; radio tuning
only decides `Active`, and standby assist only decides `Standby`.
