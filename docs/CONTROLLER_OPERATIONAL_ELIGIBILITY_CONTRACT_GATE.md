# Controller Operational Eligibility Contract Gate

**Gate ID:** `CONTROLLER-OPERATIONAL-ELIGIBILITY-01`
**Status:** Implemented, regression proven, live verified, and approved for
commit by Product Owner on 2026-10-22
**Prepared:** 2026-10-20
**Approved:** 2026-10-20
**Amended and approved:** 2026-10-20 — preserve the narrow `ML`/`MLL`
one-character alias rule and permit an isolated exact-xPilot test fixture path
without changing the live X-Plane installation
**Repository baseline:** `a50c562085e9f79d7ad93670ed795cf2349ed5a3`
**Pre-code rollback branch:** `rollback/controller-operational-eligibility-01-pre-code`
**Active engineering slice:** Correct false-online controller eligibility and
arrival ownership by consuming the existing parsed evidence as one Brain-owned
endpoint evidence packet
**Live verification:** Separately authorized and completed on 2026-10-22

## 1. Objective

Correct the existing XVatsim path that can display a controller for the wrong
airport after the available source facts identify an inactive source or a
different area of service.

The motivating preserved flight evidence is the 2026-10-20 KLAS-to-KCLE flight:

- XVatsim rejected `CLT_APP 199.998` as a guard-frequency candidate;
- XVatsim then accepted and displayed `CLT_APP 120.500` for Cleveland arrival;
- the Brain gave one positive vote because `CLT` is one edit from `CLE`;
- the Brain gave a second positive vote because the same spelling similarity
  also resembled the endpoint VATSpy alias;
- the same receipt recorded that the transmitter was `365.5 nm` from the
  destination and that `CLT_APP` did not match the terminal owner; and
- the resulting `2 positive / 2 negative` tie displayed under
  `brain-supported-tie-display`.

The receipt also proves an evidence-delivery defect. XVatsim had already parsed
the complete VATSIM controller roster, VATSpy airport identity data, vNAS
facility/ownership data, terminal authority, route authority, and AFV
transmitter facts, but Controller Relevance evaluated only a reduced
candidate-by-candidate projection. It did not receive one coherent Cleveland
arrival packet showing which sources were complete, which exact Cleveland
matches existed, and which sources had no endpoint match.

This slice will make operational eligibility and endpoint ownership explicit
Brain decisions from all applicable existing parsed facts. It will not copy
xPilot's Nearby ATC board, create another xPilot bridge, or use real-world FAA
frequencies as VATSIM online/offline authority.

## 2. Governing Bible

The governing law is:

> The Brain is the only decision maker. The Brain asks a worker for specific
> facts. The worker reports those facts without suppressing, filtering,
> classifying, scoring, selecting, changing, replacing, or acting on them. The
> Brain accepts or rejects the facts, decides operational eligibility,
> relevance, frequency, and display state, and publishes the final immutable
> UI snapshot.

Required flow:

```text
Brain requests bounded controller and transceiver facts
    -> VATSIM/AFV workers preserve and report source facts
    -> Brain decides operational eligibility
    -> Brain decides endpoint/route ownership and relevance
    -> Brain publisher assembles the approved immutable board
    -> UI renders without deciding
```

The following documents remain governing unless this gate is more specific for
this defect:

- `docs/BRAIN_OWNED_RUNTIME_CONTRACT.md`
- `docs/RADIO_BOARD_RUNTIME_CONTRACT.md`
- `docs/AUTHORITY_EVIDENCE_CONTRACT.md`
- `docs/V2_EXECUTION_PROTOCOL.md`

The historical type and diagnostic names containing `Worker` do not grant
decision authority. Source modules return facts. The decision functions and
receipts changed by this slice remain in the Brain layer and commit results to
Brain-owned runtime state before the publisher can consume them.

## 3. Relationship to the Displayed-Board Proposal

