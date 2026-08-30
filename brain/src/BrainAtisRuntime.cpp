#include "XVatsim/brain/BrainOwnedRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xvatsim::brain {
namespace {

constexpr long long kAtisLookupPendingMilliseconds = 20'000;
constexpr long long kAtisLookupSpotlightMilliseconds = 8'000;
constexpr long long kAtisLookupFailureMilliseconds = 4'000;
constexpr const char* kAtisRevisionSeparator = "|#|";

std::string TrimAndUpper(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    value = value.substr(begin, end - begin + 1);
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

bool IsFourCharacterIcao(const std::string& value) {
    return value.size() == 4 && std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) != 0;
        });
}

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

std::array<std::uint8_t, 32> Sha256(const std::vector<std::uint8_t>& source) {
    static constexpr std::uint32_t roundConstants[64]{
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,
        0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
        0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,
        0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,
        0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
        0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,
        0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,
        0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
        0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
    std::uint32_t state[8]{
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    auto message = source;
    const auto bitLength = static_cast<std::uint64_t>(message.size()) * 8ULL;
    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U) message.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    }
    for (std::size_t block = 0; block < message.size(); block += 64U) {
        std::uint32_t words[64]{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto offset = block + index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(message[offset]) << 24U) |
                (static_cast<std::uint32_t>(message[offset + 1]) << 16U) |
                (static_cast<std::uint32_t>(message[offset + 2]) << 8U) |
                static_cast<std::uint32_t>(message[offset + 3]);
        }
        for (std::size_t index = 16U; index < 64U; ++index) {
            const auto small0 = RotateRight(words[index - 15U], 7U) ^
                RotateRight(words[index - 15U], 18U) ^
                (words[index - 15U] >> 3U);
            const auto small1 = RotateRight(words[index - 2U], 17U) ^
                RotateRight(words[index - 2U], 19U) ^
                (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + small0 +
                words[index - 7U] + small1;
        }
        auto a=state[0],b=state[1],c=state[2],d=state[3];
        auto e=state[4],f=state[5],g=state[6],h=state[7];
        for (std::size_t index = 0; index < 64U; ++index) {
            const auto sum1 = RotateRight(e,6U) ^ RotateRight(e,11U) ^
                RotateRight(e,25U);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choose +
                roundConstants[index] + words[index];
            const auto sum0 = RotateRight(a,2U) ^ RotateRight(a,13U) ^
                RotateRight(a,22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h=g;g=f;f=e;e=d+temporary1;d=c;c=b;b=a;
            a=temporary1+temporary2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
        state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0; index < 8U; ++index) {
        digest[index*4U]=static_cast<std::uint8_t>(state[index]>>24U);
        digest[index*4U+1U]=static_cast<std::uint8_t>(state[index]>>16U);
        digest[index*4U+2U]=static_cast<std::uint8_t>(state[index]>>8U);
        digest[index*4U+3U]=static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

std::string HexDigest(const std::vector<std::string>& lines) {
    std::vector<std::uint8_t> bytes;
    for (const auto& line : lines) {
        const auto length = static_cast<std::uint64_t>(line.size());
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(length >> shift));
        }
        bytes.insert(bytes.end(), line.begin(), line.end());
    }
    const auto digest = Sha256(bytes);
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(64);
    for (const auto value : digest) {
        hex.push_back(digits[value >> 4U]);
        hex.push_back(digits[value & 0x0fU]);
    }
    return hex;
}

std::int64_t ParseSortableTimestamp(const std::string& value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
        value[19] != 'Z') return -1;
    const int digitIndexes[]{0,1,2,3,5,6,8,9,11,12,14,15,17,18};
    std::int64_t parsed = 0;
    for (const auto index : digitIndexes) {
        const auto character = static_cast<unsigned char>(value[index]);
        if (std::isdigit(character) == 0) return -1;
        parsed = parsed * 10 + (value[index] - '0');
    }
    const auto month = std::stoi(value.substr(5, 2));
    const auto day = std::stoi(value.substr(8, 2));
    const auto hour = std::stoi(value.substr(11, 2));
    const auto minute = std::stoi(value.substr(14, 2));
    const auto second = std::stoi(value.substr(17, 2));
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 60) return -1;
    return parsed;
}

