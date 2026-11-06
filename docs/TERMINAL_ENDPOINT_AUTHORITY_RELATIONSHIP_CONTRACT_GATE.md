# Terminal Endpoint Authority Relationship Contract Gate

**Gate ID:** `TERMINAL-ENDPOINT-AUTHORITY-RELATIONSHIP-02`  
**Status:** Offline implementation complete; live proof and commit authorization pending  
**Prepared:** 2026-10-25  
**Approved:** 2026-10-25  
**Repository baseline:** `6b39f7b2351abc2b4c635b4d13650b62e9563d9f` plus the
uncommitted Center-frequency selection slice already under live test  
**Motivating live flight:** `GTI8274`, `KNGU` to `EDFH`, 2026-10-25  
**Scope:** APP/DEP endpoint authority, relevance, and display eligibility  

## 1. Objective

Replace the ambiguous terminal-controller vote path with one typed,
source-backed relationship decision that answers this exact question:

> Does this live APP/DEP controller directly serve the filed departure or
> arrival airport, or does an authoritative source explicitly assign it as a
> parent/top-down owner of that airport?

The correction must be architectural. It may not be a KNGU/PCT exception, a
new compensating negative vote, a change to one tie threshold, or a second
decision path layered over the existing one.

When this gate closes, every terminal candidate must receive exactly one
Brain-owned relationship result, and only that result may determine whether the
candidate enters the endpoint controller board.

## 2. Governing Law

The governing law remains:

> Workers collect and preserve source facts. The Brain alone interprets those
> facts, assigns the terminal relationship, decides display eligibility, and
> publishes the immutable UI board. The UI renders the decision without
> extending, repairing, or overriding it.

Required flow:

```text
live VATSIM controller record
    + exact source-position/polygon facts
    + endpoint airport coordinate
    + explicit source ownership hierarchy
    + source completeness and freshness
    + AFV operational/radio facts
        -> one Brain terminal relationship decision
        -> one Brain display decision
        -> immutable board
        -> UI rendering only
```

This gate is more specific than the following documents wherever they permit a
geometrically adjacent or merely nearby terminal controller to enter the
endpoint-owned APP/DEP board:

- `docs/ENDPOINT_TERMINAL_POLYGON_OWNERSHIP_CONTRACT_GATE.md`
- `docs/CONTROLLER_OPERATIONAL_ELIGIBILITY_CONTRACT_GATE.md`
- `docs/AUTHORITY_EVIDENCE_CONTRACT.md`

In particular, this gate supersedes the prior requirement that polygon
intersection/adjacency alone may prove “nearby terminal relevance” for the
endpoint board. It also supersedes any prior terminal use of a supported vote
tie to display a candidate whose endpoint relationship is unproven.

This gate does not alter Center/FIR route ownership, the Center-frequency click
feature presently under live test, CTAF/UNICOM policy, or Standby Assist
hierarchy except where those consumers must stop receiving an incorrectly
accepted APP/DEP row.

## 3. Preserved Defect Evidence

During the live `GTI8274` departure from KNGU, XVatsim displayed
`PCT_APP 119.850` as a departure APP/DEP controller.

The preserved Brain receipt reported:

- endpoint: `KNGU`;
- candidate: `PCT_APP 119.850`;
- workflow stage: departure;
- candidate transmitter distance: `59.8 nm` from KNGU;
- direct terminal owner: mismatch;
- published KNGU frequency proof: unavailable;
- vNAS ownership: no applicable ownership match;
- direct endpoint VATSIM relationship: unknown/no direct match;
- candidate polygon: `PCT`;
- airport-containing polygon: a distinct endpoint polygon;
- candidate polygon relation: intersects the endpoint polygon;
- counted terminal result: `+1 exact-source-nearby-endpoint-polygon`;
- counted distance result: `-1 outside-airport-radius`;
- final result: `brain-supported-tie-display`.

The positive fact was misclassified. It proved that the PCT polygon touched or
overlapped another terminal polygon associated with the endpoint package. It
did not prove that the PCT polygon contained KNGU or that PCT owned KNGU.

The defect is therefore not a shortage of raw facts. The defect is that facts
answering different questions are flattened into votes, and a geometric
neighborhood fact is allowed to become terminal ownership/relevance.

## 4. Product Meaning of “Belongs to This Airport”

For the endpoint APP/DEP board, a controller belongs to an airport only when at
least one of these authoritative relationships is proven:

1. **Direct endpoint owner:** the live controller maps to an exact published
   terminal position/polygon, and the airport coordinate lies inside that exact
   controller polygon.
2. **Explicit parent/top-down owner:** an applicable authoritative source
   explicitly associates the live controller position with the airport as a
   parent, top-down, consolidated, bandboxed, or current dynamic owner.

Nothing else is endpoint ownership.

An operational controller can be nearby, receivable, correctly parsed, and
geometrically adjacent while still not belonging to the airport.

## 5. Required Typed Relationship Model

Every eligible APP/DEP candidate evaluated against a departure or arrival
endpoint must receive exactly one of these relationships:

### 5.1 `DIRECT_ENDPOINT`

Required facts:

- the controller is operationally eligible;
- the live callsign/frequency maps to an exact source position;
- that source position maps to one or more exact polygons; and
- the endpoint airport coordinate is inside at least one of those exact
  candidate polygons.

Result: eligible for the endpoint APP/DEP board.

### 5.2 `EXPLICIT_PARENT`

Required facts:

- the controller is operationally eligible;
- the live controller maps to an exact source position; and
- a fresh applicable source explicitly assigns that position as the airport's
  parent, top-down, consolidated, bandboxed, or dynamic owner.

Geometry may corroborate this relationship, but containment, intersection,
overlap, adjacency, polygon size, or transmitter range may not invent it.

Result: eligible for the endpoint APP/DEP board, with parent/top-down
provenance preserved.

An explicit parent is a takeover relationship, not a duplicate direct owner.
When an applicable `DIRECT_ENDPOINT` controller is operational, the parent may
not also enter the endpoint board unless the authoritative ownership source
explicitly publishes simultaneous divided responsibility for those exact
positions. A parent may still appear through a separate, independently proven
Center/FIR route relationship; that does not make it an endpoint APP/DEP row.

### 5.3 `NEARBY_ONLY`

The controller has independently proven local radio service or another exact
nearby-service fact, but neither direct endpoint containment nor explicit
parent ownership is proven.

Result: not eligible for the endpoint-owned APP/DEP board. The fact may remain
in diagnostics. A future separately approved UI may expose a clearly labelled
Nearby ATC view, but this gate does not create one.

### 5.4 `UNRELATED`

Fresh, complete applicable evidence proves that the candidate has no direct
endpoint polygon containment and no explicit parent/top-down relationship.

Result: rejected from the endpoint board.

### 5.5 `UNKNOWN`

Required source facts are unavailable, stale, corrupt, incomplete, or cannot
map the live controller to a source position with sufficient certainty.

Result: not added to the endpoint board. `UNKNOWN` is not equivalent to
offline, invalid, or unrelated; its receipt must preserve the data gap.

Failing closed for creation of a new endpoint row does not authorize removing
an already published row without the normal Brain-owned freshness and lifecycle
rules.

## 6. Source Facts and Their Authority Boundaries

Each fact family answers only its own question.

| Fact family | It may prove | It may not prove |
|---|---|---|
| VATSIM roster | controller identity, facility, primary frequency, online record | endpoint ownership |
| SimAware terminal data | exact position-pattern mapping and polygon geometry | parent/top-down ownership unless explicitly published |
| VATGlasses/vNAS ownership | airport association, owner, parent, consolidation, bandbox, top-down hierarchy | live operational status by itself |
| Endpoint airport coordinate | point-in-polygon containment | controller identity or hierarchy |
| AFV transceivers | frequency service, station location, radio reachability | endpoint ownership |
| Published airport frequencies | frequency/role corroboration | current VATSIM controller ownership or online status |
| VATSpy/static aliases | exact identity aliases | live status, polygon containment, or hierarchy |
| Callsign similarity | diagnostic clue | identity, containment, hierarchy, or display eligibility |
| Polygon intersection/adjacency | geographic relationship between polygons | airport containment, ownership, parentage, or endpoint display eligibility |
| Feed health/completeness | whether a source conclusion is usable | ownership of any individual controller |

No fact may be promoted outside its authority boundary merely because another
fact is missing.

## 7. Decision Algorithm

For each endpoint and each live APP/DEP candidate, the Brain must execute this
ordered decision:

1. Decide operational eligibility from the original VATSIM record and existing
   hard guards.
2. Resolve the exact source position or positions matching the live callsign,
   role, and source frequency constraints.