This gate supersedes `XPILOT-DISPLAYED-BOARD-01` as the implementation direction
for the recorded false-online defect if the Product Owner approves it.

The displayed-board proposal identified a possible parity capability. It did
not establish that exact board copying or a new DLL is necessary to fix the
recorded defect. The saved flight receipt now proves an existing Brain
ownership error independently of xPilot board access.

The existing xPilot 4 raw tracked-controller snapshot remains a neutral
diagnostic/correlation fact. Absence from that range-scoped snapshot is not a
global offline verdict. No xPilot 3 or xPilot 4 bridge, Manager payload, xPilot
binary, SDK contract, installation path, or package is changed by this slice.

An exact displayed-board integration may be reconsidered later only through a
separate Product Owner-approved gate that proves a remaining product need.

## 4. Source Facts and Decision Ownership

### 4.1 VATSIM data worker facts

The VATSIM data worker may mechanically decode and preserve:

- source callsign;
- source primary frequency;
- numeric source facility;
- visual range;
- controller information text;
- source `last_updated` when supplied;
- feed generation, completeness, availability, and staleness; and
- explicit parse, truncation, or corruption facts.

It may not:

- discard a controller because the callsign ends in `_OBS` or `_SUP`;
- mark a record operational because its suffix resembles `_APP`, `_DEP`,
  `_CTR`, `_TWR`, `_GND`, or `_DEL`;
- convert facility `0` into an operational facility from callsign text;
- label a record online/actionable for the UI; or
- choose a display frequency.

### 4.2 AFV/transceiver worker facts

The transceiver worker may mechanically report:

- whether matching transceiver records exist;
- every bounded matching source frequency and coordinate;
- calculated aircraft distance and receivable-range facts requested by the
  Brain;
- source availability, staleness, loss, and completeness; and
- its old candidate projection only as clearly labelled compatibility
  diagnostics while migration proof is being compared.

It may not:

- suppress a source controller because a worker-derived `actionable` flag is
  false;
- decide whether a facility is operational;
- replace an inactive or guard primary frequency with an AFV frequency;
- select the UI frequency;
- decide airport/route ownership; or
- publish a controller candidate or UI row.

### 4.3 Brain decisions

The Brain alone owns:

- whether the source facility represents an operational controller service;
- whether the source primary frequency is operationally eligible;
- whether AFV facts prove reachability without manufacturing active status;
- facility-group interpretation;
- controller frequency selection;
- endpoint, regional, top-down, and route ownership;
- relevance, current/next/arrival relation, and rejection;
- online/offline internal state;
- final row creation, ordering, color, and annotations; and
- every acceptance and rejection receipt.

### 4.4 Brain-owned endpoint evidence packet

For the active departure or arrival airport, the Brain must assemble one
immutable endpoint evidence packet before it decides any terminal controller.
The packet must preserve source availability, staleness, completeness,
generation, exact records, explicit no-match results, and provenance.

The packet must contain, when available:

- the fresh VATSIM controller roster, its declared controller count, retained
  record count, truncation/parse-loss facts, and every bounded raw candidate
  record needed for endpoint comparison;
- exact endpoint callsign/root matches and exact approved endpoint aliases
  derived by the Brain from the VATSIM roster and static identity facts;
- VATSpy airport callsign aliases and boundary identity, clearly labelled as
  static identity/authority data rather than live online status;
- vNAS availability, completeness, direct ownership, consolidated ownership,
  matching records, and explicit no-match state;
- terminal owner tokens and polygons from the existing terminal source;
- route current/next/arrival polygons, route center context, and source-owned
  regional/top-down authority;
- every matching AFV transceiver frequency, coordinate, aircraft distance, and
  endpoint-airport distance requested by the Brain;
- the original VATSIM facility and primary frequency before any contact-channel
  selection; and
- existing range-scoped xPilot correlation as optional supporting diagnostics.