bool ParseAtisCallsign(
    const std::string& raw,
    std::string* airport,
    BrainAtisServiceRole* role,
    std::string* normalized) {
    const auto callsign = TrimAndUpper(raw);
    std::string suffix;
    BrainAtisServiceRole parsedRole = BrainAtisServiceRole::Unknown;
    if (callsign.size() == 11 && callsign.compare(4, 7, "_D_ATIS") == 0) {
        suffix = "_D_ATIS";
        parsedRole = BrainAtisServiceRole::Departure;
    } else if (callsign.size() == 11 &&
               callsign.compare(4, 7, "_A_ATIS") == 0) {
        suffix = "_A_ATIS";
        parsedRole = BrainAtisServiceRole::Arrival;
    } else if (callsign.size() == 9 &&
               callsign.compare(4, 5, "_ATIS") == 0) {
        suffix = "_ATIS";
        parsedRole = BrainAtisServiceRole::Combined;
    } else {
        return false;
    }
    (void)suffix;
    const auto parsedAirport = callsign.substr(0, 4);
    if (!IsFourCharacterIcao(parsedAirport)) return false;
    if (airport != nullptr) *airport = parsedAirport;
    if (role != nullptr) *role = parsedRole;
    if (normalized != nullptr) *normalized = callsign;
    return true;
}

std::string NormalizeFrequency(const std::string& raw) {
    const auto value = TrimAndUpper(raw);
    if (value.empty()) return {};
    if (!std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character) != 0 || character == '.';
        })) return {};
    return value;
}

std::string NormalizeInformationCode(const std::string& raw) {
    const auto code = TrimAndUpper(raw);
    if (code.size() > 8) return {};
    return code;
}

std::string RevisionRoleToken(BrainAtisServiceRole role) {
    return ToString(role);
}

BrainAtisRevision ParseRevision(const BrainRawVatsimAtisRecord& raw) {
    BrainAtisRevision revision;
    if (!raw.mechanicallyComplete) return revision;
    if (!ParseAtisCallsign(
            raw.callsign, &revision.airportIcao, &revision.serviceRole,
            &revision.normalizedCallsign)) return revision;
    revision.normalizedFrequency = NormalizeFrequency(raw.frequency);
    revision.normalizedInformationCode =
        NormalizeInformationCode(raw.informationCode);
    revision.textLines = raw.textLines;
    revision.textLines.erase(
        std::remove_if(
            revision.textLines.begin(), revision.textLines.end(),
            [](const std::string& line) { return line.empty(); }),
        revision.textLines.end());
    if (revision.textLines.empty()) return {};
    if (!raw.frequency.empty() && revision.normalizedFrequency.empty()) return {};
    if (!raw.informationCode.empty() &&
        revision.normalizedInformationCode.empty()) return {};
    if (ParseSortableTimestamp(raw.lastUpdated) >= 0) {
        revision.validSourceTime = raw.lastUpdated;
    } else if (ParseSortableTimestamp(raw.logonTime) >= 0) {
        revision.validSourceTime = raw.logonTime;
    }
    revision.revisionIdentity = revision.airportIcao + "|" +
        RevisionRoleToken(revision.serviceRole) + "|" +
        revision.normalizedCallsign + "|" + revision.normalizedFrequency +
        "|" + revision.normalizedInformationCode + "|" +
        HexDigest(revision.textLines);
    const auto sortable = ParseSortableTimestamp(raw.lastUpdated);
    std::ostringstream order;
    order << (sortable >= 0 ? '1' : '0') << '|'
          << std::setw(14) << std::setfill('0')
          << (sortable >= 0 ? sortable : 0) << '|'
          << revision.revisionIdentity;
    revision.deterministicOrderKey = order.str();
    revision.valid = true;
    return revision;
}

int AutomaticRolePriority(WorkflowStage stage, BrainAtisServiceRole role) {
    if (stage == WorkflowStage::Departure) {
        if (role == BrainAtisServiceRole::Departure) return 0;
        if (role == BrainAtisServiceRole::Combined) return 1;
        return 99;
    }
    if (stage == WorkflowStage::Enroute || stage == WorkflowStage::Arrival) {
        if (role == BrainAtisServiceRole::Arrival) return 0;
        if (role == BrainAtisServiceRole::Combined) return 1;
        return 99;
    }
    return 99;
}

BrainAtisRevision ChooseBest(
    const std::vector<BrainAtisRevision>& candidates,
    const std::function<int(BrainAtisServiceRole)>& priority) {
    BrainAtisRevision selected;
    int selectedPriority = std::numeric_limits<int>::max();
    for (const auto& candidate : candidates) {
        const auto candidatePriority = priority(candidate.serviceRole);
        if (candidatePriority >= 99) continue;
        if (!selected.valid || candidatePriority < selectedPriority ||
            (candidatePriority == selectedPriority &&
             candidate.deterministicOrderKey >
                selected.deterministicOrderKey)) {
            selected = candidate;
            selectedPriority = candidatePriority;
        }
    }
    return selected;
}

std::vector<BrainAtisRevision> ParseCandidates(
    const BrainOwnedAtisCycleInput& input,
    const std::string& airport,
    std::uint64_t* examined) {
    std::vector<BrainAtisRevision> candidates;
    if (input.atisRecords == nullptr) return candidates;
    candidates.reserve(std::min<std::size_t>(input.atisRecords->size(), 3));
    for (const auto& raw : *input.atisRecords) {
        if (examined != nullptr) ++*examined;
        auto parsed = ParseRevision(raw);
        if (parsed.valid && parsed.airportIcao == airport) {
            candidates.push_back(std::move(parsed));
        }
    }
    return candidates;
}

