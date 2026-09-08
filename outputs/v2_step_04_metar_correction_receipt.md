# XVatsim V2 Step 4 METAR Correction Receipt

Date: 2026-08-28

## Status

**STEP 4 CORRECTED AND OFFLINE-PROVEN — CONTROLLED LIVE REPROOF NOT AUTHORIZED**

Implementation/evidence commit:
`5fb66a0daf9dca2101052ecd10213006e357b21a`

This receipt is the sole file in the separately authorized receipt-only
commit. That commit's identifier is resolved from Git because it cannot be
embedded in its own committed bytes.

## Failure cause and exact correction

The failed controlled-live proof registered
`WINHTTP_CALLBACK_FLAG_SEND_REQUEST` but waited for
`WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE`. The production worker now
registers `WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE`, waits for that exact
completion, and calls `WinHttpReceiveResponse` only afterward.

The correction also adds bounded one-shot dispatch, terminal-worker, and
brain-disposition diagnostics, plus the locked minimal METAR ORB. A terminal
fact drained after cancellation/join is cleanup evidence only: it is rejected
without parser execution, weather acceptance, cache/history mutation,
presentation change, drawer/ORB effect, or further scheduling.

## Regression and transport proof

- Corrective scenarios: 14/14 passed.
- Complete Step 4 focused scenarios: 67/67 passed.
- Complete saved scenarios: 561/561 passed.
- Canonical scenario fingerprint:
  `6AB913C1C0658A0CDE93A84D369267DDFDCD7FDE27797BF097FDB70DD574EF94`.
- Successful real-WinHTTP loopback lifecycle: 17 milliseconds.
- Exact request path: `/KDFW?format=json`.
- Success path: send completion, response headers, HTTP 200, complete bounded
  payload read, matching JSON/station acceptance, worker harvest, one brain
  parse, defensible VFR category, one history insertion, and complete worker
  and callback shutdown.
- Real loopback non-200 and malformed-JSON cases produced the correct terminal
  failure ledgers.

## Diagnostic matrix

PASS: startup, send start, send completion, receive start, response headers,
HTTP rejection, data availability, read, payload bound, JSON rejection,
wrong-station rejection, brain parser rejection, and successful acceptance are
distinguishable. Each request produces one bounded dispatch record, one
harvested terminal record, and one brain-disposition record. There are no raw
bodies, raw METARs, private/account data, repeated busy lines, or repeated
terminal records.

The failed-live symptom would now terminate as a send-completion timeout or
failure with the request identity, generations, WinHTTP result/error, elapsed
network time, and brain rejection visible in the bounded ledger.

## Visual and quiet-runtime proof

- Production-raster visuals: 15/15.
- Repeat render PNG differences: 0.
- Exact neutral and successful ORB string assertions: passed.
- Main-card signature outside the authorized ORB change:
  `8949928878432326300`, unchanged in all cases.
- Warm unchanged cycles: 100,000.
- Recurring parse, fingerprint, history, wrapping, raster, upload,
  publication, and terminal-diagnostic work: zero.
- Five injected cancellation phases maximum: 15 milliseconds.
- Real stalled-response WinHTTP cancellation maximum observed: 1 millisecond.
- Cancellation limit: 500 milliseconds.

## Windows Release and binary isolation

- Fresh Release proof harness/visual build: passed.
- Fresh Release fixture-off normal plugin build: passed.
- Normal plugin size: 2,403,840 bytes.
- Normal plugin SHA-256:
  `2511F7BA9992893378423DE1F113CBAA04971DB7DD0F17505DA06FBCA0943288`.
- Required normal-binary markers present: `metar.vatsim.net`,
  `VATSIM_METAR`.
- NOAA, Aviation Weather, loopback endpoint/address, synthetic corrective
  METAR, injected transport, proof endpoint, and proof-fixture markers: absent.

## Evidence and boundaries

- Original protected evidence: 116/116 files byte-identical; inventory
  fingerprint remains
  `B63A3726E4B785FD80D982AA564A63124D0F815CBFBC9A579AC4E00CF4956D51`.
- Failed-live protected evidence: all 34 physical files byte-identical; the 33
  manifest entries verify and the manifest SHA-256 remains
  `7CA785AC6367AD9C32D6F89C8CAB8A6205BE3E622C343AB847A44ADB6D939ED0`.
- No plugin was deployed or staged.
- X-Plane and xPilot were not started.
- No live VATSIM request occurred; the only network proof used loopback
  `127.0.0.1` in the proof-only build.
- V1.2.3 was untouched.

Contract deviations: **none**.

A new controlled-live-proof gate and Darron's explicit approval are required
before deployment, X-Plane/xPilot startup, or contact with the live VATSIM
METAR endpoint.