3. Obtain the exact polygons owned by those matched positions.
4. Test the endpoint airport coordinate directly against those candidate
   polygons.
5. If containment is true, return `DIRECT_ENDPOINT`.
6. Otherwise, query the applicable explicit ownership graph for the exact
   candidate position and airport.
7. If an explicit current parent/top-down/consolidated/bandbox relationship is
   true and no applicable direct endpoint controller is operational, return
   `EXPLICIT_PARENT`. Simultaneous direct/parent display requires an explicit
   source fact assigning divided responsibility.
8. Otherwise, classify independently proven local radio service as
   `NEARBY_ONLY`.
9. If applicable sources are fresh and complete and no qualifying relationship
   exists, return `UNRELATED`.
10. If the required facts are incomplete or unavailable, return `UNKNOWN`.

The algorithm may gather all facts before deciding, but it may not use an
integer majority, positive/negative vote margin, or supported-tie fallback to
replace the typed relationship result.

## 8. Source Precedence and Contradictions

The Brain must preserve source-specific claims rather than blending them into
anonymous votes.

For terminal ownership, the order of authority is:

1. applicable current dynamic ownership assignment;
2. explicit source-published airport ownership/hierarchy;
3. exact candidate-polygon containment of the airport point;
4. exact identity/frequency/radio facts used only to validate the candidate;
5. diagnostic clues that do not decide ownership.

When authoritative sources disagree, the receipt must identify the specific
conflict. The Brain may choose the stronger applicable source under a named
precedence rule; it may not cancel the conflict into a neutral vote and then
allow a weaker fact or tie policy to decide.

Unavailable or incomplete higher-priority data does not invalidate a complete,
independent lower-priority proof. It also does not turn weaker context into
ownership.

## 9. Polygon Semantics

The endpoint package must distinguish these facts without ambiguous booleans:

- candidate polygon contains endpoint airport;
- candidate polygon does not contain endpoint airport;
- containment unknown because geometry is unavailable or invalid;
- candidate polygon intersects/overlaps an endpoint polygon;
- candidate polygon is merely adjacent to an endpoint polygon; and
- explicit source ownership relationship, when supplied.

Intersection, overlap, and adjacency may be calculated for diagnostics or a
future Nearby ATC feature. They must live in a context-only structure or carry
an explicit non-authoritative type. They may not share a field or enum value
with containment or ownership.

The resolver must return geometry and source associations. It may not preselect
the winning controller, label a controller relevant, or suppress a fact before
the Brain evaluates it.

## 10. Terminal Display Invariant

The invariant is:

```text
endpoint APP/DEP row displayed
    implies relationship is DIRECT_ENDPOINT or EXPLICIT_PARENT
```

The inverse is not unconditional: an otherwise valid relationship can still be
blocked by operational, stage, lifecycle, duplicate, or presentation rules.

The following may never independently set `relationAvailable`, `accepted`, or
`displayed` for an endpoint APP/DEP row:

- polygon intersection, overlap, or adjacency;
- AFV reachability or transmitter distance;
- VATSIM roster presence;
- facility type;
- feed health;
- frequency agreement;
- fuzzy callsign similarity;
- a positive/negative tie; or
- a legacy compatibility result.

## 11. Required Legacy-Path Removal

This gate is not complete if the new relationship model is added while the old
terminal vote path remains capable of influencing production state.

Implementation must inventory and eliminate or quarantine every legacy seam,
including at least:

1. the `intersectsEndpointAnchor` fact where it is currently ambiguous with
   endpoint relevance;
2. `TerminalPolygonVote` promotion of a distinct intersecting polygon to
   `+1 exact-source-nearby-endpoint-polygon`;
3. `TerminalOwnerVote` behavior that converts an ownership mismatch into a
   scoring vote or hides it through claim-scoped neutrality;
4. terminal `relationAvailable` construction from heterogeneous positive
   votes;
5. terminal use of `supportedTiesDisplay` and
   `brain-supported-tie-display`;
6. any V1/V2, feature-flag, rollback, or compatibility branch that restores the
   removed terminal authority behavior;
7. any worker-side `relevant`, `accepted`, `owner`, or `display` classification;
8. any UI-side inference that displays an APP/DEP row without the typed Brain
   relationship; and
9. any Standby Assist input that bypasses the final Brain-approved terminal
   board.

The inventory must cover every existing terminal factor and its consumer, not
only the polygon branch that reproduced KNGU/PCT. At minimum, the closeout must
record the final disposition of:

- `EvaluateTerminal` as the current aggregation seam;
- `TerminalVatsimVote`;
- `VatsimRosterVote`;
- `VatspyVote` and any callsign clue it emits;
- `DistanceVote`;
- `VnasVote`;
- `SourceAuthorityVote`;
- `TerminalOwnerVote`;
- `TerminalPolygonVote`;
- `PublishedFrequencyVote`;
- the current route-context vote;
- xPilot correlation facts;
- terminal construction of `relationAvailable`;
- terminal assignment of `supportedTiesDisplay`;
- the generic `DecideBrainControllerCandidate` call when used for terminal
  acceptance; and
- the `terminalRelevanceV2Enabled` rollback/compatibility route, including its
  saved legacy-restoration scenario.

Each item must be assigned to one typed input lane—operational eligibility,
source-position identity, exact containment, explicit ownership hierarchy,
radio service, completeness/freshness, or diagnostic-only—or removed. No item
may remain as an untyped terminal score.

Legacy fields retained temporarily for binary or diagnostic compatibility must:

- be renamed or annotated as diagnostic-only;
- be written from the new typed result rather than independently recomputed;
- be excluded from every production decision input;
- have a removal owner and follow-up condition; and
- have a regression proving they cannot alter the board.

A rollback switch may disable the new terminal relationship feature only by
failing closed to `UNKNOWN`/no new endpoint row. It may not reactivate the
legacy vote algorithm.

## 12. Static Ambiguity Audit

Before the gate can close, an automated repository audit and human review must
prove all of the following:

- no production reference to `exact-source-nearby-endpoint-polygon` remains;
- no production path turns endpoint polygon intersection/adjacency into a
  positive ownership or relevance score;
- no terminal candidate reaches display through `brain-supported-tie-display`;
- no terminal decision uses positive-versus-negative vote totals as its final
  relationship authority;
- no feature flag selects between the new relationship model and the retired
  legacy terminal model;
- every APP/DEP board row has a source-linked typed terminal relationship;
- the publisher and UI consume the Brain result without recomputing it;
- Standby Assist consumes only the final approved board; and
- retained legacy diagnostics are demonstrably non-authoritative.

The implementation closeout must include the exact search expressions, their
results, and a reviewed table containing:

| Legacy symbol/path | Prior authority | Final disposition | Production references remaining | Reason safe |
|---|---|---|---:|---|
| `intersectsEndpointAnchor` | candidate relevance context | remove or replace with context-only typed geometry | TBD | TBD |
| `exact-source-nearby-endpoint-polygon` | positive terminal vote | remove | 0 | no adjacency ownership |
| terminal `supportedTiesDisplay` | tie can display | remove from terminal path | 0 | typed relationship required |
| terminal positive/negative ownership totals | final acceptance | diagnostic-only or remove | 0 authoritative | cannot decide relationship |
| legacy terminal rollback branch | restores old decision | remove | 0 | no dual authority |

`TBD` entries must be resolved with evidence before approval to commit.

## 13. Mandatory Pre-Fix Red Proof

Before production behavior changes, focused tests must reproduce the existing
failure through production seams.

At minimum, the preserved red proof must show:

1. KNGU lies inside its actual endpoint terminal polygon but not inside PCT's
   matched polygon.
2. The PCT polygon intersects or touches an endpoint-package polygon.
3. `PCT_APP 119.850` is operationally present and its nearest matching
   transmitter is approximately `59.8 nm` from KNGU.
4. No applicable source explicitly assigns PCT as KNGU's parent/top-down owner.
5. The uncorrected system creates the positive nearby-polygon vote.
6. The uncorrected terminal tie displays PCT for KNGU.

The test must use the production terminal resolver, Brain evidence input, Brain
decision, and board projection. It may not reimplement the error inside the
test.

If the preserved failure cannot be reproduced, implementation stops and this
gate is amended to match the proven production behavior.

## 14. Required Focused Regression Matrix

The corrected focused suite must independently prove:

1. exact candidate polygon contains airport -> `DIRECT_ENDPOINT`, displayed;
2. source-listed parent while child is offline -> `EXPLICIT_PARENT`, displayed;
3. source-listed consolidated/bandbox owner -> `EXPLICIT_PARENT`, displayed;
4. adjacent polygons with no ownership link -> `UNRELATED`, hidden;
5. overlapping polygons with no airport containment or ownership link ->
   `UNRELATED`, hidden;
