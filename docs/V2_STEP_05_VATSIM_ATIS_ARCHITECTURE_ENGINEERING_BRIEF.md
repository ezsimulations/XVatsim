# STEP 5 VATSIM ATIS ARCHITECTURE REVIEW — COMPLETE; IMPLEMENTATION NOT AUTHORIZED

## 1. Review disposition

The approved architecture is feasible without a second network request,
scheduler, worker, semantic owner, presentation snapshot, renderer path, or
publication queue. No implementation blocker or unsettled Product Owner choice
was found.

The required production flow is deliberately linear:

```text
existing VatsimDataFeedClient fetch thread and 15-second cadence
    -> mechanically bounded facts from the dedicated root atis[] array
    -> plugin transports the immutable feed view and accepted workflow facts
    -> Brain parses ATIS syntax and makes every semantic decision
    -> existing Brain-owned accessory presentation snapshot
    -> existing generic accessory preparation worker
    -> existing overlay commit, raster, draw, and publication coordinator
    -> existing publication queue
    -> Brain accepts terminal roles and acknowledges an exact visible revision
```

This document is an architecture review only. It does not authorize source or
scenario changes, builds, deployment, application startup, VATSIM access, or
controlled-live testing.

## 2. Authoritative source and schema findings

The official VATSIM Data API documentation confirms that the existing live
network document is regenerated every 15 seconds and contains a dedicated root
`atis` array. The record schema supplies `callsign`, `frequency`, `atis_code`,
`text_atis`, `last_updated`, and `logon_time` as required by this design. It also
contains fields such as CID and name that Step 5 does not need and must not
retain.

The separately documented `/afv-atis-data.json` endpoint is not required and is
prohibited for Step 5. The existing request remains
`https://data.vatsim.net/v3/vatsim-data.json`.

Saved regression evidence contains 172 distinct ATIS-shaped callsigns and
confirms all three approved forms:

- combined examples: `EDDB_ATIS`, `LPPT_ATIS`, `KAMA_ATIS`;
- departure examples: `EDDF_D_ATIS`, `KATL_D_ATIS`, `LEPA_D_ATIS`; and
- arrival examples: `EDDF_A_ATIS`, `KATL_A_ATIS`, `LTAI_A_ATIS`.

No additional callsign form is proposed. A future implementation must reject
loose prefixes, substrings, or unreviewed aliases in the Brain with one
deterministic reason.

## 3. Current-source data flow and exact gap

```text
VatsimDataFeedClient::Poll()
    -> FetchSnapshot() on the existing fetch thread
    -> ParseFeed()
         -> controllers[] -> ControllerSnapshot
         -> pilots[]      -> PilotPlanEntry
         -> root atis[]   -> currently ignored
    -> immutable VatsimDataFeedSnapshot
         -> ControllerFeedClient::BuildSnapshot()
         -> NetworkPlanLink::Poll()
         -> XVatsimPlugin flight-loop transport
              -> accepted Brain workflow/flight context
              -> Brain-owned accessory semantics
              -> ProjectBrainOwnedAccessoryPresentation()
                   -> generic AccessoryPreparationWorker
                   -> UpdateAccessoryPresentation()
                   -> OverlayWindow draw/publication coordinator
                   -> AccessoryPublicationFactQueue
                   -> ConsumeBrainOwnedAccessoryPublicationFact()
                   -> production diagnostic serializer
```

Current source has no ATIS product state machine. The neutral `ATIS` ORB is
projected by `ProjectBrainOwnedAccessoryRailSemantics()`. When ATIS is selected,
`ProjectBrainOwnedAccessoryPresentation()` supplies the shared ATIS history,
which is empty in the normal fixture-off payload, and the placeholder text
`VATSIM ATIS data is not enabled in Step 3.`

The current VATSIM parser does carry `controllers[].text_atis` and a controller
`atis` flag. Those fields support established controller filtering and
duplicated-controller-coverage authority evidence. They do not read the root
`atis[]` array, establish product ATIS availability, select an airport or
service, manage unread/history state, or control the ATIS ORB/drawer. They must
remain isolated from Step 5 and must not be used as a fallback source.

The existing production seams already provide the hard integration properties:

- one Brain-owned accessory state and visible-invalidation boundary;
- one immutable accessory command snapshot;
- per-drawer semantic revisions;
- one generic mechanical preparation worker;
- exact snapshot-pointer and command/lifecycle binding;
- one overlay commit and renderer path;
- one visible-publication coordinator and lossless terminal queue; and
- independent command-terminal and visible-attempt-terminal acceptance.

The Step 5 gap is therefore only: decode bounded dedicated ATIS facts, add
Brain-owned ATIS semantics, project those semantics into the existing snapshot,
and carry an opaque exact-revision acknowledgement through the existing first-
visible-frame fact.

## 4. Bounded immutable source-fact contract

The source DTO must be mechanical and immutable. It must not contain a derived
ICAO, service role, applicability decision, selected candidate, availability,
revision identity, unread state, or drawer instruction.

Proposed transport shape (names may be refined without changing ownership):

```cpp
enum class VatsimAtisMechanicalIssue : std::uint32_t {
    None              = 0,
    MissingCallsign   = 1u << 0,
    FieldType         = 1u << 1,
    InvalidUtf8OrNul  = 1u << 2,
    FieldLimit        = 1u << 3,
    LineLimit         = 1u << 4,
    BodyLimit         = 1u << 5,
};

struct VatsimAtisRawFact {
    std::string sourceCallsign;       // trimmed/sanitized source spelling
    std::string normalizedCallsign;   // mechanical ASCII uppercase only
    std::string frequency;            // bounded source text
    std::string informationCode;      // bounded source text
    std::vector<std::string> textLines; // order preserved
    std::string lastUpdated;          // bounded raw timestamp text
    std::string logonTime;            // bounded raw timestamp text
    VatsimAtisMechanicalIssue issue;
    bool mechanicallyUsable;
};

struct VatsimAtisRawSnapshot {
    bool rootPresent;
    bool rootArray;
    bool mechanicallyComplete;
    bool recordLimitExceeded;
    bool aggregateLimitExceeded;
    std::uint64_t sourceGeneration;
    bool hasCache;
    bool stale;
    bool fetchInProgress;
    std::vector<VatsimAtisRawFact> records;
    std::uint32_t rejectedRecordCount;
};
```

The DTO should live in the existing shared fact/type layer used by
`VatsimDataFeedSnapshot`, preserving the current dependency direction from the
feed module to Brain fact types. The feed snapshot owns the record vector; the
plugin passes a const view during the same flight-loop cycle.

### 4.1 Hard limits

| Item | Limit | Mechanical result when exceeded |
|---|---:|---|
| Root ATIS records | 256 | Mark the ATIS component over-limit/incomplete; retain no order-dependent prefix as authoritative |
| Callsign | 32 UTF-8 bytes | Retain a bounded diagnostic shell when possible; record unusable |
| Frequency | 16 UTF-8 bytes | Record unusable |
| Information code | 8 UTF-8 bytes | Record unusable |
| Timestamp field | 48 UTF-8 bytes each | Record unusable; timestamp parsing remains a Brain decision |
| `text_atis` lines | 64 | Record unusable |
| One text line | 512 UTF-8 bytes | Record unusable |
| Joined/preserved body | 8,192 UTF-8 bytes | Record unusable |
| Retained ATIS field bytes per snapshot | 1,048,576 | Mark component over-limit/incomplete and expose no authoritative candidates |
| Conservative total ATIS component memory | 2 MiB | Fail the ATIS component mechanically; the rest of the feed may remain usable |

The 2 MiB ceiling includes bounded record/vector structures and retained UTF-8
storage. Implementation must account before allocation or append and must not
reserve from untrusted source sizes.

### 4.2 Mechanical normalization rule

- Require the root member to be an array for a mechanically complete ATIS
  component.
- Decode only JSON primitive/array types required by the DTO.
- Trim outer whitespace; reject NUL; replace disallowed control characters with
  a single space; preserve text line order; and retain valid UTF-8.
- Mechanical uppercase of the callsign is permitted, but no token boundary,
  ICAO, or service role may be inferred outside the Brain.
- Missing optional frequency, code, and timestamps remain empty.
- A present wrong-typed, invalid, or over-limit field marks that record
  mechanically unusable. Do not publish a truncated record as usable ATIS.
- A syntactically odd but mechanically bounded callsign is carried to the Brain
  unchanged except for the documented mechanical normalization.
- Root record-count or aggregate-memory overflow invalidates the whole ATIS
  component instead of selecting an order-dependent prefix.
