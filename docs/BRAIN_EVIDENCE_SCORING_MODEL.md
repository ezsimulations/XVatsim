# Brain Evidence Scoring Model

Status: proposed decision model.

Purpose: define how brain-owned XVatsim decisions should move away from scattered if/else vetoes and toward explicit weighted evidence plus rare hard-block rules.

This document is a model and migration target. It does not describe a runtime behavior change by itself.

## Core Principle

Modules report facts and evidence.

The brain scores evidence.

Final verdicts come from:

- weighted evidence
- explicit hard-block rules
- a decision ledger that explains every final outcome

No module should accept, reject, hide, display, suppress, veto, or decide relevance. A module can report that a source fact is missing, stale, malformed, distant, unmatched, filtered by parser hygiene, or geometrically related. The brain decides what those facts mean.

Inside the brain, decision code should not become a maze of scattered vetoes. The brain should collect evidence items, score them, identify any hard-block evidence, then emit a final decision record.

## XVatsim Fail-Soft Product Policy

XVatsim is a clutter-reduction filter for xPilot. It is not an ATC authority system, and it does not decide whether a controller is operationally valid for the pilot. Its job is to reduce irrelevant frequency clutter while preserving frequencies that may belong to the pilot.

This makes XVatsim's failure risk asymmetric:

- false positive: an irrelevant frequency is displayed
- false negative: a relevant frequency is hidden

A false positive is bad because it adds clutter. A false negative is worse because it can hide a frequency the pilot may need.

Therefore, when evidence is uncertain, ambiguous, incomplete, stale, or conflicting, the brain should prefer showing, showing with lower confidence, or defer-labeling a candidate over hiding it outright. Hiding requires stronger evidence than displaying. Hard blocks require explicit policy and focused tests.

Fail-soft behavior means:

- uncertain evidence should not hard-hide a candidate
- weak negative evidence should not override strong positive evidence
- fallback inference should not hide when high-confidence accepted evidence exists
- incomplete evidence should usually produce displayed-with-warning, deferred, or lower-priority output instead of silent suppression
- missing data should be represented as evidence, not treated as automatic rejection

This policy does not mean every candidate must always be displayed. It means the burden of proof is higher for hiding than for displaying when the system cannot prove irrelevance.

## Display Verdict Tiers

Display-domain decisions should use these tiers, from most permissive to most restrictive:

- `display`: strong positive evidence supports showing the candidate
- `display-low-confidence`: evidence supports showing, but the decision depends on weak, incomplete, or fallback evidence
- `defer-by-stage`: candidate may be valid, but current workflow stage or UI policy delays it with an explicit reason
- `hide-strong-evidence`: hide only when strong negative evidence outweighs positive evidence or a named policy says the row is not relevant now
- `hard-block`: hide only for impossible-to-render or safety-critical conditions, such as empty frequency or explicit offline/non-displayable state

The ledger should make these tiers understandable even when the current runtime still uses older display names. A display decision record should expose score, confidence, hard-block state, and the reason why hiding was safer than displaying.

## Default Conflict Rules

Default conflict handling must follow the fail-soft product policy:

- high-confidence positive evidence beats fallback negative evidence
- multiple low-confidence negatives do not automatically beat one high-confidence positive
- medium-confidence negatives may lower priority or confidence before hiding
- one fallback hide must not override a direct accepted display relation fact
- hard blocks must be rare, explicit, and test-covered
- missing data should not equal rejection
- when the evidence is close, the safer fail-soft outcome is display-low-confidence or defer-by-stage, not silent hide

## Evidence Item Shape

Each evidence item should carry:

```cpp
struct BrainEvidenceItem {
    std::string evidenceId;
    std::string source;
    std::string type;
    BrainEvidenceConfidence confidence;
    double weight;
    BrainEvidenceDirection direction;
    std::string reason;
    bool advisory;
    bool decisive;
    bool safetyCritical;
    bool hardBlocking;
    BrainEvidenceOrigin origin;
};
```