No applicable parsed fact may silently disappear between its source snapshot
and the Brain receipt. A source that is unavailable, stale, incomplete, or
range-scoped must be labelled as such and remain neutral. An explicit no-match
may influence a decision only when the Brain can prove that the relevant source
view was fresh and complete for that question.

FAA/NASR `FRQ.csv` facts are outside the operational evidence packet. VATSIM
may use virtual or pseudo frequencies, so a real-world frequency match or miss
cannot establish, rescue, or reject a VATSIM controller. Existing FAA/NASR
support may remain for isolated diagnostics and legacy test fixtures, but its
score must be neutral in this decision path and this slice does not wire its
live acquisition.

## 5. Brain Operational-Eligibility Rules

The Brain must evaluate the preserved original source facts before controller
relevance scoring.

1. Numeric VATSIM facility `0` is observer/non-operational for controller-row
   purposes. A callsign suffix may not promote it.
2. A callsign explicitly ending in `_OBS` or `_SUP` is not operational for a
   controller row even if another source field is inconsistent. The
   inconsistency remains visible in the receipt.
3. Source primary frequency `199.998` is inactive/guard evidence and is not an
   operational controller frequency.
4. The existing `121.500` guard rejection remains preserved.
5. An empty, malformed, unavailable, or out-of-contract primary frequency is
   an explicit unknown fact, not proof that the controller is offline. It adds
   no negative vote and the Brain may not invent a frequency.
6. AFV transceiver presence proves radio service and reachability. By itself it
   does not convert an explicitly inactive source into an active controller.
7. An AFV frequency may not rescue or replace `199.998`, `121.500`, an
   observer facility, `_OBS`, or `_SUP`. When the source primary is genuinely
   unavailable rather than explicitly inactive, any AFV contact-frequency
   fallback requires separate positive operational-source support and remains
   subject to the supported-tie rule.
8. For an operational source primary frequency, existing Brain-owned
   secondary/extension-channel behavior may remain only when its current
   ownership proof remains valid and the receipt preserves both the original
   primary and selected contact frequency.
9. Unavailable, incomplete, corrupt, or stale source facts do not become a
   clean offline fact or a negative vote. They remain explicitly unknown.

Operational eligibility is necessary but not sufficient for display. An
eligible controller must still pass the existing Brain-owned phase, authority,
route, and presentation decisions.

## 6. Brain Endpoint-Ownership Rules

1. Exact endpoint callsign/root matches remain positive evidence.
2. Exact source-backed airport aliases remain positive evidence.
3. Explicit terminal authority, vNAS ownership, route authority, endpoint-local
   AFV transmitter proof, and approved regional/top-down facts retain their
   existing Brain-owned roles.
4. Edit-distance substitutions and loose shared-prefix similarity remain
   diagnostic clues only. They may not add a positive vote or establish
   `relationAvailable`. Preserve the established Australia alias protection:
   when one VATSpy endpoint alias is exactly the other value with one trailing
   character added or omitted, with no conflicting letters (`ML`/`MLL`), it
   remains one positive endpoint-identity vote. Same-length substitutions such
   as `CLT`/`CLE` do not qualify.
5. The same callsign resemblance may not be counted as two independent
   positive sources merely because it is observed by two parsing routines.
6. Distance remains a reachability/location fact. It does not create
   operational status and does not turn a wrong airport identity into an exact
   identity.
7. The existing fail-safe supported-tie rule remains: after the Brain consumes
   every applicable available fact and deduplicates correlated facts, a genuine
   supported tie displays. This slice must reduce accidental ties by completing
   the ledger; it may not change the tie outcome to hide.
8. `CLT_APP` must be rejected by the completed evidence and operational-
   eligibility decisions, not by overriding the fail-safe tie rule. The
   Cleveland result is expected to stop being a tie once the existing parsed
   facts are classified correctly.
9. `CLT_APP` may not acquire Cleveland ownership from `CLT~CLE`. Its receipt
   must retain the spelling clue, distance fact, terminal-owner mismatch, and
   final Brain rejection reason.