6. polygon intersection plus remote transmitter -> `UNRELATED`, hidden;
7. polygon intersection plus local transmitter but no ownership ->
   `NEARBY_ONLY`, hidden from the endpoint-owned board;
8. exact position but airport outside candidate polygon -> not
   `DIRECT_ENDPOINT`;
9. callsign/frequency match without geometry or hierarchy -> `UNKNOWN`, hidden;
10. stale or unavailable terminal geometry -> `UNKNOWN`, not a false unrelated
    conclusion;
11. complete source with no matching ownership -> `UNRELATED`;
12. conflicting sources -> named precedence/conflict receipt, not vote
    cancellation;
13. multiple valid direct split sectors -> each exact containing owner remains
    eligible;
14. direct child and explicit parent simultaneously online -> child remains
    direct; parent follows explicit product precedence and is never inferred
    from size/overlap;
15. PCT/KNGU preserved case -> PCT hidden with a typed reason;
16. Potomac airports actually contained by or explicitly assigned to PCT ->
    remain accepted;
17. SoCal, New York, Providence, and other shared-facility cases -> accepted
    only through containment or explicit ownership;
18. EGLL/THAMES -> THAMES is not endpoint-owned solely because of overlap or a
    nearby transmitter;
19. no terminal supported-tie fixture can create an endpoint row without
    `DIRECT_ENDPOINT` or `EXPLICIT_PARENT`; and
20. Center/FIR, airport-local TWR/GND/DEL, CTAF/UNICOM, and Center-frequency
    selection behavior remain unchanged.

Existing scenarios that encode adjacency as positive ownership must be changed
to assert the new relationship contract. They may not be retained as hidden
legacy expectations merely to keep a historical pass count.

## 15. Required Diagnostics

Every evaluated APP/DEP candidate receipt must expose:

- endpoint airport and workflow side;
- controller callsign, role, original primary frequency, and selected contact
  frequency;
- operational eligibility and reason;
- exact source position match or explicit mapping failure;
- matched candidate polygon IDs and source generations;
- airport-in-candidate-polygon result for each matched polygon;
- polygon intersection/adjacency facts labelled context-only;
- explicit parent/top-down/consolidated/bandbox relationship and provenance;
- source availability, freshness, and completeness per applicable lane;
- AFV transmitter distance and reachability labelled radio-only;
- final typed relationship;
- final endpoint-board eligibility;
- stable candidate/decision key; and
- one final reason that names the decisive proof or missing proof.

Diagnostics may continue to report migration-era vote totals only during the
implementation comparison period. They must be labelled non-authoritative and
must be removed or formally quarantined before gate closure.

## 16. Implementation Sequence

After Product Owner approval, implementation must proceed in this order:

1. preserve the KNGU/PCT live receipt and source-generation evidence;
2. add and run the production-seam red regression;
3. inventory every terminal authority producer, transformer, decision, board
   consumer, compatibility branch, and feature switch;
4. introduce the typed terminal relationship and complete fact packet;
5. make the Brain relationship decision the sole terminal authority;
6. migrate board publication, UI, and Standby Assist consumers;
7. remove the old adjacency vote, terminal tie rescue, and legacy rollback
   authority;
8. run the static ambiguity audit;
9. run the focused green suite;
10. run the complete regression suite and compare exclusions explicitly;
11. build the Release plugin;
12. only after separate authorization, install it for controlled live proof;
13. perform live direct-owner, explicit-parent, and adjacent-unrelated tests;
14. update this gate with evidence and request approval to commit.

No step may be skipped by adding a temporary exception to production logic.

## 17. Prohibited Shortcuts

The implementation may not:

- hardcode KNGU, PCT, ORF, THAMES, EGLL, or another airport/controller pair;
- add a compensating negative vote for PCT;
- change only the supported-tie threshold;
- increase the number of weak votes until the desired candidate wins;
- treat frequency proximity, transmitter distance, callsign similarity, or
  facility type as ownership;
- treat polygon intersection, overlap, adjacency, shared boundary, or polygon
  size as ownership;
- hide the legacy algorithm behind a feature flag or compatibility function;
- leave both the new typed path and old vote path capable of publishing rows;
- move the decision into the resolver, plugin host, publisher, UI, or Standby
  Assist;
- copy another client's displayed board;
- scrape a map UI;
- turn missing data into a clean unrelated/offline fact;
- silently alter Center route authority or airport-local controller logic; or
- commit, package, release, or install a live test build before the required
  gate evidence is complete and separately authorized.