- Per-record rejection is counted. If no valid applicable candidate exists and
  the component is partial, the Brain selects `SourceUnknown`, never a false
  `ConfirmedUnavailable`.
- Name, CID, real name, rating, server, and other unused personal/source fields
  are not retained.

The pure decoder used by `FetchSnapshot()` should be exposed as a production-
owned document-decoding seam so offline proof can pass bounded JSON fixtures
through the exact parser without making a network request.

## 5. Non-negotiable ownership matrix

Every responsibility has exactly one owner. “Brain” below means functions and
state in the Brain library, not the plugin or a helper in another module.

| Responsibility | Sole owner | Explicitly not allowed elsewhere |
|---|---|---|
| Network endpoint, cadence, backoff, fetch thread | Existing `VatsimDataFeedClient` | Brain, plugin, ATIS-specific timer/worker |
| JSON decode, field-type checks, UTF/control sanitization, bounds | Existing VATSIM feed module | Brain semantic selection; plugin |
| Raw immutable ATIS fact storage | Existing `VatsimDataFeedSnapshot` | A second cache or ATIS repository |
| Transport of feed view, workflow stage, flight context, text-entry facts | Plugin | Plugin interpretation or fallback |
| Callsign grammar and normalization into ICAO/service role | Brain | VATSIM module, plugin, overlay |
| Record usability for product ATIS | Brain | Parser-side semantic filtering |
| Departure/destination target | Brain accepted workflow and flight context | A second airborne detector or route parser |
| Directional/combined applicability | Brain | Feed/module suffix flags |
| Deterministic duplicate choice | Brain | Container order, plugin, overlay |
| Availability and source uncertainty | Brain | Feed module/controller presence |
| Meaningful revision identity and digest | Brain | Source generation or timestamp alone |
| Primary ATIS, unread/read, history, lookup ownership/deadlines | Brain | Plugin globals, overlay state, worker |
| Drawer selection response | Brain through existing accessory selection | Mouse callback or renderer |
| Semantic presentation invalidation and scroll reset | Brain through existing invalidation API | Overlay heuristics or texture dirtiness |
| Immutable ORB/drawer payload | Brain accessory snapshot | Preparation worker or plugin reconstruction |
| Text measurement/wrapping | Existing generic accessory worker | Brain or ATIS-specific worker |
| Commit, raster, upload, draw | Existing overlay path | Brain or new renderer |
| Visible-attempt creation and terminal fact queueing | Existing overlay coordinator/queue | ATIS state machine or test injection |
| Terminal role validation and exact unread acknowledgement | Brain | Queue dequeue, click, hidden commit, texture upload |
| Mechanical diagnostic serialization | Existing production serializer | Literal test ledger or assumed success counter |

### Rule One conclusion

The VATSIM module may return a mechanically normalized callsign, but only the
Brain may decide that `KDFW_D_ATIS` means airport `KDFW`, service `Departure`,
that it applies to the current flight, that it wins selection, or that it is
available/new/read. No second selector, fallback owner, or racing semantic path
is required or permitted.

## 6. Brain-owned ATIS state

One `BrainOwnedAtisRuntimeState` should be embedded in the existing
`BrainOwnedRuntimeState`. It is domain state, not a second accessory state or
presentation generation.

Required state:

- ATIS lifecycle epoch and last consumed source generation;
- `SourceUnknown`, `ConfirmedUnavailable`, or `Available` source result;
- accepted automatic target ICAO and desired service role;
- exact selected primary record/revision identity;
- exact unread revision identity (optional, one current primary revision);
- accepted primary current-section data;
- lookup ICAO/generation, pending start/deadline, result deadline, selection
  generation, and ownership-valid flag;
- zero, one, or two lookup display sections;
- diagnostic counters/reasons; and
- no domain-specific worker, queue, presentation command counter, or renderer
  state.

ATIS history remains in `accessory.histories[Atis]`, using the existing 32-entry,
8 KiB body, and 64 KiB aggregate enforcement. The ATIS state calls the existing
history acceptor only for a new semantic revision.

ATIS changes call the existing visible-invalidation operations:

- `RecordBrainOwnedAccessoryDrawerContentMutation(...Atis)` for authoritative
  ATIS drawer changes;
- existing selection mutation for drawer ownership;
- existing lifecycle mutation for lifecycle changes; and
- the existing rail semantic comparison to create one rail revision only when
  exact projected ATIS ORB fields change.

