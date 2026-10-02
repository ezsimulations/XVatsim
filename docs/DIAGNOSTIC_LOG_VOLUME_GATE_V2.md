# Diagnostic Log Volume Contract Gate V2

Gate: `DIAGNOSTIC-LOG-VOLUME-02`

Status: **PROPOSED — NO IMPLEMENTATION AUTHORIZED**

Baseline: XVatsim V2.1.0, commit `f61c0e649b3e1376a93285342451f73014b275b7`,
plugin SHA-256
`0D4ACC2A99309EE3ACC640798C286144044E57595E93AD8F7364485A65CBAEEF`.

This gate corrects one diagnostic serializer. It does not authorize any change
to controller selection, scoring, workflow, UI, xPilot bridge, METAR, ATIS,
PDC, polygon, distance, color, or Standby Assist behavior.

## Confirmed defect

The V2.0.2 repair successfully replaced routine per-callback timing records
with one-minute summaries. A separate `candidate-completion-trace` formatter
was expanded during the Australia Brain-vote work so that it now writes every
candidate's complete vote narrative again whenever any candidate evidence
changes.

The 1 October 2026 log contained:

| Evidence | Observed |
| --- | ---: |
| Total file | 52,488,079 bytes |
| `candidate-completion-trace` | 48,304,436 bytes |
| Candidate trace records | 1,374 |
| Average candidate trace | 35,156 bytes |
| Largest candidate trace | 70,163 bytes |
| Candidates in largest trace | 49 |
| Flight-loop summaries | 78,012 bytes |

The defect is the repeated, unbounded diagnostic representation. It is not a
Brain-scoring, worker-frequency, or flight-loop performance defect.

## Bible law and ownership boundary

The Brain remains the only decision maker.

- Every worker continues to give the Brain all requested facts.
- Every controller evidence source continues to reach the Brain with its exact
  `+1`, `0`, or `-1` score.
- No diagnostic code may filter, alter, hide, promote, reject, or suppress any
  fact before the Brain receives it.
- The Brain finishes its decision before diagnostics receives an immutable
  decision receipt.
- Diagnostic deduplication and formatting cannot feed back into Brain state,
  worker state, UI state, or scheduling.

This gate changes what is written after a decision. It does not change the
decision or the information available to the Brain.

## Required correction

Replace the current full-board `candidate-completion-trace` with two bounded,
post-decision records.

### Candidate decision summary

One compact `candidate-decision-summary` records:

- session and Brain decision generation;
- phase, plan, and polygon identities;
- radio-board and route hashes;
- total, accepted, rejected, pending, displayed, and changed counts;
- a digest of the complete ordered decision set.

The summary must not exceed 1,024 UTF-8 bytes.

### Candidate vote receipt

On the first complete decision generation, one `candidate-vote-receipt` is
written for every candidate. After that baseline, another receipt is written
only for a candidate whose material evidence or Brain result changed.

Each receipt includes:

- callsign, frequency, and facility group;
- accepted, rejected, or pending result;
- displayed state and Brain-assigned relation;
- current and matched polygon plus route-entry distance;
- positive, negative, and neutral totals;
- every evidence source name and exact `+1`, `0`, or `-1` score;
- a compact semantic reason for each source;
- the final Brain reason.

Rejected and neutral evidence must remain visible in diagnostics. The formatter
may replace repeated free-form prose with stable reason tokens and material
values, but it may not omit a source or change its score.

Pure transport churn is not a material evidence change. A new timestamp,
process epoch, connection epoch, polling sequence, or xPilot snapshot sequence
does not re-emit all candidate receipts when the facts, scores, reasons, and
Brain result are unchanged. A transport change that changes source
availability, facts, scores, reasons, or the Brain result remains material.

No physical diagnostic line may exceed 4,096 UTF-8 bytes. An exceptional
receipt that cannot fit is split into numbered records carrying the same
candidate key and receipt digest. Silent truncation is forbidden.

## Scope boundary

Expected implementation changes after approval:

- replace the unbounded completion-trace formatting path;
- add one focused diagnostics-only reducer/formatter if needed;
- add focused regression coverage;
- record the offline and live proof in this gate.

No Brain decision file should require a behavioral change. If a Brain file
appears to require one, implementation stops and this gate returns to the
Product Owner before that change is made.

The existing asynchronous diagnostics writer, date-stamped files, and
three-date retention policy remain unchanged. This correction does not add a
new rotation system or another runtime scheduler.