Suggested enums:

```cpp
enum class BrainEvidenceConfidence {
    High,
    Medium,
    Low,
    UnknownFallback,
};

enum class BrainEvidenceDirection {
    SupportsAccept,
    SupportsReject,
    SupportsDisplay,
    SupportsHide,
    SupportsDefer,
};

enum class BrainEvidenceOrigin {
    RawSourceEvidence,
    ComputedGeometry,
    RouteContext,
    ControllerRelevance,
    DisplayPolicy,
    FallbackInference,
};
```

## Confidence Levels

### High Confidence

Use for direct source facts or brain-owned accepted facts with clear identity and no ambiguity.

Examples:

- controller relevance accepted a candidate with a stable completion key
- direct AFV station evidence within computed range
- direct authority evidence matched route scope and proof source
- accepted display relation fact from controller relevance
- explicit user or safety constraint

### Medium Confidence

Use for computed evidence with strong but indirect support.

Examples:

- computed geometry intersects a route corridor
- transceiver station is near an authority polygon within tolerance
- airport frequency role matches a controller facility
- terminal authority ownership matches source-backed tokens
- multiple medium source facts agree

### Low Confidence

Use for weak, incomplete, stale, or auxiliary support.

Examples:

- partial callsign/root match
- weak route family match
- advisory distance hint
- missing source ownership where other evidence still exists
- non-authoritative catalog or fallback text clue

### Unknown / Fallback

Use for inference made because stronger evidence is missing.

Examples:

- fallback center relation inferred from polygon key
- fallback current relation because route metadata is unavailable
- fallback UNICOM when CTAF source is unresolved
- last-proven display reuse during pending verification

Fallback evidence must always be lower confidence than direct accepted relation facts.

## Suggested Normalized Weights

These values are a starting point. Individual domains may tune thresholds, but must keep relative ordering stable.

Scores must be bounded and normalized. The brain should weigh evidence statistically, not let one ordinary evidence item become an absolute pass/fail authority. A high-confidence item should be strong, but it is not mathematically unbeatable unless it is separately marked as an explicit hard block.

Because false negatives are higher severity for XVatsim, thresholds should bias toward display/defer when evidence is uncertain. A weak negative score should reduce confidence or priority before it hides. A hide/reject verdict needs either strong accumulated negative evidence or an explicit hard block.

| Evidence class | Direction | Suggested weight |
| --- | --- | ---: |
| High confidence positive | accept/display | +0.80 to +1.00 |
| Medium confidence positive | accept/display | +0.50 to +0.79 |
| Low confidence positive | accept/display | +0.20 to +0.49 |
| Unknown/fallback positive | accept/display/defer | +0.05 to +0.19 |
| High confidence negative | reject/hide/defer | -0.80 to -1.00 |
| Medium confidence negative | reject/hide/defer | -0.50 to -0.79 |
| Low confidence negative | reject/hide/defer | -0.20 to -0.49 |
| Unknown/fallback negative | reject/hide/defer | -0.05 to -0.19 |
| Hard-block evidence | reject/hide/defer | separate boolean/category, not a larger score |

Positive and negative totals should be tracked separately, not only as one net number. Scores should support later aggregation, averaging, confidence blending, thresholding, or Bayesian-style comparison without changing their meaning. A useful summary is:

```text
positiveScore=0.90 negativeScore=0.15 net=0.75 confidence=high hardBlock=false
```

Hard-block evidence must never be represented as a giant negative number. It should be explicit, rare, separately auditable, and allowed only for impossible-to-render or explicitly safety-critical conditions.

Recommended display interpretation:

```text
positiveScore about 0.80-1.00, negativeScore low: display
positiveScore fallback/low and negativeScore low: display-low-confidence or defer
positiveScore medium, negativeScore medium: defer or display-low-confidence unless explicit policy says hide
negativeScore about 0.80-1.00, positiveScore weak: hide-strong-evidence
hardBlock=true: hard-block
```

