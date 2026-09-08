# XVatsim V2 Execution Protocol

Status: active

Approved: 2026-08-26

## Roles

- Darron is the product owner, final decision-maker, and bridge between the
  ChatGPT director task and the Codex engineering task.
- The director maintains roadmap scope, prepares or reviews Contract Gates,
  defines acceptance evidence, and accepts or rejects completed work.
- Codex is the engineer. Codex audits the requested slice, proposes exact files
  and proof, implements only after approval, runs the proof, and commits the
  accepted slice.

The repository is the durable shared memory. Do not rely on unstated context
being shared automatically between separate tasks or model selections.

## One-Step Rule

Only one engineering slice may be active.

The sequence is:

1. Director issues one bounded engineering brief.
2. Codex inspects the current repository without editing.
3. Codex presents a Contract Gate with exact intended files and tests.
4. Darron explicitly approves or rejects the gate.
5. Codex implements only the approved scope.
6. Codex runs focused and proportional full proof.
7. Codex reviews the diff for unrelated changes.
8. Codex commits the completed slice with a narrow commit message.
9. Director reviews source changes, proof artifacts, test output, and commit.
10. Darron accepts the slice before another task begins.

If proof fails, the task remains active. Do not commit a claimed completion and
do not begin the next roadmap step.

## Contract Gate Requirements

Before source or tooling edits, Codex must state:

- objective and explicit non-goals
- exact files intended to change or add
- source of truth for every new fact
- brain-owned decisions and module-produced facts
- whether `plugin/src/XVatsimPlugin.cpp` is touched and why
- live behavior changed: yes or no
- incorrect or obsolete behavior replaced/deleted instead of patched around
- focused proof to add or run
- full regression/build proof to run
- performance risk and measurement method
- rollback boundary

Approval applies only to that gate. It does not carry into the next change.

## Evidence Receipt

Every completed engineering slice must report:

- starting and ending commit
- complete changed-file list
- focused test commands and results
- full scenario count and result
- build targets and configuration
- elapsed test/build time where available
- hashes of relevant binaries or packages
- visual artifacts for UI work
- performance measurements for runtime work
- live-test evidence or an explicit statement that live proof remains pending
- known limitations and deferred work

The director may independently rerun or inspect any claimed proof. A statement
such as `implemented`, `compiled`, or `tests pass` without evidence is not an
acceptance result.

## Commit Rule

- One approved slice per commit unless a documented reason requires more.
- No unrelated cleanup, formatting, dependency upgrades, or feature work.
- Generated build products and transient logs stay out of source commits.
- Reviewable proof reports may be committed under `outputs/` when the roadmap
  step calls for them.
- Commit messages should identify the proven outcome, not merely the edited
  file, for example: `test: establish v2 windows validation baseline`.

## Stop Conditions

Stop and return to Darron when:

- the requested outcome requires a product choice not in the approved roadmap
- exact source ownership conflicts with the brain-owned runtime contract
- existing user changes overlap the intended files
- a required test cannot be executed or its result cannot be proven
- implementation would expand into another roadmap step
- the working tree contains unexplained changes
