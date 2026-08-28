# VATSIM METAR fact worker

This removable worker accepts one brain-issued, normalized airport request,
performs one bounded HTTPS request against `metar.vatsim.net`, returns immutable
transport facts, and stops. It owns no target, cadence, parsing, category,
freshness, history, lookup, lifecycle, or presentation policy.