## 18. Performance and Runtime Boundaries

The correction must remain bounded to the filed departure and arrival terminal
packages and the live APP/DEP candidates already being evaluated.

It must not:

- scan the global polygon catalog on every simulator frame;
- add a new flight-loop network request;
- add UI-thread geometry work;
- repeatedly recompute unchanged point-in-polygon results; or
- weaken existing asynchronous source-fetch and cache boundaries.

The airport/polygon containment result must be cacheable by airport coordinate,
source generation, and polygon identity. Explicit ownership results must be
cacheable by endpoint, position identity, source generation, and applicable
dynamic ownership generation.

## 19. Offline and Live Exit Criteria

### 19.1 Offline exit

All of the following are required:

- preserved pre-fix red artifact;
- focused regression matrix green;
- full environment-independent regression suite green;
- every exclusion listed and justified;
- Release build green;
- static ambiguity audit green;
- no authoritative legacy terminal decision path remains;
- no unexpected changes to the uncommitted Center-frequency selection slice;
  and
- reviewed diagnostic receipts for direct, parent, nearby-only, unrelated, and
  unknown cases.

### 19.2 Live exit

After separate live-install authorization:

- KNGU with PCT online does not display PCT without direct or explicit parent
  proof;
- an airport actually inside a PCT-owned polygon still displays the correct PCT
  controller;
- an explicit top-down takeover displays with named ownership provenance;
- source loss produces `UNKNOWN`, not fabricated ownership or clean unrelated;
- no stale terminal row is resurrected by a legacy path;
- diagnostics show one relationship and one display decision per candidate;
- performance remains within the existing release gates; and
- the active live battle-test streak is reset as required for an authority-code
  change.

## 20. Required Closeout Artifacts

The implementation closeout must preserve:

- the original KNGU/PCT live diagnostic excerpt;
- the pre-fix red output;
- the focused green output;
- the full-suite output and exclusion record;
- the Release build output;
- the legacy-path inventory and final disposition table;
- the static search output;
- representative decision receipts for all five relationships;
- the exact built and installed artifact hashes if live proof is authorized;
- live proof logs and screenshots; and
- Product Owner approval to commit.

## 21. Definition of Done

This gate is complete only when:

1. terminal endpoint membership is a typed relationship, not a vote margin;
2. direct ownership requires airport containment by the exact candidate
   polygon;
3. parent/top-down ownership requires an explicit source relationship;
4. nearby radio service and polygon adjacency cannot populate the endpoint-owned
   board;
5. supported ties cannot create terminal ownership;
6. one Brain path exclusively controls terminal relationship and display;
7. legacy vote and rollback authority is removed rather than hidden;
8. retained diagnostics cannot influence production state;
9. KNGU/PCT and all required acceptance/rejection cases pass;
10. the full regression and Release build pass;
11. separately authorized live proof passes; and
12. the Product Owner approves the completed evidence and explicitly authorizes
    commit.

Until every item is proven, the gate remains open.

## 22. Approval Record

**Product Owner decision:** Approved 2026-10-25  
**Implementation authorized:** Yes  
**Live installation authorized:** No  
**Commit authorized:** No

## 23. Offline Implementation Closeout — 2026-10-25

The approved architecture is implemented and the offline portion of this gate
is green. Live installation, live proof, and commit remain outside the current
authorization.

### 23.1 Sole terminal decision

APP/DEP candidates now receive one `TerminalEndpointRelationship`:

- `DIRECT_ENDPOINT` only when an exact candidate source polygon contains the
  endpoint airport;
- `EXPLICIT_PARENT` only from an exact fresh vNAS endpoint ownership record;
- `NEARBY_ONLY` from independently proven local radio service without
  ownership;
- `UNRELATED` from complete contradictory ownership data or an exact
  non-containing candidate polygon; and
- `UNKNOWN` when position, geometry, or ownership cannot be established.

`DecideBrainTerminalControllerCandidate` is the only APP/DEP acceptance
function. It forcibly marks all migration-era evidence votes diagnostic-only,
so its published vote totals are `0/0/0`. Only `DIRECT_ENDPOINT` and
`EXPLICIT_PARENT` can set `accepted=true`.

The relationship and proof are propagated through the completion, immutable
module board, and final display station. Board insertion has a second invariant
guard that rejects an APP/DEP row lacking one of the two display-eligible typed
relationships.