## Sufficient automated proof

The gate requires these focused tests:

1. **Incident replay**
   - Reproduce the observed 49-candidate board with eight evidence sources per
     candidate.
   - Prove all 49 candidates and all source scores appear in bounded receipts.
   - Prove no line exceeds 4,096 bytes.
   - Prove the complete initial diagnostic output remains below 256 KiB.

2. **Unchanged transport replay**
   - Repeat that board 1,000 times while changing only timestamps and xPilot
     snapshot sequence numbers.
   - Prove zero repeated candidate receipts and no more than 32 KiB of
     additional diagnostic output.

3. **Single-candidate change**
   - Change one source score and reason for one candidate.
   - Prove exactly one candidate receipt plus one compact summary is emitted.
   - Prove the added output remains below 8 KiB.

4. **Brain evidence proof**
   - Verify every source name, exact score, vote total, and final Brain result
     in the persisted receipts against the completed Brain decisions.

5. **Behavior identity**
   - Run the existing Australia, controller relevance, polygon, PDC, METAR,
     ATIS, UI, xPilot bridge, performance, and complete regression suites.
   - Final Brain decisions and display snapshots must remain unchanged.

6. **Performance proof**
   - Run the existing flight-loop diagnostics probe.
   - Prove diagnostic work remains asynchronous and no measured callback is
     pushed above 1 ms by this change.

These are accelerated automated tests. They do not wait for an hour or eight
hours of wall-clock time.

## Offline implementation proof — 2026-10-01

- Release builds passed for `XVatsimPlugin` and `XVatsimRegressionHarness`.
- The 49-candidate/eight-vote incident load produced a 46,068-byte one-time
  baseline across bounded summary and per-candidate records.
- 1,000 completed Brain decision observations containing only radio/route
  hashes, distance provenance, xPilot process epochs, connection epochs, and
  snapshot sequence churn produced zero duplicate records.
- One material candidate vote/Brain result change produced exactly one changed
  candidate receipt.
- Candidate removal produced one explicit removal receipt, and reset produced a
  fresh complete baseline.
- A synthetic 29-vote candidate split into three numbered records. Every source,
  exact ternary score, and semantic reason remained present; every record stayed
  below the 4,096-byte physical-line limit.
- Reducer timing across the 1,000-observation load averaged 108 microseconds and
  peaked at 343 microseconds on this machine.
- All 10 focused Australia/Brain, runtime, performance, PDC, and xPilot bridge
  contract probes passed. The asynchronous route-performance probe also passed
  an isolated rerun after one contention-sensitive batched run.
- 887 applicable regression scenarios passed. One machine-locked legacy xPilot
  3.0.2 fingerprint scenario was excluded because this machine currently has
  the xPilot 4 beta X-Plane plugin (`2,257,072` bytes, SHA-256
  `DEA6B12F64C0E35F9E61398B9AA659C3A8896886B4890D4EC89C50ACD6CD22A2`),
  while that fixture requires the old 3.0.2 binary (`4,990,976` bytes). The
  relative-path canonicalization companion scenario passed.

## Controlled live proof

After the automated proof passes, the candidate remains uncommitted and is
installed only after X-Plane and xPilot are closed.

The Product Owner will conduct one long online flight as the final live proof.
Record the diagnostic file size at startup, after the departure/controller-dense
phase, during cruise, after arrival, and after normal shutdown. Calculate total
growth and bytes per flight hour. The flight must confirm:

- normal controller display, colors, distances, and Standby Assist;
- normal METAR, ATIS, PDC, and xPilot bridge behavior;
- readable vote receipts for accepted and rejected candidates;
- no repeated full-board records during unchanged operation;
- expected file growth and healthy flight-loop summaries;
- no return of large repeated candidate traces during any phase of flight.

The Product Owner chooses the natural flight length. No artificial eight-hour
simulation is required before that flight, and the live result remains the final
approval evidence.

## Rollback and approval boundary

- Preserve the V2.1.0 tag, package, installed rollback binary, and plugin hash.
- Keep implementation changes uncommitted through controlled live testing.
- If any Brain decision, controller display, polygon color, distance, PDC,
  METAR, ATIS, bridge behavior, or performance result changes, restore V2.1.0
  and reject the candidate.
- Approval of this gate authorizes implementation and automated proof only.
- Deployment, commit, release, updater changes, and deletion of incident logs
  or crash dumps require separate authorization.
