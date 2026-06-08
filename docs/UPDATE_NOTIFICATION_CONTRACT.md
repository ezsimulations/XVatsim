# Update Notification Contract

Status: Approved, implemented, and live-tested for the 1.0.2 freeware
release.

This contract defines the future in-plugin update notification behavior for
XVatsim. Darron approved this contract for the local 1.0.2 candidate before
implementation.

This contract also carries the approved design direction for brain-owned
frequency ordering and standby-assist ownership. The two work items were
implemented together for the 1.0.2 freeware release.

## Purpose

XVatsim should be able to notify users that a newer freeware build is available
without becoming an installer, downloader, or auto-updater. The plugin remains
notify-only: users download the new ZIP from the X-Plane.org file page.

## Manifest Source

- The plugin reads a public JSON manifest named `xvatsim_update.json`.
- Expected public URL:
  `https://ezsimulations.github.io/XVatsim/xvatsim_update.json`
- The manifest includes `latest_version`, `minimum_supported_version`,
  `package_sha256`, `plugin_sha256`, `download_page_url`, `critical`,
  `message`, and `release_notes`.
- If the main repository remains private, the manifest must still be publicly
  reachable through GitHub Pages or a separate public update repository.

## Check Cadence

- The plugin may check quietly in the background after plugin load.
- The plugin must not block the X-Plane flight loop while checking.
- The plugin should persist the last check time and avoid automatic checks more
  often than once every 24 hours.
- A manual menu action may bypass the 24-hour automatic-check interval.
- The result should be cached for the current simulator session.

## Version Decision

- The installed plugin version is compared to `latest_version`.
- `plugin_sha256` may be used as an additional integrity/status signal when it
  is practical to hash the installed plugin file.
- If `latest_version` is newer than the installed version, status is
  `available`.
- If the installed version is equal to `latest_version` and the expected hash
  matches or hash checking is unavailable, status is `current`.
- If the manifest cannot be fetched, parsed, or trusted, status is
  `check-failed`.
- Unknown or malformed manifest fields must fail closed and must not produce a
  false update notice.

## Hidden UI Rule

- Before battery power and xPilot connection, the update checker must not wake
  the overlay just to say the version is current or an update is available.
- While hidden, the plugin may log diagnostics only:
  - `updateStatus=current`
  - `updateStatus=available latest=<version>`
  - `updateStatus=check-failed`
- Normal controller/radio display ownership remains unchanged.

## Natural Wake Notification

- If an update is available, the notification is queued while the overlay is
  hidden.
- The queued notice appears the first time the overlay naturally wakes after
  battery power and xPilot connection.
- The update notice must be a small status/banner line and must not replace the
  active ATC board.
- Suggested text:
  - `XVatsim <version> update available`
  - `Download from X-Plane.org`
- The banner should be dismissible or naturally expire so it does not distract
  from active ATC.

## Current Version Behavior

- The plugin should not show `XVatsim is current` on every normal flight.
- Current/valid status should remain silent during automatic checks.
- Current/valid status may be shown only after a manual user action such as
  `XVatsim > Check for Updates`.
- Current/valid status should still be written to diagnostics.

## Critical Update Behavior

- If `critical` is true and a newer version is available, the plugin may be
  more assertive after battery power is available.
- A critical notice may appear even before route ATC naturally wakes, but it
  still must not block flight controls, radio state, xPilot, or route logic.
- Suggested text:
  - `Critical XVatsim update available`
  - `Download from X-Plane.org`

## Manual Menu Behavior

- Add a menu item: `XVatsim > Check for Updates`.
- Manual check results may display:
  - update available,
  - current version valid,
  - check failed,
  - check skipped/offline.
- Manual check output may open the overlay briefly as a user-requested message.

## Diagnostics

Every automatic or manual check should log:

- check source: `automatic` or `manual`
- installed version
- latest manifest version when available
- status: `current`, `available`, `check-failed`, or `disabled`
- whether the notice was queued, shown, dismissed, or expired
- manifest URL
- HTTP/fetch error class when applicable, without noisy payload dumps

## Non-Goals

- No automatic installation.
- No automatic ZIP download.
- No plugin replacement while X-Plane is running.
- No forced browser launch.
- No update notice that wakes the overlay before the normal battery/xPilot
  readiness rule, except for critical update behavior after battery power.

## Brain-Owned Frequency Intent

XVatsim must have one decision maker for frequency display order, active row
state, standby row state, and standby-assist target selection: the brain.
Modules may report facts only. They must not decide final UI order, final row
labels, or which frequency belongs in COM1 standby.