bool IsUnread(
    const BrainOwnedAtisRuntimeState& state,
    const std::string& identity) {
    return !identity.empty() && std::find(
        state.unreadRevisionIdentities.begin(),
        state.unreadRevisionIdentities.end(), identity) !=
            state.unreadRevisionIdentities.end();
}

bool AddUnread(BrainOwnedAtisRuntimeState* state, const std::string& identity) {
    if (state == nullptr || identity.empty() || IsUnread(*state, identity)) {
        return false;
    }
    state->unreadRevisionIdentities.push_back(identity);
    ++state->unreadCreatedCount;
    return true;
}

std::string JoinLines(const std::vector<std::string>& lines) {
    std::string joined;
    for (const auto& line : lines) {
        if (!joined.empty()) joined.push_back('\n');
        joined += line;
    }
    return joined;
}

std::string AtisTitle(const BrainAtisRevision& revision) {
    std::string title = "ATIS — " + revision.airportIcao + " — " +
        ToString(revision.serviceRole);
    if (!revision.normalizedInformationCode.empty()) {
        title += " — INFO " + revision.normalizedInformationCode;
    }
    return title;
}

std::string AtisBody(const BrainAtisRevision& revision) {
    std::string body;
    if (!revision.normalizedFrequency.empty()) {
        body += "FREQUENCY " + revision.normalizedFrequency;
    }
    if (!revision.validSourceTime.empty()) {
        if (!body.empty()) body.push_back('\n');
        body += "SOURCE TIME " + revision.validSourceTime;
    }
    if (!body.empty()) body.push_back('\n');
    body += JoinLines(revision.textLines);
    return body;
}

bool AcceptAtisHistory(
    BrainOwnedRuntimeState* state,
    const BrainAtisRevision& revision) {
    if (state == nullptr || !revision.valid) return false;
    BrainOwnedAccessoryHistoryEntryInput entry;
    entry.drawer = BrainOwnedAccessoryDrawerId::Atis;
    entry.stableKey = revision.revisionIdentity;
    entry.title = AtisTitle(revision);
    entry.body = AtisBody(revision);
    entry.chronological = true;
    const auto chronology = ParseSortableTimestamp(revision.validSourceTime);
    entry.chronologyKey = chronology >= 0 ? chronology : 0;
    const auto decision = AcceptBrainOwnedAccessoryHistoryEntry(state, entry);
    if (decision.accepted) {
        ++state->atis.historyMutationCount;
        return true;
    }
    return false;
}

std::string CurrentVisibleRevisionIdentity(
    const BrainOwnedAtisRuntimeState& state) {
    std::vector<std::string> identities;
    if (state.transientPresentation ==
            BrainAtisTransientPresentation::LookupSpotlight) {
        for (const auto& revision : state.lookupRevisions) {
            if (revision.valid) identities.push_back(revision.revisionIdentity);
        }
    } else if (state.transientPresentation ==
                   BrainAtisTransientPresentation::None &&
               state.availability == BrainAtisAvailability::Available &&
               state.primaryRevision.valid) {
        identities.push_back(state.primaryRevision.revisionIdentity);
    }
    std::string identity;
    for (const auto& part : identities) {
        if (!identity.empty()) identity += kAtisRevisionSeparator;
        identity += part;
    }
    return identity;
}

std::vector<std::string> SplitVisibleRevisionIdentity(
    const std::string& identity) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (;;) {
        const auto separator = identity.find(kAtisRevisionSeparator, begin);
        const auto part = identity.substr(
            begin, separator == std::string::npos
                ? std::string::npos : separator - begin);
        if (!part.empty()) parts.push_back(part);
        if (separator == std::string::npos) break;
        begin = separator + std::char_traits<char>::length(
            kAtisRevisionSeparator);
    }
    return parts;
}

bool OwnsAtisLookupPresentation(const BrainOwnedRuntimeState& state) {
    return state.accessory.activeDrawer == BrainOwnedAccessoryDrawerId::Atis &&
        state.atis.lookupPresentationOwnershipValid &&
        state.accessory.selectionGeneration ==
            state.atis.lookupPresentationSelectionGeneration;
}

void ResetAtisDrawerToTop(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    ++state->accessory.scrollResetGeneration;
}

void RecordAtisSemanticMutation(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    ++state->atis.semanticChangeCount;
    RecordBrainOwnedAccessoryDrawerContentMutation(
        state, BrainOwnedAccessoryDrawerId::Atis);
}