No separate ATIS presentation generation is allowed.

## 7. Brain state machine and ownership transitions

### 7.1 Stable states

```text
SourceUnknown
    fresh complete feed + applicable usable candidate -> Available(Read/Unread)
    fresh complete feed + no applicable candidate     -> ConfirmedUnavailable

ConfirmedUnavailable
    applicable usable candidate -> Available(Unread)
    feed stale/failed/incomplete -> SourceUnknown

Available(Read or Unread)
    identical fresh candidate    -> no-op
    meaningful new candidate     -> Available(Unread)
    fresh complete absence       -> ConfirmedUnavailable
    feed stale/failed/incomplete -> SourceUnknown (history retained)
```

Target changes caused by the accepted workflow re-run deterministic selection
once. Departure uses the accepted departure ICAO and desired `Departure` role;
Enroute/Arrival use the accepted destination ICAO and desired `Arrival` role.
No other flight-phase input is introduced.

### 7.2 Lookup ownership

```text
None
  valid lookup text -> Pending (ATIS drawer selected, one reset)

Pending
  fresh cache evaluated -> Spotlight or ConfirmedUnavailable result
  20 seconds elapsed    -> SourceUnknown failure result
  newer drawer/lifecycle -> ownership lost; no later drawer change

Spotlight (8 seconds) / Unavailable result (4 seconds)
  deadline -> restore current automatic primary once
  newer drawer/lifecycle -> ownership lost; expiry cannot steal selection
```

Manual lookup never changes the primary ICAO, accepted flight context, automatic
service role, source cadence, or network schedule.

### 7.3 Exact unread acknowledgement

The snapshot carries an opaque Brain-issued ATIS read-candidate identity only
when the ATIS drawer content actually includes that exact primary revision. The
overlay copies the identity without interpretation into the production first-
visible-frame terminal fact. The Brain clears unread only when all are true:

1. the fact and visible-attempt role are accepted in the current lifecycle;
2. disposition is a successful first qualifying frame;
3. the rendered drawer is ATIS;
4. the fact's opaque identity equals the current unread revision; and
5. the command was not superseded, failed, hidden-only, cancelled, stale, or
   rejected.

Clearing unread changes the exact ATIS ORB from amber `NEW A` to cyan `INFO A`
and causes one ordinary Brain presentation invalidation. It does not mutate
history or the ATIS content revision.

## 8. Deterministic callsign parsing and candidate selection

The Brain accepts only these exact normalized grammars, with a four-character
ASCII alphanumeric ICAO:

| Form | Brain role |
|---|---|
| `KDFW_D_ATIS` | Departure |
| `KDFW_A_ATIS` | Arrival |
| `KDFW_ATIS` | Combined |

Examples:

| Context | Candidates | Result and reason |
|---|---|---|
| Departure KDFW | `KDFW_D_ATIS`, `KDFW_ATIS` | Departure wins semantic priority |
| Departure KDFW | `KDFW_A_ATIS`, `KDFW_ATIS` | Combined wins; arrival-only is inapplicable |
| Enroute to KSAN | `KSAN_A_ATIS`, `KSAN_ATIS` | Arrival wins semantic priority |
| Enroute to KSAN | `KSAN_D_ATIS` only | No applicable primary; fresh complete feed means confirmed unavailable |
| Manual KATL | combined plus split | Show combined only |
| Manual KATL | departure and arrival, no combined | Show both, Departure section then Arrival section |
| Duplicate departure records | same role | Newest valid `last_updated`; then stable lexical/content key |
| Duplicate with equal/invalid time | same role | Stable key `CALLSIGN|FREQUENCY|CODE|TEXT_DIGEST`; never container order |
| `KDFW_X_ATIS`, `KDFW_DEP_ATIS`, `XX_ATIS` | any | Brain rejects with exact grammar reason |

The semantic revision key is:

```text
ICAO | SERVICE_ROLE | NORMALIZED_CALLSIGN | NORMALIZED_FREQUENCY |
NORMALIZED_INFORMATION_CODE | SHA256(SANITIZED_TEXT_LINES_WITH_BOUNDARIES)
```

`last_updated`, `logon_time`, and feed generation may order candidates but do
not define a new revision.

## 9. Availability, revision, and unread truth tables

### 9.1 Availability/freshness

