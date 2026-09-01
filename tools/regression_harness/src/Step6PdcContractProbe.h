#pragma once

#include <array>
#include <string>
#include <string_view>

namespace xvatsim::tools::step6_pdc_proof {

struct Step6ProbeMetadata {
    std::string_view scenarioSuffix;
    std::string_view sourceSetup;
    std::string_view expectedRedFact;
    std::string_view architectureRequirement;
};

inline constexpr std::array<Step6ProbeMetadata, 61> kStep6ProbeMetadata{{
    {"01_source_qualification", "qualified exact xPilot source and triplet", "mechanical qualification seam is available", "14.1.1"},
    {"02_capability_fail_closed", "missing and type-wrong triplet capabilities", "mechanical source is incomplete", "14.1.2"},
    {"03_coherent_tuple", "equal sequences bracket bounded sender and body", "one coherent tuple", "14.1.3"},
    {"04_torn_retry", "sequence and source change during observation", "torn tuple is never authoritative", "14.1.4"},
    {"05_status_matrix", "stable and transitional status/callsign samples", "status evidence remains mechanical", "14.1.5"},
    {"06_text_bounds", "sender/body boundaries and unsafe text", "independent bounded issue bits", "14.1.6"},
    {"07_duplicate_fast_path", "Brain-issued connected-disposition token", "sequence-only duplicate with zero string reads", "14.1.7"},
    {"08_unsupported_runtime", "unqualified source fingerprint", "fail closed without fallback", "14.1.8"},
    {"09_disconnected_positive_race", "disconnected zero then unseen one then connected one", "one provisional admission instead of suppression", "14.2.9"},
    {"10_status_transition_provisional", "status changes while sequence one is sampled", "provisional candidate retained", "14.2.10"},
    {"11_cold_disconnected_provisional", "cold first disconnected positive", "incomplete candidate admitted on connection", "14.2.11"},
    {"12_cold_connected_startup_recovered", "cold first stable-connected positive", "startup-recovered admission", "14.2.12"},
    {"13_reconnect_duplicate", "prior connected N then reconnect unchanged N", "zero replay", "14.2.13"},
    {"14_disconnected_advance", "connected N then disconnected N+1", "one later admission", "14.2.14"},
    {"15_multiple_provisional_order", "multiple disconnected advances", "bounded exact provisional order", "14.2.15"},
    {"16_forward_gap", "unobserved forward jump", "latest only plus persistent gap", "14.2.16"},
    {"17_sequence_epoch_matrix", "duplicate plus-one rollback zero wrap source change", "exact epoch dispositions", "14.2.17"},
    {"18_transport_capacity", "full FIFO and occupied retained slot", "exact immutable retained delivery", "14.2.18"},
    {"19_full_transport_no_suppression", "new connected fact while transport full", "connected disposition does not advance before delivery", "14.2.19"},
    {"20_pending_context", "message precedes flight plan roster workflow callsign", "pending identity retained", "14.3.20"},
    {"21_in_place_promotion", "context arrives before and after twenty seconds", "same identity promoted without duplicate", "14.3.21"},
    {"22_strict_controller_pdc", "fresh relevant controller and strict clearance evidence", "ControllerPdc classification", "14.3.22"},
    {"23_controller_operational", "controller message missing strict PDC evidence", "ControllerPrivateOperational classification", "14.3.23"},
    {"24_advertisement_not_pdc", "controller PDC-available advertisement", "not a clearance", "14.3.24"},
    {"25_pilot_not_pdc", "proven pilot using PDC words", "PilotPrivate classification", "14.3.25"},
    {"26_unknown_sender", "stale roster absent sender unprovable staff", "UnknownDirectPrivate retained", "14.3.26"},
    {"27_typed_staff_unavailable", "staff-looking sender without typed authority", "no fabricated staff classification", "14.3.27"},
    {"28_workflow_classification", "same strict message across workflow modes", "PDC only in IFR Departure", "14.3.28"},
    {"29_non_private_channels_excluded", "radio broadcast server chat text_atis ATIS", "zero Step 6 product admission", "14.3.29"},
    {"30_orb_states", "cold source idle unread read gap combined states", "exact PDC ORB truth", "14.4.30"},
    {"31_drawer_headings", "all reachable private classifications", "exact drawer title and headings", "14.4.31"},
    {"32_history_bounds", "sender per-sender total and byte limits", "deterministic bounded eviction", "14.4.32"},
    {"33_group_ordering", "multiple senders sequences and accepted-time ties", "deterministic newest-first groups", "14.4.33"},
    {"34_unread_saturation", "one through thirty-two unread", "exact saturating unread status", "14.4.34"},
    {"35_utf8_scroll_layout", "long UTF-8 markup URL gap and wheel input", "literal bounded rendered scrolling", "14.4.35"},
    {"36_owned_drawer_reset", "new message while PDC owns drawer", "one viewport reset to unread", "14.4.36"},
    {"37_hidden_update_no_steal", "new PDC message while METAR or ATIS owns", "no drawer theft or foreign reset", "14.4.37"},
    {"38_visible_identity_limit", "partially visible and offscreen unread entries", "maximum sixteen exact drawn identities", "14.4.38"},
    {"39_acknowledgement_matrix", "accepted visible fact and all negative terminal variants", "only exact first-visible identities acknowledge", "14.4.39"},
    {"40_unchanged_frames_zero_work", "settled repeated frames", "zero recurring semantic or presentation work", "14.4.40"},
    {"41_cross_drawer_ownership", "METAR ATIS PDC switching with independent histories", "current drawer ownership remains exact", "14.4.41"},
    {"42_lifecycle_matrix", "reset new-flight callsign cold recover reconnect disable stop", "exact clear preserve and source-order behavior", "14.5.42"},
    {"43_old_provisional_lifecycle", "old provisional candidate crosses lifecycle", "cancel or stale-reject without migration", "14.5.43"},
    {"44_uninitialized_placeholder", "direct default Brain fixture", "accepted Step 3 PDC placeholder remains exact", "14.5.44"},
    {"45_plugin_initializes", "normal plugin startup ordering", "PDC initialized before first product snapshot", "14.5.45"},
    {"46_controller_ads_zero_work", "saved controller text_atis PDC advertisements", "zero private product work", "14.5.46"},
    {"47_exact_accounting", "produced queued retained consumed admission rejection terminal", "all identities reconcile without silent loss", "14.5.47"},
    {"48_private_diagnostics", "message source and decisions through serializer", "bounded metadata and no raw private text", "14.5.48"},
    {"49_fixture_off_isolation", "normal Release payload", "no fixture selector synthetic text or legacy card", "14.5.49"},
    {"50_live_rejection_raw_sequence_truth", "positive raw tuple plus fatal source evidence", "raw and Brain sequence domains remain distinct", "12.1"},
    {"51_qualification_issue_serialization", "all mechanical issue bits", "bounded stable names and masks without private text", "12.2"},
    {"52_canonical_policy_runtime_preflight_parity", "identical primitive runtime and preflight evidence", "one canonical qualification result", "12.3"},
    {"53_exact_fingerprint_version_advisory", "exact fingerprint plus four version metadata variants", "supplementary version never vetoes exact binary", "12.4"},
    {"54_wrong_fingerprint_fails_closed", "plausible version plus wrong size and hash", "fingerprint remains fatal authority", "12.5"},
    {"55_absolute_plugin_path_canonicalization", "locked xPilot absolute path", "exact canonical target hashed once", "12.6"},
    {"56_relative_plugin_path_canonicalization", "locked xPilot relative path and X-Plane root", "same canonical target identity", "12.7"},
    {"57_plugin_file_read_failure_reason", "missing unreadable and non-file target evidence", "bounded fail-closed reasons without search", "12.8"},
    {"58_dataref_capability_reason_matrix", "each missing and wrong-type private dataref", "independent fatal capability bits", "12.9"},
    {"59_source_snapshot_change_fails_closed", "source identity changes across one sample", "one coherent fail-closed observation", "12.10"},
    {"60_positive_startup_bridge_to_brain", "already-present positive tuple through policy sampler queue Brain", "one startup admission without prime suppression", "12.11"},
    {"61_qualification_event_latch_privacy", "stable failure then one qualification transition", "event-latched private-safe observability", "12.12"},
}};

int RunStep6PdcContractProbe(const std::string& scenarioName);

}  // namespace xvatsim::tools::step6_pdc_proof
