# V2 Step 4 Engineering Brief — VATSIM METAR

## Status and scope

Step 4 is an implementation candidate pending Director review. It adds one
removable asynchronous VATSIM METAR fact source to the accepted Step 3 ORB rail
and information drawer. It does not deploy a plugin, start X-Plane or xPilot,
or perform a live VATSIM request. Those activities remain separately gated.

The governing rule is unchanged:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

## Source and module boundary

The only production weather endpoint is:

`https://metar.vatsim.net/:icao?format=json`

`modules/metar` accepts one normalized four-character ICAO, an opaque
brain-issued request identity, and a request purpose. It performs one bounded
HTTPS request and returns immutable transport facts, the returned station, raw
METAR, completion time, payload size, and separately measured network elapsed
time. It does not inspect flight mode, choose a target, schedule itself, parse
or classify weather, mutate history, or publish UI state.

The normal module has no injectable fixture transport. The proof-only target
defines `XVATSIM_METAR_PROOF_FIXTURES` and is linked only by the regression
harness. The normal plugin links the production module through the generic
`AsyncFactWorkerHost` seam.

WinHTTP uses HTTPS, an exact host and bounded single-airport path, no redirects,
bounded phase and total deadlines, a 65,536-byte response ceiling, and a
4,096-byte raw-report ceiling. Cancellation signals the active operation,
closes the request handle, waits for callback closure, and joins within the
locked 500-millisecond shutdown limit.

## Brain-owned policy

The brain alone owns target resolution, dispatch, priority, freshness, cache,
parsing, classification, history, transient lookup presentation, lifecycle,
and the final ORB/drawer view model.

- IFR Departure targets only the accepted flight-plan departure.
- IFR Enroute and Arrival target only the accepted flight-plan arrival.
- VFR uses only a valid departure from the existing committed flight-plan fact,
  whose source is `OnboardFms` or `CurrentLocation`; otherwise the automatic
  primary remains unavailable/unknown.
- A new or missing primary has first priority, a one-shot pilot lookup second,
  and a routine primary refresh third.
- Primary revalidation is eligible every 60 seconds. Consecutive transport
  failures back off for 120, 240, then at most 300 seconds.
- Only one request may be active. Target and lookup generations reject obsolete
  completions.
- Observation freshness is resolved from `DDHHMMZ` against the adjacent month
  candidates, rejects reports more than five minutes in the future, and becomes
  stale after 90 monotonic minutes. A still-fresh cached report may survive a
  transient source failure; stale data is gray/unknown.

The parser covers METAR and SPECI prefixes, observation time, statute-mile and
metric visibility, fractions, `9999`, `CAVOK`, runway visual range exclusion,
cloud layers, vertical visibility, NSC/NCD, and remarks isolation. Only BKN,
OVC, and vertical visibility establish a ceiling. Missing or ambiguous evidence
returns UNKNOWN. The worse defensible ceiling/visibility category controls.

## Lookup interaction

`Select METAR Airport…` forwards a bounded text fact. The brain strictly accepts
exactly four ASCII alphanumeric characters after safe normalization and rejects
empty, oversized, wildcard, traversal-shaped, and `all` requests.

A valid submission immediately selects the METAR drawer and publishes
`FETCHING METAR — <ICAO>` for no more than 20 seconds. Success replaces it with
an eight-second lookup spotlight. Failure publishes a bounded four-second
failure state and returns to the pinned primary. Manual close or a switch to
ATIS/PDC revokes presentation ownership; completion cannot reopen or switch the
drawer and starts no unseen timer. A valid changed completion may still enter
truthful chronological history. Any material primary presentation change
preempts a lookup spotlight immediately.

The lookup never replaces the automatic primary, never changes the METAR ORB,
and never receives periodic refresh.

## Publication and quiet runtime

Content, source health, freshness, and presentation have independent
generations. Identical raw content does not reparse, mutate history, wrap, or
rasterize, but a successful response may recover source health. The overlay
renders only visible generation changes. Hidden METAR content retains the last
prepared snapshot and is prepared once when the drawer is next opened.

The accepted runtime shape is one primary target, one bounded request, one
parsed primary observation, and at most one transient lookup. There is no
nearby-airport scan, route-wide collection, arrival prefetch during Departure,
lookup refresh, per-frame cache scan, per-frame hashing, or network work on the
simulator/draw thread.

## Lifecycle

Temporary overlay close, sleep, and xPilot disconnect close the drawer and
preserve accepted bounded cache/history. While disconnected the brain dispatches
no request and publishes no visible METAR update. One in-flight completion may
be retained as an unparsed fact; reconnect reevaluates the target, validates its
generation, and then accepts or rejects it.

Disable cooperatively cancels and joins work while preserving accepted state.
Confirmed new flight, cold-and-dark, callsign change, session reset, plugin
stop, and process exit cancel work and clear METAR state/history through the
existing hard-boundary semantics. No boundary permits a surviving worker.

## Proof boundary

Fifty-three Step 4 scenarios bring the saved suite from 494 to 547. Offline
proof includes focused and full regression, deterministic production-raster
visuals, warm-idle counters, request/backoff and worker-shutdown checks, fresh
Windows Release fixture and normal builds, binary hashes, and normal-binary
fixture/source isolation. Controlled live VATSIM/X-Plane proof and deployment
remain explicitly unperformed and separately gated.