| Feed condition | Applicable usable record | Brain availability | ORB | Drawer current section |
|---|---:|---|---|---|
| No successful feed | n/a | SourceUnknown | Gray `ATIS` | Source unknown; retained history may remain |
| Fetch running within freshness grace | retained current source still fresh | Preserve current evaluation | Existing exact state | Existing current/history |
| Stale, failed, over-limit, missing/wrong root | any/unknown | SourceUnknown | Gray `ATIS` | Not current; retained history marked source unknown |
| Fresh complete | No | ConfirmedUnavailable | Gray `ATIS` | Confirmed no applicable ATIS |
| Fresh complete | Yes | Available | Cyan or amber | Current primary plus history |
| Fresh partial, valid applicable candidate present | Yes | Available | Cyan or amber | Valid selected record; partial diagnostic retained |
| Fresh partial, no valid applicable candidate | Unknown | SourceUnknown | Gray `ATIS` | Source incomplete, not controller offline |

### 9.2 Meaningful revision

| Change | New revision | History | Unread | Presentation |
|---|---:|---:|---:|---:|
| Feed generation only | No | 0 | unchanged | 0 |
| `last_updated` only | No | 0 | unchanged | 0 unless source availability itself changed |
| Identical record after fresh refresh | No | 0 | unchanged | 0 |
| Information code | Yes | 1 | set | At most 1 semantic command |
| Text with same code | Yes | 1 | set | At most 1 semantic command |
| Frequency | Yes | 1 | set | At most 1 semantic command |
| Callsign/service role | Yes | 1 | set | At most 1 semantic command |
| Split/combined selected content changes | Yes | 1 per new displayed revision | set for new primary | At most 1 logical command |
| Background nonprimary record changes | Maybe history if accepted by policy | primary unchanged | unchanged | 0 while ATIS view/ORB unchanged |

### 9.3 Unread acknowledgement

| Event | Clears exact unread revision? |
|---|---:|
| ATIS click captured/Brain selection | No |
| Command projected | No |
| Hidden command commit | No |
| Preparation/raster/upload | No |
| Failed, cancelled, stale, or superseded publication | No |
| Visible frame of older revision | No |
| Accepted first frame with another drawer | No |
| Accepted first frame containing exact current unread ATIS revision | Yes, once |
| Later unchanged frames | No additional action |

## 10. Lifecycle truth table

| Boundary | Current/lookup/unread | History | Source on return | Presentation behavior |
|---|---|---|---|---|
| Reset XVatsim | Clear | Clear through existing accessory boundary | Unknown | Existing destructive reset |
| Confirmed new flight | Clear | Clear | Unknown | New accepted context chooses target |
| Callsign identity change | Clear before new identity | Clear | Unknown | Existing callsign boundary |
| Confirmed cold/dark | Clear | Clear | Unknown | Existing cold/dark reset |
| Recover Current Flight | Preserve accepted state | Preserve | Unknown until fresh generation | Existing recovery semantics unchanged |
| Temporary xPilot loss/reconnect | Cancel lookup ownership; preserve accepted state | Preserve | Unknown until fresh generation | Drawer closes through existing boundary; no stale reopen |
| Overlay sleep/wake | Preserve | Preserve | Preserve current feed evaluation | One visibility publication if required; no semantic replay |
| Plugin Admin disable/re-enable | Cancel lookup ownership; preserve accepted state | Preserve | Unknown because shared feed client restarts | One retained coherent projection after revalidation; no duplicate history |
| Orderly lifecycle cancellation | Terminally account before epoch advance | Preserve or clear per boundary | Boundary-specific | Existing lossless fact rules |
| Late old-epoch fact | Reject stale | No mutation | Unchanged | Cannot acknowledge unread or steal drawer |

## 11. ORB and drawer state table