## Scoring Calibration Guardrails

The normalized scale is a contract, not a formatting preference.

Forbidden patterns:

- No 100-vs-1 style scoring.
- No giant numeric weights that make ordinary evidence mathematically unbeatable.
- No single ordinary evidence item may act as an absolute pass, reject, display, or hide authority.
- No hard block may be represented by a very large positive or negative score.
- No module-style pass/fail vocabulary should be reintroduced inside the brain without a decision ledger.

Required calibration rules:

- Prefer `0.00` to `1.00` scores for new brain decision ledgers.
- High confidence evidence should normally sit around `0.80` to `1.00`.
- Medium confidence evidence should normally sit around `0.50` to `0.79`.
- Low confidence evidence should normally sit around `0.20` to `0.49`.
- Fallback or unknown evidence should normally sit around `0.05` to `0.19`.
- Hard blocks are separate explicit categories with named reasons.

Hard blocks are allowed only for impossible-to-render or explicitly safety-critical conditions, such as no usable frequency, an empty display frequency, or a row explicitly marked offline/non-displayable where the renderer cannot present useful data.

## Aggregation Principles

Scores should be suitable for later aggregation, averaging, thresholding, confidence blending, or statistical-style comparison. The model should make multiple independent evidence items matter without letting correlated facts masquerade as independent proof.

Aggregation rules:

- Track positive and negative score totals separately.
- Keep evidence item identity and origin visible in the ledger.
- Multiple independent evidence items may strengthen a conclusion.
- Correlated evidence should be grouped, capped, or marked so it is not double-counted as independent.
- One low-confidence negative cannot override strong positive evidence.
- Multiple weak negatives may lower confidence or priority, but should not automatically hard-hide.
- Missing data is evidence of uncertainty, not evidence of rejection.
- Fallback inference must remain lower confidence than direct accepted relation facts.
- If the scores are close, prefer fail-soft output: display with warning, lower priority display, defer, or needs-more-evidence.

Conceptual fail-soft thresholds:

- `display`: positive evidence clearly outweighs negative evidence.
- `display-with-warning` or `needs-more-evidence`: evidence is mixed, weak, incomplete, or fallback-heavy.
- `defer`: stage or policy says not now, but relevance may still be valid.
- `hide`: negative evidence is strong, better supported than positive evidence, and the ledger explains why hiding is safer than showing.
- `hard-block`: explicit impossible-to-render or safety-critical condition, separate from score size.

## Display Intent Calibration Examples

These examples describe current BrainDisplayIntent diagnostics. They are not live score-driven behavior yet.

### HNL Relation Fact Vs Fallback Hidden

```text
subject=HNL_02_CTR@126.500
positive=0.90 high-confidence accepted CURRENT_POLYGON relation fact
negative=0.00 no hard display-negative evidence after relation fact applies
hardBlock=false
winner=positive
recommendation=keep-current-display
```

The accepted relation fact is strong but still bounded. Fallback hidden inference must not defeat it unless future evidence supplies stronger negative proof or an explicit display policy reason.

### Fallback-Hidden Accepted Center

```text
subject=HNL_02_CTR@126.500
positive=0.00
negative=0.15 fallback hidden inference
hardBlock=false
winner=negative
recommendation=prefer-display-with-warning
```

Fallback hidden is low-confidence negative evidence. Because the row was accepted by relevance and is renderable, fail-soft preview should expose that current hide decisions are candidates for warning or more evidence.

### Duplicate Suppression

```text
positive=0.00
negative=0.65 duplicate role/callsign/frequency key
hardBlock=false
winner=negative
recommendation=keep-current-hide
```

Duplicate suppression is a display policy decision, not proof that the controller is irrelevant. If future unique relation or polygon evidence exists for the duplicate, the recommendation can move toward lower-priority display.

### Stage-Deferred Row