BrainAtisAvailability ResolveSourceAvailability(
    const BrainOwnedAtisCycleInput& input,
    bool hasApplicableCandidate) {
    if (!input.feedHasCache || input.feedStale || !input.atisRootPresent ||
        !input.atisRootArray) {
        return BrainAtisAvailability::SourceUnknown;
    }
    if (hasApplicableCandidate) return BrainAtisAvailability::Available;
    if (!input.atisComponentComplete || input.atisMechanicalIssueMask != 0) {
        return BrainAtisAvailability::SourceUnknown;
    }
    return BrainAtisAvailability::ConfirmedUnavailable;
}

std::string AutomaticTarget(
    const BrainOwnedAtisCycleInput& input) {
    if (!input.flightContext.active) return {};
    if (input.workflowStage == WorkflowStage::Departure) {
        const auto target = TrimAndUpper(input.flightContext.departureIcao);
        return IsFourCharacterIcao(target) ? target : std::string{};
    }
    if (input.workflowStage == WorkflowStage::Enroute ||
        input.workflowStage == WorkflowStage::Arrival) {
        const auto target = TrimAndUpper(input.flightContext.destinationIcao);
        return IsFourCharacterIcao(target) ? target : std::string{};
    }
    return {};
}

std::string BuildEvaluationKey(
    const BrainOwnedAtisCycleInput& input,
    const std::string& target,
    const BrainOwnedAtisRuntimeState& state) {
    std::ostringstream key;
    key << input.feedGeneration << '|' << input.feedHasCache << '|'
        << input.feedStale << '|' << input.atisRootPresent << '|'
        << input.atisRootArray << '|' << input.atisComponentComplete << '|'
        << input.atisMechanicalIssueMask << '|'
        << static_cast<int>(input.workflowStage) << '|' << target << '|'
        << state.lookupGeneration << '|'
        << static_cast<int>(state.transientPresentation) << '|'
        << state.pendingLookupIcao;
    return key.str();
}

std::vector<BrainAtisRevision> ChooseManualLookupRevisions(
    const std::vector<BrainAtisRevision>& candidates) {
    const auto combined = ChooseBest(
        candidates, [](BrainAtisServiceRole role) {
            return role == BrainAtisServiceRole::Combined ? 0 : 99;
        });
    if (combined.valid) return {combined};
    std::vector<BrainAtisRevision> selected;
    const auto departure = ChooseBest(
        candidates, [](BrainAtisServiceRole role) {
            return role == BrainAtisServiceRole::Departure ? 0 : 99;
        });
    const auto arrival = ChooseBest(
        candidates, [](BrainAtisServiceRole role) {
            return role == BrainAtisServiceRole::Arrival ? 0 : 99;
        });
    if (departure.valid) selected.push_back(departure);
    if (arrival.valid) selected.push_back(arrival);
    return selected;
}

}  // namespace

const char* ToString(BrainAtisServiceRole role) {
    switch (role) {
        case BrainAtisServiceRole::Departure: return "DEPARTURE";
        case BrainAtisServiceRole::Arrival: return "ARRIVAL";
        case BrainAtisServiceRole::Combined: return "COMBINED";
        case BrainAtisServiceRole::Unknown:
        default: return "UNKNOWN";
    }
}

const char* ToString(BrainAtisAvailability availability) {
    switch (availability) {
        case BrainAtisAvailability::Idle: return "IDLE";
        case BrainAtisAvailability::Available: return "AVAILABLE";
        case BrainAtisAvailability::ConfirmedUnavailable:
            return "CONFIRMED_UNAVAILABLE";
        case BrainAtisAvailability::SourceUnknown:
        default: return "SOURCE_UNKNOWN";
    }
}

const char* ToString(BrainAtisTransientPresentation presentation) {
    switch (presentation) {
        case BrainAtisTransientPresentation::None: return "NONE";
        case BrainAtisTransientPresentation::LookupPending:
            return "LOOKUP_PENDING";
        case BrainAtisTransientPresentation::LookupSpotlight:
            return "LOOKUP_SPOTLIGHT";
        case BrainAtisTransientPresentation::LookupUnavailable:
            return "LOOKUP_UNAVAILABLE";
        case BrainAtisTransientPresentation::LookupSourceUnknown:
            return "LOOKUP_SOURCE_UNKNOWN";
        default: return "UNKNOWN";
    }
}