| Semantic state | ORB text/tone | Drawer title/state | Scroll behavior |
|---|---|---|---|
| Idle / confirmed unavailable / source unknown | `ATIS`, gray | Distinguish confirmed unavailable from source unknown | Preserve unless the Brain owns a new result transition |
| Available, read, code A | top `KDFW`, bottom `INFO A`, cyan | `ATIS — KDFW — DEPARTURE/ARRIVAL/COMBINED` | Preserve background user scroll |
| Available, read, no code | top `KDFW`, bottom `ATIS`, cyan | Same, code omitted | Preserve |
| Available, unread, code A | top `KDFW`, bottom `NEW A`, amber | Exact current revision at top | Reset once for newly visible revision |
| Available, unread, no code | top `KDFW`, bottom `NEW`, amber | Exact current revision at top | Reset once |
| Lookup pending | Automatic primary ORB unchanged | `FETCHING ATIS — KABQ` | Reset once |
| Combined lookup result | Automatic primary ORB unchanged | One combined section | Reset once; restore after 8 s |
| Split lookup result | Automatic primary ORB unchanged | Departure then Arrival sections | Reset once; restore after 8 s |
| Confirmed lookup unavailable | Automatic primary ORB unchanged | Explicit fresh-feed unavailable result | Reset once; restore after 4 s |
| Lookup source unknown | Automatic primary ORB unchanged | Source unknown, not controller offline | Reset once; restore after 4 s |
| Retained history under source unknown | Gray `ATIS` | History visible and marked not current | Pilot scroll preserved |

The shared ORB tone enum gains generic `Cyan` and `Amber` values. The renderer's
existing data-driven two-line ORB layout is generalized from METAR-only to any
snapshot-provided two-line accessory ORB. There is no ATIS-specific renderer
selector: the overlay draws the exact Brain fields.

## 12. Manual lookup timeline

1. The plugin menu opens the existing bounded text-entry UI in a new
   `AtisAirportLookup` mode.
2. The overlay returns raw entered text. The plugin transports one immutable
   text-entry fact.
3. The Brain validates one four-character ICAO, records a lookup generation,
   selects ATIS through the existing accessory selection authority, records
   ownership by selection/lifecycle generation, emits one pending semantic
   command, and advances scroll reset once.
4. On the next existing ATIS Brain service edge, the Brain evaluates the most
   recent shared feed snapshot. No network request is made.
5. If no fresh snapshot is available, pending may wait for a normal feed
   generation, bounded by 20 seconds.
6. A successful current result becomes an eight-second spotlight: combined if
   present, otherwise clearly ordered Departure and Arrival sections.
7. A fresh complete absence becomes a four-second confirmed-unavailable result.
   A stale/failed/incomplete source becomes a four-second source-unknown result.
8. Expiry restores the current automatic primary once and advances scroll reset
   once if ATIS still owns presentation.
9. A newer ATIS/PDC/METAR selection, lifecycle change, reset, or identity change
   invalidates ownership. Later evaluation or expiry may update Brain history
   truthfully but cannot reopen ATIS or reset another drawer.

## 13. Exact future implementation scope

The future implementation/offline-proof gate should authorize only the
following existing files unless red proof demonstrates a narrower documented
need:

| File | Required seam |
|---|---|
| `brain/include/XVatsim/brain/BrainTypes.h` | Mechanical raw ATIS fact/view types shared with the existing feed snapshot |
| `modules/vatsim_data_feed/include/XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h` | Extend immutable snapshot; expose the production document decoder for offline proof |
| `modules/vatsim_data_feed/src/VatsimDataFeedClient.cpp` | Decode/bound root `atis[]` mechanically on the existing fetch thread |
| `brain/include/XVatsim/brain/BrainOwnedRuntime.h` | Brain ATIS state, inputs, decisions, diagnostics, opaque read-correlation fields |
| `brain/src/BrainOwnedRuntime.cpp` | Existing accessory projection, rail semantics, lifecycle boundaries, and publication-consumption hook |
| `brain/CMakeLists.txt` | Register one Brain ATIS implementation translation unit |
| `plugin/src/XVatsimPlugin.cpp` | Transport feed view/stage/context, ATIS text-entry menu fact, diagnostics; no semantic logic |
| `modules/overlay/include/XVatsim/modules/overlay/OverlayAccessoryCore.h` | Production-owned complete visible-terminal builder seam if required to prevent test fact injection |
| `modules/overlay/src/OverlayAccessoryCore.cpp` | Same builder implementation used by overlay and tests |
| `modules/overlay/src/OverlayWindow.cpp` | Generic cyan/amber palette, generic two-line ORB draw, opaque revision correlation copied to first-frame fact |
| `tools/regression_harness/CMakeLists.txt` | Link the existing feed decoder for offline production-seam JSON fixtures |
| `tools/regression_harness/src/main.cpp` | Production-seam probes and measured assertions |
| top-level `CMakeLists.txt` | Register a dedicated current-source Step 5 visual-proof tool, if the new tool is adopted |

