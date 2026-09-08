# V2 Step 1 Engineering Brief - Windows Proof Baseline

Status: ready for Codex Contract Gate

Issued: 2026-08-26

## Objective

Establish one repeatable Windows V2 development-validation entry point that
proves the closed V1.2.3 baseline can be rebuilt and fully regression-tested
before any V2 feature behavior is introduced.

## Required Outcome

The proposed validation path must:

- build `XVatsimRegressionHarness` and `XVatsimPlugin` in Release configuration
- discover and run every saved `.scn` regression scenario in deterministic
  filename order
- report expected, discovered, executed, passed, and failed scenario counts
- fail with a non-zero result on configuration, build, missing-scenario, or
  scenario failure
- record total elapsed time
- record SHA-256 hashes for the built harness and plugin
- write a concise proof receipt outside generated package folders
- remain suitable for repeated local Windows development use

## Boundaries

- Live plugin behavior must not change.
- No VFR, ORB, METAR, ATIS, or PDC implementation begins in this step.
- Do not modify the V1.2.3 release package or its recorded hashes.
- Reuse existing build/release-gate behavior where practical; do not create a
  competing test framework.
- Generated build products and transient per-scenario logs remain ignored.
- Do not change the 451 scenario inputs merely to make the baseline pass.

## First Codex Response

Codex must inspect the repository and respond with a Contract Gate before any
edit. The gate must name exact files, explain how existing release tooling is
reused, define the proof-receipt location, and list the commands/results that
will prove the new validation entry point.

Do not edit, run destructive commands, or commit during the Contract Gate turn.

## Acceptance Evidence

After approval and implementation, completion requires:

- the new validation entry point passes from the repository root
- Release plugin and harness builds pass
- all `451 / 451` scenarios pass
- the receipt contains counts, elapsed time, configuration, commit, and hashes
- an intentional non-destructive negative-path check proves failures propagate
  as a non-zero result
- the working-tree diff contains only approved Step 1 files
- the completed slice is committed with a narrow commit message
