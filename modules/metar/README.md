# VATSIM METAR fact worker

This removable worker accepts one brain-issued, normalized airport request,
performs one bounded HTTPS request against `metar.vatsim.net`, returns immutable
transport facts, and stops. It owns no target, cadence, parsing, category,
freshness, history, lookup, lifecycle, or presentation policy.

The production WinHTTP state machine registers and waits for
`WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE` before beginning response
receipt. Every dispatched request terminates in one immutable bounded fact with
the request identity and purpose, primary/lookup generations, terminal stage,
WinHTTP operation/result/error, HTTP status, payload byte count, elapsed network
time, completion monotonic time, and a bounded reason. Raw response bodies and
raw METAR text are never diagnostic fields.

The proof build may replace only the endpoint with a loopback-only peer. The
normal module remains fixed to `metar.vatsim.net`, contains no loopback or
synthetic response, and has no alternate weather source. Cancellation closes
the active request, waits for callback closure, and joins the worker. A terminal
fact drained at a disabled or stopped lifecycle boundary is cleanup evidence
only and is never accepted as weather.