```text
positive=0.55 accepted arrival-prep relation
negative=0.70 current workflow stage does not render arrival-prep yet
hardBlock=false
winner=negative
recommendation=prefer-stage-defer
```

Stage defer means "not now", not "irrelevant". The ledger must preserve the accepted subject and the stage reason.

### Filtered Or Unknown Relation

```text
positive=0.00
negative=0.65 filtered or unknown relation fact
hardBlock=false
winner=negative
recommendation=prefer-display-with-warning or needs-more-evidence
```

Filtered, hidden, or unknown relation states after relevance acceptance must not become silent suppression. The fail-soft recommendation should explain whether the row needs warning, lower priority, or more evidence.

### Non-Displayable Offline Or Empty-Frequency Row

```text
positive=0.00
negative=0.90 explicit non-displayable condition
hardBlock=true
winner=blocked
recommendation=hard-block-hide
```

The hard block comes from the explicit non-displayable condition, not from the numeric score being large.

## Future Implementation Guardrails

Any new scoring domain must expose:

- positive score
- negative score
- confidence
- hardBlock
- winner
- recommendation or final fail-soft interpretation
- final reason
- evidence item identities and origins

Any future live score-driven behavior must first run in preview/parity mode. The preview must prove that the score-driven decision set matches existing behavior before it can become authority, unless the requested product change explicitly intends a behavior difference.

Any change from hide to display, display to hide, reject to accept, or accept to reject must have focused tests that prove:

- the final result
- positive and negative score summaries
- hardBlock state
- winning and losing evidence
- fail-soft recommendation
- why false-negative risk was acceptable before hiding

## Verdicts

Final brain verdicts should be produced from the evidence set and hard-block rules:

- `accepted`
- `rejected`
- `displayed`
- `hidden`
- `deferred`
- `needs-more-evidence`
- `fallback-used`

Each verdict must have:

- final reason
- winning evidence
- losing evidence
- score summary
- hard-block reason if present
- fallback-used flag

## Verdict Rules

### Accepted

Use when positive accept evidence is strong enough and no explicit hard block applies.

Suggested default:

- positive accept score is high, such as >= 0.80 after aggregation
- positive score exceeds negative reject score by a meaningful margin, such as >= 0.20
- no hard-block reject evidence

### Rejected

Use when negative reject evidence clearly outweighs accept evidence, or when a hard-block reject applies.

Suggested default:

- negative reject score is high, such as >= 0.80 after aggregation
- negative score exceeds positive accept score by a meaningful margin, such as >= 0.20
- or explicit hard-block reject evidence exists

### Displayed

Use when a relevance-accepted subject also has enough display evidence and no explicit display hard block applies.

Suggested default:

- accepted by relevance, or equivalent high-confidence display source
- positive display score exceeds hide/defer score
- final display policy permits the current stage

### Hidden

Use when display evidence says the subject should not be visible now.

Hidden is allowed only if the decision ledger explains it. A hidden verdict after relevance acceptance must include stronger negative display evidence or an explicit display policy reason.

Because a false negative is the higher-severity XVatsim failure, hidden-after-accept should be uncommon and highly visible. The ledger must explain why hiding was safer than displaying with low confidence.

### Deferred

Use when the subject is valid but should be shown later or after pending verification.

Examples:

- arrival-prep relation before display policy permits arrival wake
- phase publisher reuses last proven display while verification is pending
- source temporarily stale but not invalid

### Needs More Evidence

Use when neither accept nor reject evidence is sufficient.

This should be preferred over a low-confidence reject when the system is uncertain.

For display decisions, `needs-more-evidence` should usually map to display-low-confidence or defer-by-stage, not silent hide, unless a separate strong negative or hard block applies.

### Fallback Used

Use when the verdict depends on fallback inference. It can accompany another verdict:

- `accepted + fallback-used`
- `displayed + fallback-used`
- `deferred + fallback-used`

Fallback-use must be visible because it is lower confidence and should be easy to debug.

