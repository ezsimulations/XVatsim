# YMML Brain evidence gate

Gate: `YMML-BRAIN-EVIDENCE-01`

Baseline: `e34baf1912a112812e85c1940c2031bb0b922a7c` (`v2.0.1`)

User authorization: approved in the 15 September 2026 recovery session.

## Problem to repair

At Melbourne, live controllers such as `ML_GND`, `ML_TWR`, `ML_APP`,
`ML_DEP`, `ML-S_DEP`, and `ML-R_APP` can be omitted when a worker requires an
airport-derived callsign string to match exactly. The worker is not allowed to
turn a mismatch into a hidden row before the Brain sees the remaining evidence.

The captured Melbourne-to-Sydney case also requires extended Center coverage.
`ML-GUN_CTR` logged in on `133.150` and declared `BLA 132.2` in an affirmative
extension statement. VATSpy maps `ML-BLA` to `YBLA`. The Brain must be able to
use the controller's original `132.200` AFV transmitter as the contact channel
when the route is currently in `YBLA`, while retaining `133.150` as the login
channel fact.

## Ownership contract

Workers and source parsers may fetch, parse, normalize, compare, calculate
distance, and report geometry. They return every requested candidate and every
available fact. They must not return a survivor list, hide a candidate, assign a
display relation, select a winner, or make an accept/reject decision.

The Brain alone:

- checks lifecycle, phase, facility, guard-channel, and radio-board eligibility;
- reduces each independent evidence family to one score: pass `+1`, neutral `0`,
  or fail `-1`;
- counts positive and negative scores without weights;
- displays an eligible candidate when it has at least one positive score and
  positive scores are greater than or equal to negative scores;
- excludes an eligible candidate only when negative scores outnumber positive
  scores;
- chooses current, next, arrival, hidden, and standby/contact-channel intent;
- records the complete receipt used for the decision.

Missing, disabled, stale, incomplete, or inapplicable evidence is neutral. It is
not a negative vote. Confidence and provenance may be recorded for diagnostics,
but neither may alter the score or veto the Brain's result.

## Required evidence families

Every endpoint controller receipt records these families exactly once:

1. `vatsim`: live actionable controller, facility, service suffix, and any
   complete coverage declaration.
2. `vatspy`: source-backed airport aliases and Center-to-boundary bindings.
3. `vnas`: United States operational ownership evidence; neutral outside its
   supported scope or when unavailable.
4. `frequency-distance`: nearest same-channel AFV transmitter distance from the
   applicable departure or arrival airport. At or within five nautical miles is
   `+1`, beyond five nautical miles is `-1`, and missing coordinates are `0`.
5. `terminal-source`: SimAware or another applicable published terminal-owner
   match when available.
6. `published-frequency`: an applicable published role/frequency confirmation;
   a missing or non-confirming optional catalog is neutral.

Center receipts use the same `+1/0/-1` rule for VATSIM declaration, VATSpy
binding, route geometry, and receivable same-controller channel facts.

Multiple facts derived from one source family count once. Conflicting facts in
one family reduce that family to neutral and are retained in the receipt.

## Source matching

VATSpy `[Airports]` records are parsed as source facts. For YMML they establish
the published `ML`, `ML-S`, and `ML-R` aliases. A worker reports whether the
observed prefix is an exact alias, a source-backed root/sector variation, a
confirmed mismatch, or unknown. Arbitrary global wildcard acceptance is
forbidden.

An affirmative Center coverage declaration is only usable when its shorthand
or full position binds unambiguously to a current or next route sector and the
same controller has a usable original AFV transmitter on the declared channel.
The primary login channel and every original transmitter channel remain separate
facts.

## Diagnostic receipt

Every radio-board candidate produces one Brain completion receipt, including
rejected and neutral cases. The receipt contains:

- callsign, role, endpoint, chosen relation, and chosen contact channel;
- each evidence-family name, score, provenance, and short reason;
- positive, negative, and neutral totals;
- Brain eligibility result and final display/hide decision.

The live diagnostic log must emit the complete receipt without truncating the
vote list. This receipt is the required evidence during a controlled online test.

## Acceptance cases

- YMML `ML_GND`, `ML_TWR`, and `ML_DEL` receive all evidence scores and display
  when their Brain tally is non-negative.
- `ML_APP`, `ML_DEP`, `ML-S_DEP`, and `ML-R_APP` use source-backed YMML aliases;
  no syntax worker can suppress them.
- An unrelated prefix receives a VATSpy `-1` when complete source facts exist,
  but still reaches the Brain with all other scores.
- vNAS contributes `0` outside the United States and never removes a candidate.
- Endpoint transmitter distances produce exactly `+1`, `0`, or `-1` at the
  five-nautical-mile boundary.
- The captured `ML-GUN_CTR` extension selects current `YBLA` and `132.200` only
  when the VATSIM declaration, VATSpy binding, route relation, and original AFV
  channel facts support it.
- Withdrawing or invalidating the declaration removes the extension evidence;
  independently valid direct or next Center evidence remains available.
- The number of Brain receipts equals the number of requested radio-board
  candidates.
- Existing V2.0.1 regression scenarios remain behaviorally valid unless an old
  expectation explicitly depended on weighted scores or worker suppression. Any
  such expectation is replaced with the corresponding Brain receipt.

## Deployment boundary

The candidate passed offline checks, the controlled YMML live test, and the
subsequent full-flight online validation. The Product Owner approved the live
result and authorized committing it on 17 September 2026.