10. VATSIM roster presence proves that a source record exists. It does not by
   itself prove that the controller serves the active endpoint.
11. When the VATSIM roster is fresh and complete, the Brain must record whether
    an exact Cleveland or approved Cleveland-authority candidate exists. A
    direct-endpoint no-match is meaningful endpoint evidence but may not veto a
    separately proven regional, consolidated, or top-down controller.
12. VATSpy exact alias facts may prove identity. VATSpy fuzzy similarity may
    not prove identity, and VATSpy static data may never be described as live
    online/offline status.
13. vNAS absence is neutral when its ownership view is unavailable or
    incomplete. A direct or consolidated match is strong positive evidence. A
    no-match from a complete applicable view remains an explicit Brain fact and
    cannot be silently discarded.
14. FAA/NASR real-world frequencies contribute zero operational score in both
    directions.

The endpoint ledger must expose at least these evidence families when their
facts are applicable:

- `operational-source`: original VATSIM facility, original primary frequency,
  observer/supervisor state, source freshness, and parse completeness;
- `endpoint-identity`: exact VATSIM callsign/root plus exact VATSpy endpoint
  alias facts, with fuzzy resemblance retained only as a clue;
- `operational-ownership`: vNAS direct/consolidated ownership, terminal owner,
  and source-owned regional/top-down authority, preserving which provider made
  each statement;
- `route-relation`: current/next/arrival polygon and compatible center/terminal
  route authority;
- `radio-service`: AFV callsign/frequency match, transmitter existence,
  receivability, and endpoint-airport distance; and
- `optional-correlation`: the existing range-scoped xPilot observation, which
  may confirm a matching observation but whose absence remains neutral.

The families organize the receipt; they do not limit the Brain to one vote per
family. Every applicable independent fact receives one visible factor result.
For example, original-primary eligibility, exact VATSIM endpoint identity,
exact VATSpy identity, vNAS ownership, terminal ownership, route authority,
AFV transmitter location, and AFV reachability are separately reported when
their source views are usable.

Facts derived from the same underlying observation must carry a common
deduplication key and may contribute at most once to the score. The `CLT~CLE`
spelling guess therefore cannot become separate VATSIM and VATSpy positive
votes. Independent facts from different source contracts remain separately
counted even when they agree.

Every decision receipt must report all three views of the decision:

- raw facts considered: positive, negative, and neutral;
- independent scored factors after deduplication: positive, negative, and
  neutral; and
- final positive and negative decision score used by the Brain.

After operational hard failures are handled, the relevance rule remains:

```text
positive score > negative score  -> display
negative score > positive score  -> hide
positive score = negative score and positive score > 0 -> display supported tie
positive score = 0 -> no supported evidence; hide
```

Missing, stale, incomplete, or out-of-scope facts remain neutral and may not be
turned into votes merely to force a majority. If the final supported score is
tied after every usable fact is consumed, the controller displays. An explicit
operational hard failure such as facility `0`, `_OBS`, `_SUP`, `199.998`, or
`121.500` is reported before endpoint scoring and is not disguised as a tie.

## 7. Incorrect Behavior Replaced

The implementation must delete or retire these behaviors from the live path:

- `ParseControllerCallsignFlags` as a source-worker admission and
  `actionable` decision;
- source-worker deletion of `_OBS` and `_SUP` records before the Brain sees
  them;
- live facility promotion from numeric observer/unknown status solely through
  an operational-looking callsign suffix;
- live primary-frequency rescue in which an explicitly guard/inactive
  controller primary is replaced by a normal AFV frequency;
- positive endpoint ownership from edit-distance substitutions or loose
  shared-prefix similarity, except the approved one-trailing-character
  `ML`/`MLL`-style alias rule;
- duplicate positive votes derived from the same fuzzy callsign assumption;
- treating VATSIM record presence or a valid APP/DEP suffix as proof that the
  controller serves the active endpoint;
- evaluating a reduced radio candidate without the complete endpoint evidence
  packet and source-health facts;