## Conflicting Evidence

The brain should not let one low-confidence negative fact override strong direct acceptance evidence.

Rules:

- one low-confidence reject must not override high-confidence accept evidence
- one fallback hide must not override a direct accepted display relation fact
- multiple medium-confidence rejects may outweigh weak acceptance
- multiple low-confidence negatives do not automatically outweigh one high-confidence positive
- medium-confidence negatives should lower confidence or priority before hiding unless they clearly dominate
- hard-block evidence must be explicit and rare
- hard-block evidence must name the safety or correctness rule it enforces
- fallback inference must always be lower confidence than direct accepted relation facts
- missing data must not be converted into rejection without an explicit policy reason

Example:

```text
HNL_02_CTR:
  +0.90 controller-relevance accepted CURRENT_POLYGON relation
  -0.15 fallback polygon key PHZH does not match route globals KZAK/KZOA
  final: displayed
  reason: high-confidence accepted relation outranks low-confidence fallback polygon inference
```

## Generic Brain Decision Record

Every final brain decision domain should emit a record shaped like:

```cpp
struct BrainDecisionRecord {
    std::string decisionId;
    std::string subjectKey;
    BrainDecisionDomain domain;
    std::vector<BrainEvidenceItem> evidence;
    double positiveScore;
    double negativeScore;
    std::string confidenceSummary;
    std::string hardBlockReason;
    BrainVerdict finalVerdict;
    std::string finalReason;
    std::vector<std::string> winningEvidenceIds;
    std::vector<std::string> losingEvidenceIds;
    bool fallbackUsed;
};
```

Suggested domains:

```cpp
enum class BrainDecisionDomain {
    RadioRange,
    AuthorityRelevance,
    ControllerRelevance,
    DisplayIntent,
    OverlayRender,
    CtafUnicomCompletion,
    PhasePublisher,
};
```

The subject key should be stable and domain-specific:

- radio-range: callsign + frequency + station index
- authority-relevance: callsign + authority id + polygon id/key + proof source
- controller-relevance: completion stable key
- display-intent: completion stable key or final station key
- overlay-render: final display station key + display index

## Application To Current XVatsim Paths

Across all current paths, fail-soft means source uncertainty should be visible in the ledger before it becomes a hide/reject decision. A domain may still hide or defer candidates, but the decision record must show why negative evidence is strong enough to justify the higher-risk false-negative outcome.

### transceiver_resolver Evidence

Source role:

- report source/cache/fetch status
- report parser/source hygiene counters
- report per-controller and per-station evidence
- report distance, range, frequency, guard, actionable, and station matching facts

Brain scoring:

- high positive: matching usable station within range with non-guard display frequency
- medium positive: station geometry supports reachability but has weaker source detail
- negative: non-actionable, missing transceiver, over max distance, beyond receivable range, empty frequency, guard frequency
- hard-block candidates: malformed source identity or safety-critical invalid data only when explicitly defined

Fail-soft radio-range policy:

- direct usable station evidence should display even if weaker fallback geometry is uncertain
- missing or stale transceiver source data should produce lower confidence or deferred evidence before rejection when other positive evidence exists
- over-distance and beyond-range facts are negative evidence, but the score must explain whether they are decisive or advisory
- empty or guard frequencies can be strong negative or hard-block evidence only when the display frequency is impossible or unsafe to render

Current migrated status:

- normal Resolve live radio board is brain-owned
- ResolveAuthorityStations live candidates are brain-owned
- ResolveAirportCoverage live candidates are brain-owned
- compatibility candidate vectors remain parity-only when evidence exists

### route_sector Authority Relevance Evidence

Source role:

- report route work scope
- report source controllers
- report route-scope polygon facts
- report active polygon facts
- report transceiver route proof facts
- report duplicated-ATIS proof facts

Brain scoring:

- high positive: direct active authority proof that matches route scope
- medium positive: transceiver route proof within tolerance and source ownership matches
- medium positive: duplicated-ATIS proof with eligible facility and owned route-relevant polygon
- negative: unmapped controller, route-scope mismatch, active-not-relevant, geometry mismatch, no usable proof
- hard-block: explicit invalid/unsafe source condition only when defined

Fail-soft authority policy:

- unmapped or missing ownership should not by itself erase a controller when route/proof evidence still supports relevance
- transceiver geometry mismatch may lower confidence before hiding unless it clearly defeats the positive proof
- duplicated-ATIS proof should be advisory unless source/kind/facility evidence is strong enough to accept or reject
- active polygon filtering must be represented as evidence, not a silent route_sector veto

Current migrated status:

- live `AuthorityRelevanceSnapshot::relevantAuthorities` is brain-owned when evidence exists
- `compatibilityRelevantAuthorities` remains diagnostics/parity only

### Controller Relevance

Source role:

- consume route-sector authority relevance
- consume radio-range candidates
- consume departure, arrival, and enroute board evidence
- emit accepted/rejected/deferred completions with stable subject identity

Brain scoring:

- high positive: controller matches a route/authority/radio candidate through brain-owned evidence
- medium positive: controller is plausible for current workflow stage but relation needs verification
- fallback positive: compatibility behavior keeps a row visible while evidence is incomplete
- negative: accepted source later disproven, wrong workflow domain, duplicate completion, stale source

Fail-soft controller-relevance policy:

- a candidate with strong radio or authority evidence should not be rejected only because a weaker route-stage inference is incomplete
- missing route or polygon data should lower confidence, not equal rejection
- rejected relevance completions must expose the evidence ledger and reason so later display code cannot silently reinterpret the outcome

### BrainDisplayIntent Display Decisions

Source role:

- consume accepted relevance completions
- consume display relation facts
- consume route polygon keys, workflow stage, radio tuning, and final board candidates

Brain scoring:

- high positive: accepted completion with direct final display relation
- high positive: tuned current station when policy allows
- medium positive: current/next/arrival polygon match
- low or fallback positive: fallback relation inferred from route distance or missing route metadata
- negative: non-displayable row, stage-deferred row, duplicate row, filtered/hidden relation, phase-publisher pending reuse, overlay cap

Required rule:

A controller accepted by relevance cannot be hidden later by fallback display inference unless the display decision ledger shows stronger negative evidence or an explicit display policy reason.

Fail-soft display policy:

- accepted relation facts are high-confidence positive evidence
- fallback polygon inference is lower-confidence evidence
- hidden-after-accept must explain why hiding is safer than display-low-confidence
- non-displayable rows, such as empty frequency or explicitly offline, may be hard-blocked because they cannot be rendered usefully
- duplicate and stage-deferred rows are display-policy suppressions, not evidence that the controller is irrelevant

### CTAF/UNICOM Completion Decisions

Source role:

- CTAF lookup reports airport, frequency, resolved/unresolved state, fallback UNICOM state

Brain scoring:

- high positive: source CTAF frequency found for the active airport
- fallback positive: UNICOM fallback when CTAF unresolved
- negative: wrong airport, stale lookup, no airport context

Fail-soft CTAF/UNICOM policy:

- unresolved CTAF should not bypass the brain as a final display decision
- UNICOM fallback must be marked fallback and lower confidence than source CTAF
- no airport context should defer or lower confidence before silent suppression unless a hard block applies

Required migration:

- CTAF/UNICOM must produce brain decision records or synthetic completions before display
- final display rows must not bypass the same ledger standards as normal controllers

### Phase Publisher Reuse

Source role:

- report candidate display snapshot, verification pending state, and last-proven snapshot availability

Brain scoring:

- medium positive: candidate display is complete and current
- fallback/defer: last proven snapshot reused while verification is pending
- negative/defer: current candidate not displayable

Fail-soft phase publisher policy:

- reuse should preserve last-proven useful rows while current evidence is pending
- reuse must not silently hide newly accepted completions without a decision record
- pending verification should defer or mark low confidence before hiding

Required ledger:

- every accepted current-cycle completion displaced by last-proven reuse needs a decision record

### Overlay Cap Behavior

Source role:

- report final display index and overlay render cap

Brain scoring:

- displayed: row is within overlay cap
- hidden/deferred by UI capacity: row is beyond cap

Fail-soft overlay policy:

- overlay cap is a UI capacity decision, not evidence that capped rows are irrelevant
- capped rows should have identity and reason exposed
- `+N more ATC` should be treated as deferred/overflow display, not silent hide

Required ledger:

- `overlay-rendered` or `overlay-capped` record for every final display row
- cap count alone is not enough; capped row identity must be visible

## Display-Specific Rule

A controller accepted by relevance cannot be hidden later by fallback display inference unless:

- the display decision ledger contains the accepted completion key
- the ledger records all display evidence used
- negative display evidence outweighs positive accepted relation evidence
- or an explicit display policy hard block applies

Fallback display inference is advisory and low confidence. It must not silently defeat a high-confidence accepted relation fact.

## HNL Class Rule

For the HNL failure class:

- high-confidence evidence: controller relevance accepted `HNL_02_CTR@126.500` as `CURRENT_POLYGON`
- low-confidence negative evidence: fallback polygon inference sees `PHZH` while route globals still say `KZAK/KZOA`
- accepted relation facts are high confidence
- fallback polygon inference is low/fallback confidence
- fallback inference must not hide an accepted center when a high-confidence relation fact supports display

Required scoring outcome:

```text
positiveScore=0.90
negativeScore=0.15
fallbackUsed=true
hardBlock=false
finalVerdict=displayed
finalReason=accepted-relation-fact-outranks-fallback-polygon-inference
```

A high-confidence accepted relation fact must strongly outweigh low-confidence fallback polygon inference, but it is still an evidence item on the normalized scale. It is not an automatic absolute pass unless a separate hard-block policy explicitly says so.

If a future display policy hides the row anyway, it must present stronger negative evidence than the accepted relation fact or an explicit hard-block/display-policy reason. A fallback-hidden record alone is not enough.

## Testing Requirements

Focused scenarios must assert not only final result, but also score and reason summary.

At minimum:

- final verdict
- positive score
- negative score
- confidence summary
- hard block reason if any
- fallback used yes/no
- winning evidence ids/reasons
- losing evidence ids/reasons

Hidden-after-accept must include evidence score explanation:

- accepted completion key
- display decision record
- display/hide/defer verdict
- positive display evidence
- negative display evidence
- why hiding was safer than displaying or display-low-confidence
- final reason

Every hard block must be asserted by focused tests:

- subject key
- hardBlock=true
- named hard-block reason
- positive score
- negative score
- confidence
- winner

Score summaries must expose:

- positive score
- negative score
- confidence
- hardBlock
- winner
- fail-soft recommendation when available

Scores should remain bounded. Focused tests should reject new `100/75/50` style display-intent diagnostic scores unless they are part of a legacy compatibility field outside the normalized decision ledger.

Fail-soft scenarios should prove uncertain candidates are not silently hidden:

- weak negative evidence plus strong positive evidence displays or display-low-confidence
- missing data is ledgered as missing data, not automatic rejection
- fallback inference does not override direct accepted relation facts
- deferred rows include stage/source/reason identity

Migrated paths must keep:

- `droppedBeforeBrain=0`
- compatibility vectors marked non-authoritative when evidence exists
- old-vs-brain mismatch guardrails until compatibility cleanup is complete

## Do Not Regress

- Do not add module-side final decisions.
- Do not let fallback inference outrank direct accepted facts.
- Do not use compatibility vectors as authority when evidence exists.
- Do not hide accepted rows without a display decision record.
- Do not introduce hard blocks without explicit named policy.
- Do not report only final rows; report the evidence and score that produced them.