Proposed new files:

- `brain/src/BrainAtisRuntime.cpp` — the only ATIS semantic implementation;
- `tools/step5_atis_visual_proof/CMakeLists.txt` and
  `tools/step5_atis_visual_proof/src/main.cpp` — deterministic proof only;
- uniquely named Step 5 scenarios, engineering brief, proof summary, receipt,
  and deterministic visual outputs authorized by the future gate.

`OverlayWindow.h`, the preparation worker contract, publication queue ordering,
METAR worker/scheduler, controller authority modules, Step 3 visual proof, and
Step 4 visual proof should not require product changes. The future gate must
stop rather than silently expand scope if compilation or red proof contradicts
that inventory.

## 14. Legacy-symbol and duplicate-owner plan

There is no legacy Step 5 product owner to remove. The following established
symbols are deliberately retained but quarantined from the product ATIS path:

- `ControllerSnapshot::textAtis` remains controller-information/authority
  evidence only.
- `ControllerSnapshot::atis` remains controller facility filtering only.
- `ParseControllerCallsignFlags()` remains part of the established controller
  feed unless a separate gate authorizes its relocation; it must not inspect
  root `atis[]` facts or produce Step 5 roles.
- duplicated-controller-coverage ATIS proof logic in `RouteSectorResolver`
  remains unrelated to product ATIS content.

Future proof must show that a populated `controllers[].text_atis` with an empty
root `atis[]` cannot make the product ORB available or populate the product
drawer. It must also search for and reject any new root-ATIS selector outside
`BrainAtisRuntime.cpp`/the declared Brain projection functions.

The placeholder ATIS empty-state projection is replaced in place by Brain ATIS
semantics. No compatibility selector, parallel history, plugin-side cache, or
fallback to controller data is permitted.

## 15. Production-seam proof architecture

The end-to-end offline seam must execute, in order:

```text
bounded JSON fixture
 -> production VATSIM document decoder
 -> immutable VatsimDataFeedSnapshot ATIS view
 -> plugin-equivalent const transport input (no semantic conversion)
 -> actual Brain ATIS cycle and decision
 -> actual click queue when a drawer action is involved
 -> actual Brain accessory selection and immutable projection
 -> actual generic AccessoryPreparationWorker
 -> actual preparation binding and presenter commit
 -> actual drawer/rail render plan
 -> actual visible-publication coordinator
 -> production-owned full terminal-fact builder
 -> actual AccessoryPublicationFactQueue
 -> actual Brain publication consumer
 -> actual diagnostic serializer
```

The test observes returned decisions and counters. It must not precompute or
inject the expected ICAO, role, selected candidate, availability, revision,
unread result, drawer payload, publication fact, or Brain acceptance decision.

## 16. Required red/green proof matrix for the future gate

| # | Production-seam behavior | Unchanged-source red | Required green |
|---:|---|---|---|
| 1 | Dedicated root `atis[]` decode | Root array is ignored; product remains empty | Exact bounded records appear in shared snapshot; no second request |
| 2 | Controller text isolation | Demonstrate controller text is a separate legacy input | Populated `controllers[].text_atis` alone cannot establish product ATIS |
| 3 | Ownership audit | No raw dedicated DTO/Brain owner exists | Module emits only mechanical facts; only Brain parses/decides |
| 4 | Ambiguous/malformed callsigns | No dedicated record path | Facts reach Brain; exact deterministic accept/reject reasons |
| 5 | Combined/departure/arrival/duplicates | No product selection | Role priority, timestamp, lexical/content tie-break exact |
| 6 | Departure-to-Enroute target | ATIS ORB remains placeholder | Existing workflow edge selects departure then destination without another detector |
| 7 | No ATIS, empty text, malformed, oversize, stale, failed, recovery | Placeholder cannot distinguish source truth | Exact Available/ConfirmedUnavailable/SourceUnknown table |
| 8 | Same and later identical feed generation | No product state | Zero history/unread/snapshot/raster/upload/publication work |
| 9 | Code/text/frequency/role/station changes | No product state | Each meaningful change creates one exact revision; timestamp-only does not |
| 10 | Unread acknowledgement | No unread state | Only exact accepted ATIS first frame clears; every negative case preserves |
| 11 | New ATIS while drawer open | No product update | One command, one top reset, one frame, then zero recurring work |
| 12 | History and bounds | Generic fixture history only | Newest first, semantic dedup, 32/8 KiB/64 KiB, session boundaries |
| 13 | Lookup pending/combined/split/unavailable/expiry/lost ownership | No ATIS lookup mode | Cached-feed-only lookup, 20/8/4-second bounds, no drawer theft |
| 14 | IFR/VFR/no-plan contexts | No ATIS primary | Accepted IFR/VFR target rules; no-plan automatic idle/manual available |
| 15 | Reset/Recover/callsign/disconnect/sleep/Admin/lifecycle | No ATIS domain state | Exact lifecycle truth table; stale fact cannot acknowledge or mutate |
| 16 | Terminal accounting and capacity | No ATIS revision correlation | Exact command/attempt roles, retained immutable retry, zero duplicate/loss/liveness |
| 17 | 1,000 clicks and 100,000 unchanged cycles | ATIS empty-only baseline | Exact input accounting; zero recurring semantic/raster/upload/publication/worker work |
| 18 | Current-source visual proof | Neutral/empty only | Every read/unread/idle/drawer/split/history/stale/scale/clipping state deterministic twice |
| 19 | Baseline, builds, loopback, isolation | Accepted Step 3/4 baseline | Step 3/4/5 focused, full regression, fixture-off, isolation, cancellation, WinHTTP all pass |