- silently reducing vNAS, VATSpy, route, terminal, or VATSIM no-match state to
  an unexplained neutral value;
- allowing FAA/NASR real-world frequency agreement to establish VATSIM
  endpoint service; and
- assigning `station.online` directly from a worker-produced `actionable`
  value.

Compatibility projections may exist only during this slice for deterministic
old-versus-new comparison. They must be labelled compatibility-only, may not
feed the publisher, and must be removed when no approved proof depends on them.

## 8. Protected Existing Behavior

The slice must preserve:

- exact airport callsign and exact approved alias handling;
- SoCal/Los Angeles-to-Ontario and other existing regional terminal cases;
- Potomac shared-TRACON behavior;
- vNAS direct and consolidated ownership precedence;
- top-down controller behavior;
- Center route/polygon selection;
- Australian extension and duplicated-ATIS behavior from accepted operational
  primaries;
- valid secondary/extension contact channels after Brain acceptance;
- guard, ATIS, CTAF/UNICOM, COM tuning, Standby Assist, color, and phase rules;
- settled-idle caching and wake behavior;
- xPilot 3 and xPilot 4 compatibility; and
- all unrelated PDC, METAR, ATIS, Manager, update, UI, and packaging behavior.

No protection applies to a test that encodes worker-owned decisions or the
recorded false-online behavior. If an existing test expects such behavior, its
expectation must be replaced with a Brain-owned fact/decision expectation and
called out in the evidence receipt.

## 9. Exact Implementation File Manifest

Approval of this gate authorizes changes only to the following files.

### Brain fact and decision contracts

- `brain/include/XVatsim/brain/BrainTypes.h`
- `brain/include/XVatsim/brain/RadioReachableSnapshot.h`
- `brain/include/XVatsim/brain/BrainControllerDecision.h`
- `brain/include/XVatsim/brain/BrainOwnedWorkerTypes.h`
- `brain/src/BrainRadioRangeWorker.cpp`
- `brain/src/RadioReachableSnapshot.cpp`
- `brain/src/BrainControllerDecision.cpp`
- `brain/src/BrainControllerEvidencePipeline.cpp`

### Fact workers

- `modules/vatsim_data_feed/src/VatsimDataFeedClient.cpp`
- `modules/vatsim_data_feed/include/XVatsim/modules/vatsim_data_feed/VatsimDataFeedClient.h`
- `modules/controller_feed/src/ControllerFeedClient.cpp`
- `modules/transceiver_resolver/src/TransceiverResolver.cpp`

### Plugin shell wiring

- `plugin/src/XVatsimPlugin.cpp`

The plugin file may change only to pass preserved facts into the existing
Brain request, include the new facts in stable identity where required, and
record Brain receipts. It may not contain eligibility, facility, frequency,
ownership, scoring, or display policy.

### Focused proof tooling

- `tools/regression_harness/CMakeLists.txt`
- `tools/regression_harness/src/main.cpp`
- `tools/regression_harness/src/Step6PdcContractProbe.cpp` (test-only fixture
  path override; default production-path proof remains unchanged)
- new: `tools/regression_harness/src/ControllerOperationalEligibilityProbe.h`
- new: `tools/regression_harness/src/ControllerOperationalEligibilityProbe.cpp`

### Contract and evidence

- `docs/CONTROLLER_OPERATIONAL_ELIGIBILITY_CONTRACT_GATE.md`
- `docs/DIAGNOSTIC_LOG_VOLUME_GATE_V2.md` for the combined automated and live
  evidence receipt

No other file may change. A newly discovered need to touch another file is a
stop condition requiring an amended gate and Product Owner approval.

The existing unrelated working-tree documentation changes are not part of this
slice and must not be edited, staged, reverted, or included in the slice commit
unless separately approved.

## 10. Explicit Non-Goals

This slice will not:

- create or change an xPilot 3 or xPilot 4 DLL/bridge;
- read, copy, or reconstruct the xPilot displayed board;
- promote xPilot 4 `GetControllers()` to displayed-board authority;
- modify the Manager, installer, manifest, packaging, or release;
- patch, hook, inject into, scrape, proxy, or replace xPilot;
- add a network call, thread, timer, polling loop, or flight-loop scan;
- create a global callsign alias database;
- globally reject every tied decision;
- impose a universal five-nautical-mile terminal-coverage rule;
- wire, download, or use FAA/NASR `FRQ.csv` as operational VATSIM evidence;
- change controller ordering, color, text, UI layout, or pilot controls;
- deploy to X-Plane or connect to VATSIM for proof; or
- begin any later displayed-board or Manager slice.

## 11. Mandatory Pre-Fix Red Proof

After approval, the first implementation action is to add the focused probe and
run it against the uncorrected production seams before changing behavior.

The preserved red run must prove all four failures:

1. A fresh, complete VATSIM roster containing `CLT_APP` but no exact or
   approved Cleveland terminal identity still allows `CLT_APP 120.500` to be
   accepted for `KCLE` because `CLT~CLE` produces duplicate positive spelling
   votes and the tie displays.
2. A numeric facility-`0` record with an `_APP`-looking callsign can reach an
   operational APP/DEP candidate through suffix fallback.
3. A source primary of `199.998` plus an AFV station on `120.500` can produce a
   live `120.500` candidate instead of preserving the inactive primary result.
4. The current Cleveland receipt does not contain one auditable endpoint packet
   carrying VATSIM roster completeness, exact endpoint-match count, VATSpy
   identity result, vNAS completeness/no-match state, terminal ownership,
   route authority, and AFV endpoint distance together.

The probe must use production decoder, transceiver, Brain radio-range, and
Brain relevance entry points. It may not reimplement the decisions in the
test. The output and nonzero exit status must be saved in `pre_fix_red.txt`
before behavior is corrected.

If any claimed failure cannot be reproduced at the production seam, coding
stops. The gate must be amended to match the proven defect before implementation
continues.

## 12. Focused Green Proof

The corrected focused probe must pass and report each case independently:

- raw facility `0`, `_OBS`, and `_SUP` records reach the Brain as facts rather
  than disappearing in the worker;
- the Brain rejects those records as operational controller rows with explicit
  receipts;
- raw primary `199.998` remains visible in the receipt and cannot be rescued by
  AFV `120.500`;
- `121.500` is rejected explicitly, while empty, malformed, stale, and
  incomplete primary facts remain distinct unknown facts and do not become
  false offline votes;
- `CLT_APP` is rejected for `KCLE`; the `CLT~CLE` clue remains diagnostic and
  contributes no positive vote;
- a fresh, complete VATSIM roster with no exact/approved Cleveland terminal
  identity produces an explicit `no-direct-endpoint-controller` fact;
- the same VATSIM absence does not veto a separately proven regional,
  consolidated, or top-down controller;
- VATSpy is labelled static identity evidence and cannot claim online status;
- vNAS direct, consolidated, unavailable, incomplete, and complete-no-match
  states remain distinguishable in the receipt;
- FAA/NASR frequency agreement and disagreement both contribute zero
  operational score, including a valid VATSIM pseudo-frequency fixture;
- a valid exact `CLE_APP` operational-primary fixture remains eligible for
  Cleveland relevance evaluation;
- xPilot raw-snapshot absence remains neutral rather than becoming a global
  veto;
- the selected controller-row frequency retains its Brain-approved source and
  original-primary provenance;
- exact aliases, explicit regional ownership, top-down coverage, and route
  authority remain valid;
- the existing supported-tie regression still displays and reports
  `brain-supported-tie-display` after consuming its complete evidence packet;
- every input candidate has exactly one Brain acceptance or rejection receipt;
- every applicable endpoint evidence family has exactly one consumed or
  explicitly neutral packet entry with provenance; and