BrainOwnedTextEntryDecision CommitBrainOwnedAtisTextEntryFact(
    BrainOwnedRuntimeState* state,
    const BrainOwnedTextEntryFact& fact) {
    BrainOwnedTextEntryDecision decision;
    if (state == nullptr ||
        fact.mode != BrainOwnedTextEntryMode::AtisAirportLookup) {
        decision.reason = "text-entry-mode-not-atis";
        return decision;
    }
    const auto airport = TrimAndUpper(fact.text);
    decision.normalizedText = airport;
    if (!IsFourCharacterIcao(airport)) {
        ++state->atis.lookupRejectedCount;
        decision.reason = "atis-lookup-invalid-icao";
        return decision;
    }
    BrainOwnedAccessoryVisibleInvalidationBatch invalidation(state);
    state->atis.pendingLookupIcao = airport;
    ++state->atis.lookupGeneration;
    if (state->atis.lookupGeneration == 0) state->atis.lookupGeneration = 1;
    state->atis.transientPresentation =
        BrainAtisTransientPresentation::LookupPending;
    state->atis.transientDeadlineMonotonicMs =
        fact.monotonicMs + kAtisLookupPendingMilliseconds;
    state->atis.lookupRevisions.clear();
    if (state->accessory.activeDrawer != BrainOwnedAccessoryDrawerId::Atis) {
        state->accessory.activeDrawer = BrainOwnedAccessoryDrawerId::Atis;
        ++state->accessory.selectionGeneration;
        RecordBrainOwnedAccessorySelectionMutation(state);
    }
    state->atis.lookupPresentationSelectionGeneration =
        state->accessory.selectionGeneration;
    state->atis.lookupPresentationOwnershipValid = true;
    ResetAtisDrawerToTop(state);
    RecordAtisSemanticMutation(state);
    ++state->atis.lookupAcceptedCount;
    decision.accepted = true;
    decision.presentationChanged = true;
    decision.reason = "atis-lookup-pending";
    return decision;
}