Additional independent cases required inside those groups:

- information code absent/present and same code with changed text;
- valid newest timestamp, equal timestamp, invalid timestamp, and container-order
  reversal;
- departure-only record rejected for arrival primary and vice versa;
- one logical feed mutation affecting both drawer and rail creates at most one
  presentation command;
- source recovery with identical retained content updates availability once but
  does not duplicate history or unread;
- exact old-revision visible fact after a newer revision does not clear unread;
- combined terminal role accounting and hidden command followed by later visible
  attempt accounting;
- lookup for the current primary, a different airport, combined-only, split-
  only, one-sided split, and malformed ICAO;
- scroll preservation for background history changes and one top reset for a
  currently visible new revision;
- plugin suspend/resume with retained history but SourceUnknown until the next
  fresh feed generation; and
- normal fixture-off payload contains no proof fixture, alternate endpoint, or
  test transport.

The future gate must specify scenario lineage and measurable timing limits before
implementation. Existing simulator-thread, click-to-terminal, issue-to-commit,
visible-first-frame, cancellation, queue, and warm-idle limits remain binding.

## 17. Risks and controls

| Risk | Required control |
|---|---|
| Controller `text_atis` accidentally becomes fallback content | Explicit isolation test and source audit |
| Feed module parses role for convenience | Ownership test: no derived ICAO/role fields in DTO |
| Root overflow makes selection depend on order | Whole ATIS component becomes incomplete; no prefix selection |
| Repeated 15-second snapshots create work | Feed-generation/revision edge checks and 100,000-cycle proof |
| Workflow transition races feed evaluation | Brain consumes accepted stage/context and deterministically reevaluates once |
| Visible frame clears wrong unread revision | Opaque exact revision correlation and lifecycle/command validation |
| New revision while open resets repeatedly | Existing scroll-reset generation consumed once |
| Manual lookup steals automatic primary | Separate Brain lookup ownership; ORB primary fields unchanged |
| Resume advertises retained data as current before feed freshness | SourceUnknown on reconnect/resume until fresh generation |
| Tests manufacture the desired decision/fact | Production decoder-to-serializer seam and zero direct expected-fact injection |

## 18. Blockers and Product Owner decisions

No blocker was found. The approved gate already settles all pilot-visible
choices needed for implementation: source, cadence, directional priority,
workflow target transition, lookup behavior, timers, revision identity, unread
acknowledgement, history bounds, uncertainty wording, ORB text/tone, lifecycle,
and presentation ownership.

If implementation evidence reveals a real accepted callsign outside the three
audited forms, a need for a second endpoint/worker/scheduler, or a need to change
Reset/Recover semantics, work must stop for Director/Product Owner disposition.
Those outcomes must not be inferred or patched under the future gate.

## 19. Recommendation

Prepare a separate narrowly scoped **Step 5 VATSIM ATIS Implementation and
Offline Proof Contract Gate** using this file inventory, ownership matrix,
bounded DTO, production-seam red cases, and proof matrix. This architecture
review grants no implementation, controlled-live, deployment, staging, commit,
or release authority.