- the Cleveland and supported-tie receipts report raw fact totals,
  deduplicated factor totals, and the exact final positive/negative score. The
  gate does not predeclare a manufactured Cleveland vote total before the
  production probe proves which source views are usable.

The focused run must also include these existing protections without changing
their intended legitimate result:

- `brain_controller_relevance_v2_tie_fails_safe_to_display.scn`
- `brain_controller_relevance_v2_kbwi_iad_m_vnas_and_endpoint_transmitters.scn`
- `brain_controller_relevance_v2_vnas_unavailable_is_neutral.scn`
- `brain_controller_relevance_v2_faa_pseudo_frequency_miss_is_neutral.scn`
- `resolver_duplicated_atis_derived_rejects_wrong_facility.scn`
- `--australia-brain-evidence`
- `--xpilot4-bridge-contract`

## 13. Static Bible Proof

The evidence receipt must include automated searches and a reviewed ownership
table proving:

- the VATSIM worker does not drop `_OBS`/`_SUP` controller facts;
- the VATSIM worker does not assign operational/actionable UI truth;
- a complete VATSIM roster and its completeness/truncation facts reach the
  Brain endpoint packet without the plugin classifying them;
- the transceiver worker does not use facility, suffix, primary guard status,
  or `actionable` to make the live eligibility decision;
- the transceiver worker does not select the final UI frequency;
- the plugin contains no new eligibility, ownership, score, fallback, or
  display decision;
- only Brain files decide operational eligibility and endpoint ownership;
- fuzzy callsign substitutions cannot produce a positive vote; the approved
  one-trailing-character `ML`/`MLL`-style alias rule remains positive;
- FAA/NASR frequency facts cannot produce a positive or negative operational
  vote;
- VATSpy evidence is labelled static identity/authority evidence rather than
  live status;
- parsed VATSIM, VATSpy, vNAS, terminal, route, and AFV endpoint facts are each
  consumed or explicitly neutralized by the Brain with a reason;
- the compatibility candidate vector cannot feed the live publisher;
- xPilot raw absence remains neutral;
- bridge, Manager, installer, packaging, and release files are unchanged; and
- the final changed-file list exactly matches this gate.

A class or function being located in the `brain` directory is not sufficient
proof. The report must show that source workers return facts, the Brain creates
the decision receipt, Brain-owned runtime stores it, and the publisher consumes
only accepted Brain completions.

## 14. Regression, Build, and Performance Proof

### 14.1 Full regression and build

- Configure a fresh Release build from the approved source.
- Build `XVatsimRegressionHarness` and `XVatsimPlugin` in Release.
- Run all `888` currently present `.scn` files in sorted order twice from the
  fresh build; both passes must be `888 / 888` unless this gate is amended.
- Run every focused command listed in Section 12.
- Run the focused operational-eligibility probe at least 100 consecutive times
  with identical results.
- Review the complete diff for unrelated changes.
- Hash the Release harness and plugin candidates with SHA-256.

One passing scenario, a successful compile, or a screenshot is not completion.

### 14.2 Predeclared performance limits

The Release focused probe must demonstrate:

- no new network request, background thread, queue, timer, or callback;
- a 512-controller synthetic source/AFV fact batch is decoded and evaluated by
  the production seams in no more than `25 ms` wall time after warm-up;
- `10,000` repeated Brain operational-eligibility evaluations complete in no
  more than `250 ms` wall time after warm-up;
- an unchanged radio-board identity produces a Brain cache hit with zero
  repeated transceiver fetch and zero repeated relevance decision work;
- queue depth remains `0` because this slice adds no queue;
- process working-set growth after warm-up and `10,000` identical evaluation
  cycles is no more than `4 MiB`;
- no per-flight-loop or per-frame decision log is added; and
- diagnostics remain change-driven: at most one eligibility receipt per
  changed candidate plus the existing bounded summaries.

Failure of a predeclared limit is a failed gate. Limits may not be relaxed
after seeing a failure without Product Owner review and an amended gate.