Module responsibilities:

- report available stations/frequencies;
- report role facts such as Delivery, Ground, Tower, TRACON, Center, CTAF, or
  UNICOM;
- report evidence facts such as airport match, current polygon, next polygon,
  arrival polygon, online/offline, route-entry distance, and authority source;
- report radio state as sampled data;
- avoid final display decisions such as `Active`, `Standby`, final row order,
  or next-assist target.

Brain responsibilities:

- merge all reported station/frequency facts;
- apply the workflow-specific priority order;
- decide the final ordered UI frequency list;
- decide which row is `Active` using COM1 active frequency only;
- decide which row is the standby-assist target;
- tell the assist helper which frequency to load;
- tell the UI exactly what rows to render and in what order.

UI responsibilities:

- render the brain-owned final ordered list;
- avoid independent row sorting that can contradict the brain;
- avoid deciding `Active`, `Standby`, `Next`, or final priority.

Assist helper responsibilities:

- receive the brain-selected target frequency;
- write the target to COM1 standby only when assist is enabled and the latch
  allows it;
- report `already loaded`, `loaded`, or `failed`;
- never choose the target frequency.

## Frequency Priority Rules

Departure mode display and assist priority:

1. Delivery
2. Ground
3. Tower
4. TRACON, meaning Approach or Departure
5. Center in the current polygon
6. Center in the next polygon
7. CTAF or UNICOM

Arrival mode display and assist priority:

1. Center in the current polygon
2. TRACON, meaning Approach or Departure
3. Tower
4. Ground
5. Center in the next polygon
6. CTAF or UNICOM

CTAF/UNICOM rules:

- CTAF/UNICOM always displays at the bottom of the list for the applicable
  mode.
- CTAF/UNICOM is not standby-assist eligible unless a later approved contract
  explicitly changes that policy.

Center relation rules:

- Current-polygon center always outranks next-polygon center for assist.
- In Departure mode, next-polygon center follows current-polygon center.
- In Arrival mode, next-polygon center is a fallback after current-polygon
  Center, TRACON, Tower, and Ground have no eligible next frequency.
- Center relation comes from brain-owned route/display relation facts, not from
  lexicographic callsign or frequency order.

COM1 active rule:

- COM1 active frequency is the only radio frequency that advances the standby
  assist target.
- COM2 may still be displayed in the radio status area, but COM2 must not mark
  ATC rows as `Active` and must not move the standby-assist pointer.

Standby-assist selection rule:

- The brain walks the final ordered list.
- The row matching COM1 active is labeled `Active`.
- The first eligible row after the COM1 active row becomes the brain-selected
  standby-assist target.
- If COM1 is not tuned to any displayed eligible row, the first eligible row in
  the final ordered list becomes the standby-assist target.
- Rows already tuned to COM1 are never selected as standby.
- Guard and blocked frequencies remain ineligible.

Required examples:

- Departure with COM1 on Tower and TRACON available: standby target is TRACON.
- Departure with COM1 on TRACON, current center available, and next center
  available: standby target is current center.
- Departure with COM1 on current center and next center available: standby
  target is next center.
- Arrival with COM1 on center and TRACON available: standby target is TRACON.
- Arrival with COM1 on Ground, no other current-polygon arrival frequency, and
  next center available: standby target is next center.
- CTAF present with no other valid next frequency: CTAF remains bottom display
  row and does not become a standby-assist target.

## Cleanup Rule

The implementation must not leave competing decision paths behind.

- Existing module-side or UI-side ordering that can contradict brain-owned
  frequency intent must be removed or reduced to passive rendering/reporting.
- Existing standby-assist target selection must be converted from
  helper-decided to brain-decided.
- Any old code that still computes final `Active`, `Standby`, final row order,
  or assist target outside the brain must be deleted, not left dormant.
- Regression coverage must prove the old next-polygon-before-current-polygon
  standby bug cannot recur.

## Approval And Test Workflow

- After this contract is completed and approved, code changes may begin.
- After code changes, do not commit and do not build a new freeware package
  until Darron performs a live visual/real-life simulator test.
- If the live test passes, then commit, compute fresh hashes, update the update
  manifest, and rebuild the freeware package.
- If the live test fails, revise the code under the same contract before any
  commit/package step.

## Approval State

Darron approved this contract for implementation in the 1.0.2 candidate.
Diagnostics passed, GitHub Pages served the update manifest successfully, and
Darron reported the live simulator test passed before commit/package closeout.
After the live pass, the 1.0.2 freeware package was rebuilt, the public update
manifest hashes were refreshed, and the package smoke check passed.