BrainOwnedAtisCycleDecision RunBrainOwnedAtisCycle(
    BrainOwnedRuntimeState* state,
    const BrainOwnedAtisCycleInput& input) {
    BrainOwnedAtisCycleDecision decision;
    if (state == nullptr) {
        decision.reason = "atis-state-unavailable";
        return decision;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto finish = [&]() {
        decision.evaluationMicroseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        state->atis.maximumEvaluationMicroseconds = std::max(
            state->atis.maximumEvaluationMicroseconds,
            decision.evaluationMicroseconds);
        decision.availability = state->atis.availability;
        decision.feedGeneration = input.feedGeneration;
        return decision;
    };

    const auto automaticTarget = AutomaticTarget(input);
    decision.targetAirportIcao = automaticTarget;
    const bool transientActive = state->atis.transientPresentation !=
        BrainAtisTransientPresentation::None;
    if (transientActive && !OwnsAtisLookupPresentation(*state)) {
        state->atis.lookupPresentationOwnershipValid = false;
        state->atis.transientPresentation = BrainAtisTransientPresentation::None;
        state->atis.pendingLookupIcao.clear();
        state->atis.lookupRevisions.clear();
        decision.ownershipLost = true;
        decision.reason = "atis-lookup-presentation-ownership-lost";
    }
    if (state->atis.transientPresentation !=
            BrainAtisTransientPresentation::None &&
        state->atis.transientPresentation !=
            BrainAtisTransientPresentation::LookupPending &&
        input.monotonicMs >= state->atis.transientDeadlineMonotonicMs) {
        BrainOwnedAccessoryVisibleInvalidationBatch invalidation(state);
        state->atis.transientPresentation = BrainAtisTransientPresentation::None;
        state->atis.pendingLookupIcao.clear();
        state->atis.lookupRevisions.clear();
        state->atis.lookupPresentationOwnershipValid = false;
        ResetAtisDrawerToTop(state);
        RecordAtisSemanticMutation(state);
        decision.lookupExpired = true;
        decision.semanticChanged = true;
        decision.reason = "atis-lookup-presentation-expired";
    }

    if (!input.pluginEnabled || !input.xpilotConnected) {
        if (state->atis.availability != BrainAtisAvailability::SourceUnknown &&
            (state->atis.primaryRevision.valid || state->atis.initialized)) {
            BrainOwnedAccessoryVisibleInvalidationBatch invalidation(state);
            state->atis.availability = BrainAtisAvailability::SourceUnknown;
            ++state->atis.sourceUnknownCount;
            RecordAtisSemanticMutation(state);
            decision.semanticChanged = true;
        }
        decision.reason = input.pluginEnabled
            ? "atis-xpilot-disconnected" : "atis-plugin-disabled";
        return finish();
    }

    const auto evaluationKey = BuildEvaluationKey(
        input, automaticTarget, state->atis);
    const bool pendingLookup = state->atis.transientPresentation ==
        BrainAtisTransientPresentation::LookupPending;
    const bool pendingDeadlineReached = pendingLookup &&
        input.monotonicMs >= state->atis.transientDeadlineMonotonicMs;
    if (!pendingLookup && evaluationKey == state->atis.lastEvaluationKey) {
        if (decision.reason.empty()) decision.reason = "atis-input-unchanged";
        return finish();
    }
    if (pendingLookup && !pendingDeadlineReached &&
        evaluationKey == state->atis.lastEvaluationKey) {
        decision.reason = "atis-lookup-awaiting-normal-feed-edge";
        return finish();
    }

    decision.evaluated = true;
    ++state->atis.evaluationCount;
    state->atis.lastEvaluationKey = evaluationKey;
    state->atis.lastFeedGeneration = input.feedGeneration;
    state->atis.lastFeedHasCache = input.feedHasCache;
    state->atis.lastFeedStale = input.feedStale;
    state->atis.lastFeedRootPresent = input.atisRootPresent;
    state->atis.lastFeedRootArray = input.atisRootArray;
    state->atis.lastFeedComponentComplete = input.atisComponentComplete;
    state->atis.lastFeedMechanicalIssueMask = input.atisMechanicalIssueMask;
    state->atis.initialized = true;

    BrainOwnedAccessoryVisibleInvalidationBatch invalidation(state);
    std::uint64_t examined = 0;
    std::vector<BrainAtisRevision> automaticCandidates;
    if (!automaticTarget.empty()) {
        automaticCandidates = ParseCandidates(input, automaticTarget, &examined);
    }
    const auto selected = ChooseBest(
        automaticCandidates, [&](BrainAtisServiceRole role) {
            return AutomaticRolePriority(input.workflowStage, role);
        });
    const auto resolvedAvailability = automaticTarget.empty()
        ? BrainAtisAvailability::Idle
        : ResolveSourceAvailability(input, selected.valid);
    const auto authoritativeSelected =
        resolvedAvailability == BrainAtisAvailability::Available
        ? selected : BrainAtisRevision{};
    const bool targetChanged =
        state->atis.automaticAirportIcao != automaticTarget ||
        state->atis.workflowStage != input.workflowStage;
    const bool availabilityChanged =
        state->atis.availability != resolvedAvailability;
    const bool revisionChanged =
        authoritativeSelected.valid
        ? (!state->atis.primaryRevision.valid ||
           authoritativeSelected.revisionIdentity !=
                state->atis.primaryRevision.revisionIdentity)
        : (resolvedAvailability != BrainAtisAvailability::SourceUnknown &&
           state->atis.primaryRevision.valid);
    if (targetChanged || availabilityChanged || revisionChanged) {
        state->atis.automaticAirportIcao = automaticTarget;
        state->atis.availability = resolvedAvailability;
        state->atis.workflowStage = input.workflowStage;
        if (authoritativeSelected.valid && revisionChanged) {
            state->atis.primaryRevision = authoritativeSelected;
            decision.historyMutated = AcceptAtisHistory(
                state, authoritativeSelected);
            decision.unreadCreated = AddUnread(
                &state->atis, authoritativeSelected.revisionIdentity);
            decision.selectedRevisionIdentity =
                authoritativeSelected.revisionIdentity;
            if (state->accessory.activeDrawer ==
                BrainOwnedAccessoryDrawerId::Atis &&
                state->atis.transientPresentation ==
                    BrainAtisTransientPresentation::None) {
                ResetAtisDrawerToTop(state);
            }
        } else if (!authoritativeSelected.valid &&
                   resolvedAvailability != BrainAtisAvailability::SourceUnknown) {
            state->atis.primaryRevision = {};
        }
        if (resolvedAvailability == BrainAtisAvailability::SourceUnknown) {
            ++state->atis.sourceUnknownCount;
        } else if (resolvedAvailability ==
                       BrainAtisAvailability::ConfirmedUnavailable) {
            ++state->atis.confirmedUnavailableCount;
        }
        RecordAtisSemanticMutation(state);
        decision.semanticChanged = true;
    }

    if (pendingLookup && OwnsAtisLookupPresentation(*state)) {
        auto lookupCandidates = ParseCandidates(
            input, state->atis.pendingLookupIcao, &examined);
        auto lookupRevisions = ChooseManualLookupRevisions(lookupCandidates);
        const auto lookupAvailability = ResolveSourceAvailability(
            input, !lookupRevisions.empty());
        if (lookupAvailability != BrainAtisAvailability::Available) {
            lookupRevisions.clear();
        }
        bool completeLookup = !lookupRevisions.empty() ||
            lookupAvailability == BrainAtisAvailability::ConfirmedUnavailable ||
            (!input.feedFetchInProgress &&
             lookupAvailability == BrainAtisAvailability::SourceUnknown) ||
            pendingDeadlineReached;
        if (completeLookup) {
            state->atis.lookupRevisions = lookupRevisions;
            state->atis.transientPresentation = !lookupRevisions.empty()
                ? BrainAtisTransientPresentation::LookupSpotlight
                : lookupAvailability ==
                    BrainAtisAvailability::ConfirmedUnavailable
                    ? BrainAtisTransientPresentation::LookupUnavailable
                    : BrainAtisTransientPresentation::LookupSourceUnknown;
            state->atis.transientDeadlineMonotonicMs = input.monotonicMs +
                (!lookupRevisions.empty()
                    ? kAtisLookupSpotlightMilliseconds
                    : kAtisLookupFailureMilliseconds);
            for (const auto& revision : lookupRevisions) {
                decision.historyMutated =
                    AcceptAtisHistory(state, revision) ||
                    decision.historyMutated;
                decision.unreadCreated =
                    AddUnread(&state->atis, revision.revisionIdentity) ||
                    decision.unreadCreated;
            }
            ResetAtisDrawerToTop(state);
            RecordAtisSemanticMutation(state);
            decision.lookupCompleted = true;
            decision.semanticChanged = true;
            decision.reason = !lookupRevisions.empty()
                ? "atis-lookup-accepted"
                : state->atis.transientPresentation ==
                    BrainAtisTransientPresentation::LookupUnavailable
                    ? "atis-lookup-confirmed-unavailable"
                    : "atis-lookup-source-unknown";
        }
    }
    decision.examinedRecords = examined;
    decision.candidates = automaticCandidates.size();
    state->atis.examinedRecordCount += examined;
    state->atis.candidateCount += automaticCandidates.size();
    if (decision.reason.empty()) {
        decision.reason = decision.semanticChanged
            ? "atis-semantic-state-changed" : "atis-semantic-no-op";
    }
    return finish();
}

void ProjectBrainOwnedAtisOrbPresentation(
    const BrainOwnedRuntimeState& state,
    BrainOwnedAccessoryOrbPresentation* orb) {
    if (orb == nullptr) return;
    orb->label = "ATIS";
    orb->neutral = true;
    orb->airportIcao.clear();
    orb->categoryText.clear();
    orb->stateText = ToString(state.atis.availability);
    orb->tone = BrainOwnedAccessoryOrbPresentation::Tone::Gray;
    if (state.atis.availability != BrainAtisAvailability::Available ||
        !state.atis.primaryRevision.valid) return;
    orb->neutral = false;
    orb->airportIcao = state.atis.primaryRevision.airportIcao;
    const bool unread = IsUnread(
        state.atis, state.atis.primaryRevision.revisionIdentity);
    if (state.atis.primaryRevision.normalizedInformationCode.empty()) {
        orb->categoryText = unread ? "NEW" : "ATIS";
    } else {
        orb->categoryText = std::string(unread ? "NEW " : "INFO ") +
            state.atis.primaryRevision.normalizedInformationCode;
    }
    orb->tone = unread
        ? BrainOwnedAccessoryOrbPresentation::Tone::Amber
        : BrainOwnedAccessoryOrbPresentation::Tone::Cyan;
}

std::vector<BrainOwnedAccessoryHistoryEntry>
ProjectBrainOwnedAtisDrawerPresentation(
    const BrainOwnedRuntimeState& state,
    const BrainOwnedAccessoryHistory& history,
    BrainOwnedAccessoryDrawerState* drawerState,
    std::string* drawerTitle,
    std::string* drawerStateText,
    std::string* emptyStateText,
    std::string* visibleRevisionIdentity) {
    std::vector<BrainOwnedAccessoryHistoryEntry> entries;
    std::vector<BrainAtisRevision> current;
    if (drawerTitle != nullptr) *drawerTitle = "ATIS";
    if (drawerStateText != nullptr) drawerStateText->clear();
    if (emptyStateText != nullptr) emptyStateText->clear();
    if (visibleRevisionIdentity != nullptr) {
        *visibleRevisionIdentity = CurrentVisibleRevisionIdentity(state.atis);
    }
    switch (state.atis.transientPresentation) {
        case BrainAtisTransientPresentation::LookupPending:
            if (drawerState != nullptr) {
                *drawerState = BrainOwnedAccessoryDrawerState::Loading;
            }
            if (drawerTitle != nullptr) {
                *drawerTitle = "ATIS LOOKUP — " +
                    state.atis.pendingLookupIcao;
            }
            if (drawerStateText != nullptr) {
                *drawerStateText = "WAITING FOR CURRENT VATSIM FEED — " +
                    state.atis.pendingLookupIcao;
            }
            return entries;
        case BrainAtisTransientPresentation::LookupSpotlight:
            current = state.atis.lookupRevisions;
            if (drawerTitle != nullptr) {
                *drawerTitle = "ATIS LOOKUP — " +
                    state.atis.pendingLookupIcao;
            }
            break;
        case BrainAtisTransientPresentation::LookupUnavailable:
            if (drawerState != nullptr) {
                *drawerState = BrainOwnedAccessoryDrawerState::Empty;
            }
            if (drawerTitle != nullptr) {
                *drawerTitle = "ATIS LOOKUP — " +
                    state.atis.pendingLookupIcao;
            }
            if (emptyStateText != nullptr) {
                *emptyStateText = "NO APPLICABLE ATIS IN CURRENT VATSIM FEED";
            }
            return entries;
        case BrainAtisTransientPresentation::LookupSourceUnknown:
            if (drawerState != nullptr) {
                *drawerState = BrainOwnedAccessoryDrawerState::Unavailable;
            }
            if (drawerTitle != nullptr) {
                *drawerTitle = "ATIS LOOKUP — " +
                    state.atis.pendingLookupIcao;
            }
            if (drawerStateText != nullptr) {
                *drawerStateText = "VATSIM ATIS SOURCE STATUS UNKNOWN";
            }
            return entries;
        case BrainAtisTransientPresentation::None:
        default:
            if (state.atis.availability == BrainAtisAvailability::Available &&
                state.atis.primaryRevision.valid) {
                current.push_back(state.atis.primaryRevision);
                if (drawerTitle != nullptr) {
                    *drawerTitle = "ATIS — " +
                        state.atis.primaryRevision.airportIcao + " — PRIMARY";
                }
            } else if (state.atis.availability ==
                       BrainAtisAvailability::ConfirmedUnavailable) {
                if (drawerState != nullptr) {
                    *drawerState = BrainOwnedAccessoryDrawerState::Empty;
                }
                if (emptyStateText != nullptr) {
                    *emptyStateText = "NO APPLICABLE ATIS IN CURRENT VATSIM FEED";
                }
                return entries;
            } else if (state.atis.availability ==
                       BrainAtisAvailability::SourceUnknown) {
                if (drawerState != nullptr) {
                    *drawerState = BrainOwnedAccessoryDrawerState::Unavailable;
                }
                if (drawerStateText != nullptr) {
                    *drawerStateText = "VATSIM ATIS SOURCE STATUS UNKNOWN";
                }
                return entries;
            } else {
                if (drawerState != nullptr) {
                    *drawerState = BrainOwnedAccessoryDrawerState::Empty;
                }
                if (emptyStateText != nullptr) {
                    *emptyStateText = "ATIS IDLE — NO ACCEPTED FLIGHT ENDPOINT";
                }
                return entries;
            }
            break;
    }
    entries.reserve(current.size() + history.entries.size());
    for (const auto& revision : current) {
        BrainOwnedAccessoryHistoryEntry entry;
        entry.stableKey = "CURRENT|" + revision.revisionIdentity;
        entry.title = AtisTitle(revision);
        entry.body = AtisBody(revision);
        entry.retainedBytes = entry.stableKey.size() + entry.title.size() +
            entry.body.size();
        entries.push_back(std::move(entry));
    }
    entries.insert(entries.end(), history.entries.begin(), history.entries.end());
    if (drawerState != nullptr) {
        *drawerState = entries.empty()
            ? BrainOwnedAccessoryDrawerState::Empty
            : BrainOwnedAccessoryDrawerState::Ready;
    }
    return entries;
}

bool AcknowledgeBrainOwnedAtisVisibleRevision(
    BrainOwnedRuntimeState* state,
    const std::string& visibleRevisionIdentity) {
    if (state == nullptr || visibleRevisionIdentity.empty() ||
        state->accessory.activeDrawer != BrainOwnedAccessoryDrawerId::Atis ||
        CurrentVisibleRevisionIdentity(state->atis) !=
            visibleRevisionIdentity) return false;
    const auto identities = SplitVisibleRevisionIdentity(visibleRevisionIdentity);
    std::size_t removed = 0;
    BrainOwnedAccessoryVisibleInvalidationBatch invalidation(state);
    for (const auto& identity : identities) {
        const auto iterator = std::find(
            state->atis.unreadRevisionIdentities.begin(),
            state->atis.unreadRevisionIdentities.end(), identity);
        if (iterator != state->atis.unreadRevisionIdentities.end()) {
            state->atis.unreadRevisionIdentities.erase(iterator);
            ++removed;
        }
    }
    if (removed == 0) return false;
    state->atis.unreadAcknowledgedCount += removed;
    RecordAtisSemanticMutation(state);
    return true;
}

void ResetBrainOwnedAtisProductState(BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    state->atis = {};
}

void MarkBrainOwnedAtisSourceUnknownPreservingAcceptedState(
    BrainOwnedRuntimeState* state) {
    if (state == nullptr) return;
    BrainOwnedAccessoryVisibleInvalidationBatch invalidation(state);
    state->atis.transientPresentation = BrainAtisTransientPresentation::None;
    state->atis.pendingLookupIcao.clear();
    state->atis.lookupRevisions.clear();
    state->atis.lookupPresentationOwnershipValid = false;
    state->atis.lastEvaluationKey.clear();
    if (state->atis.availability != BrainAtisAvailability::SourceUnknown) {
        state->atis.availability = BrainAtisAvailability::SourceUnknown;
        ++state->atis.sourceUnknownCount;
        RecordAtisSemanticMutation(state);
    }
}

}  // namespace xvatsim::brain