## 15. Evidence Receipt

The final evidence report must provide:

- starting and ending commits;
- exact changed-file manifest;
- the preserved pre-fix red output and exit code;
- focused green commands, named cases, and exact totals;
- before-and-after raw fact totals, deduplicated factor totals, and decision
  score for `CLT_APP`, plus the same totals for the preserved supported-tie
  case;
- both full regression commands, `888 / 888` results, and elapsed times;
- Release configuration and build commands/results;
- static Bible checks and the worker-versus-Brain ownership table;
- performance commands and all measured values;
- representative receipts for `CLT_APP`, facility `0`, `199.998`, exact
  `CLE_APP`, a VATSIM pseudo-frequency, a regional terminal case, and a
  top-down case;
- SHA-256 hashes for the harness and plugin;
- proof that the normal binary contains no fixture endpoint or test payload;
- proof that no simulator deployment or live VATSIM test occurred;
- known limitations;
- rollback instructions and a tested source-level rollback result; and
- the narrow commit hash and message.

If any proof is missing or fails, the status remains `not proven`, the slice is
not committed as complete, and no later slice begins.

## 16. Rollback Boundary

This slice has no data migration, settings migration, bridge payload, Manager
payload, or package change.

Rollback is one narrow source commit plus removal of its evidence directory.
The rollback proof must rebuild the prior source and rerun the preserved red
probe to demonstrate that the rollback boundary is understood. The old false
behavior must not be deployed merely to demonstrate rollback.

## 17. Live Verification Gate

Offline proof is mandatory before any live verification. This gate does not
authorize copying the candidate plugin into X-Plane, launching a controlled
network flight, or connecting to VATSIM for testing.

After the complete offline receipt is reviewed, the Product Owner may authorize
a separate controlled live test. That later test should compare XVatsim,
xPilot, the contemporaneous VATSIM feed, AFV source facts, and an independent
map at recorded timestamps. A screenshot alone is supporting evidence, not the
proof chain.

## 18. Stop Conditions

Stop and return to the Product Owner if:

- a required file is outside the approved manifest;
- the defect cannot be reproduced at the production seam;
- a worker must suppress, classify, score, select, replace, or publish to make
  the fix work;
- the plugin shell would need product-decision logic;
- legitimate regional, top-down, shared-TRACON, Center, Australia, or
  secondary-channel behavior cannot be preserved;
- the only proposed fix is a global tie rejection or universal distance rule;
- source incompleteness would be converted into a clean offline fact;
- an xPilot bridge, Manager, installer, package, or live-network change becomes
  necessary;
- any focused, full, build, static, lifecycle, memory, or performance proof
  fails; or
- the result can only be described as working from compilation or appearance.

## 19. Approval Boundary

Approval authorizes only the exact implementation and offline proof described
in this gate. It does not authorize live deployment, live-network verification,
Manager work, bridge work, packaging, release, or another engineering slice.

The correct completion claim is:

```text
Source facts preserved without worker decisions
    -> Brain operational-eligibility receipt
    -> Brain endpoint/route relevance receipt
    -> Brain-approved immutable display snapshot
    -> focused and full offline proof
    -> separately authorized controlled live verification
```

Until that entire authorized portion is proven, the status is `not proven`.

## 20. Implementation Closeout — 2026-10-22

The Product Owner separately authorized controlled live verification. The
Cleveland false-online case, Australia alias behavior, EGLL endpoint ownership,
and live DAL41 Heathrow controller changes were verified without adding or
reading a replacement xPilot board bridge. Controller and transceiver sources
remain fact providers; the Brain alone owns eligibility, votes, continuity,
and display publication.

The complete applicable regression set passed `911 / 911`. The documented
machine-locked xPilot 3.0.2 absolute-fingerprint scenario was excluded because
that binary is not installed; its relative-path companion passed. The Product
Owner confirmed the final live behavior and authorized the combined source
commit on 2026-10-22.