### 23.2 vNAS resolution

The live KNGU/PCT parse did not authorize PCT because its status was
`records=0 ... facilityComplete=1 ownership=0`. The parser was healthy, but it
had produced no exact PCT-to-KNGU ownership record. The former system then
allowed an unrelated SimAware polygon-intersection fact to cast the positive
vote that tied the remote-distance negative vote.

The corrected path does not require vNAS to fabricate a negative. A matched
vNAS ownership record can prove `EXPLICIT_PARENT`; no matched record remains
unknown unless an independent exact polygon proves direct containment or
non-containment. In the preserved live geometry, PCT maps exactly to the PCT
polygon, that polygon does not contain KNGU, and the result is therefore
`UNRELATED`.

Dynamic vNAS incompatibility has named precedence over static polygon
containment. A focused conflict fixture proves that an arrival-incompatible
departure sector remains hidden even when a synthetic containing polygon is
also present.

### 23.3 Removed and quarantined legacy authority

| Prior seam | Final disposition |
|---|---|
| `exact-source-nearby-endpoint-polygon` | removed; zero production matches |
| `intersectsEndpointAnchor` | replaced by `intersectsEndpointAnchorContextOnly`; score-free diagnostic |
| terminal `relationAvailable` aggregation | removed from APP/DEP |
| terminal `supportedTiesDisplay` | removed from APP/DEP |
| terminal vote margin selection | replaced by typed relationship selection rank |
| generic `DecideBrainControllerCandidate` for APP/DEP | replaced by the typed terminal decision |
| `terminalRelevanceV2Enabled` in the Brain terminal path | removed; it cannot restore vote authority |
| `vnasSectorPrecedenceEnabled` in the Brain decision | removed; source hierarchy is always enforced |
| old worker-side relevance implementation | approximately 680 unreachable lines deleted |
| owner-token mismatch scoring | diagnostic-only neutral fact |
| frequency, roster, route context, xPilot, and callsign clues | diagnostic-only; cannot create endpoint membership |

The generic vote decision and its tie reason remain only for non-APP/DEP
domains such as Center/FIR and airport-local roles. `EvaluateTerminal` reaches
that function solely through an explicit `!IsAppDep(role)` branch.

### 23.4 Required behavior proved

Focused production-seam scenarios prove:

- direct polygon containment displays `DIRECT_ENDPOINT`;
- vNAS shared-facility ownership displays `EXPLICIT_PARENT`;
- a local transmitter without ownership is `NEARBY_ONLY` and hidden;
- KNGU/PCT and EGLL/THAMES exact non-containing polygons are `UNRELATED` and
  hidden;
- missing geometry and ownership is `UNKNOWN` and hidden;
- a live direct child suppresses an online explicit parent;
- a stronger explicit operational owner suppresses a lower consolidated
  parent;
- dynamic operation incompatibility overrides contradictory static
  containment with named provenance;
- multiple exact containing split positions remain eligible;
- a legacy feature switch and a supported tie cannot restore endpoint vote
  authority; and
- Center-frequency click selection, non-Center click rejection, single-FSS
  Center selection, airport-local roles, CTAF/UNICOM, and Center/FIR behavior
  remain covered.

### 23.5 Evidence and results

Preserved artifacts:

- `outputs/terminal_endpoint_authority_relationship_02/01_pre_fix_red.txt`;
- `outputs/terminal_endpoint_authority_relationship_02/02_kngu_post_fix_green.txt`;
- `outputs/terminal_endpoint_authority_relationship_02/03_focused_relationship_matrix.txt`;
- `outputs/terminal_endpoint_authority_relationship_02/04_full_suite_summary.txt`;
- `outputs/terminal_endpoint_authority_relationship_02/05_static_ambiguity_audit.txt`; and
- `outputs/terminal_endpoint_authority_relationship_02/06_release_build.txt`.

Final offline results:

- focused relationship and Center-click matrix: green;
- full scenario inventory: 918;
- environment-independent scenarios passed: 917;
- environment-independent failures: 0;
- excluded host-specific absolute-path fixture: 1, unchanged and unrelated;
- Release `XVatsimPlugin` build: green;
- `git diff --check`: clean apart from Git line-ending notices; and
- live plugin installation: not performed.

The offline gate is complete. The overall gate remains open for separately
authorized live installation, direct/parent/adjacent live proof, and explicit
commit approval.
