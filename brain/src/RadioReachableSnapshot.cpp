#include "XVatsim/brain/RadioReachableSnapshot.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace xvatsim::brain {

namespace {

constexpr int kVatsimFlightServiceFacility = 1;
constexpr int kVatsimDeliveryFacility = 2;
constexpr int kVatsimGroundFacility = 3;
constexpr int kVatsimTowerFacility = 4;
constexpr int kVatsimApproachFacility = 5;
constexpr int kVatsimCenterFacility = 6;

std::string Trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() &&
           std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin &&
           std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }

    return std::string(begin, end);
}

std::string ToUpper(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

std::string NormalizeFrequency(std::string frequency) {
    frequency = Trim(std::move(frequency));
    std::string normalized;
    normalized.reserve(frequency.size());
    for (const auto character : frequency) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
            normalized.push_back(character);
        }
    }
    return normalized;
}

void HashCombine(std::uint64_t* seed, std::uint64_t value) {
    if (seed == nullptr) {
        return;
    }
    *seed ^= value + 0x9e3779b97f4a7c15ull + (*seed << 6) + (*seed >> 2);
}

void HashCombine(std::uint64_t* seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(value.size()));
    for (const auto character : value) {
        HashCombine(
            seed,
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(character)));
    }
}

void HashCombine(std::uint64_t* seed, bool value) {
    HashCombine(seed, static_cast<std::uint64_t>(value ? 1u : 0u));
}

std::string BuildStableKey(
    const std::string& callsign,
    const std::string& frequency,
    RadioReachableFacilityGroup group) {
    std::ostringstream stream;
    stream << callsign << "|" << frequency << "|" << ToString(group);
    return stream.str();
}

void IncrementGroupCount(
    RadioReachableGroupCounts* counts,
    RadioReachableFacilityGroup group) {
    if (counts == nullptr) {
        return;
    }

    switch (group) {
        case RadioReachableFacilityGroup::Delivery:
            ++counts->delivery;
            break;
        case RadioReachableFacilityGroup::Ground:
            ++counts->ground;
            break;
        case RadioReachableFacilityGroup::Tower:
            ++counts->tower;
            break;
        case RadioReachableFacilityGroup::AppDep:
            ++counts->appDep;
            break;
        case RadioReachableFacilityGroup::Center:
            ++counts->center;
            break;
        case RadioReachableFacilityGroup::Atis:
            ++counts->atis;
            break;
        case RadioReachableFacilityGroup::Other:
            ++counts->other;
            break;
    }
}

std::uint64_t BuildStableHash(
    const RadioReachableControllerSnapshot& snapshot) {
    std::uint64_t hash = 1469598103934665603ull;
    HashCombine(&hash, snapshot.available);
    HashCombine(&hash, snapshot.stale);
    HashCombine(&hash, static_cast<std::uint64_t>(snapshot.source));
    HashCombine(&hash, static_cast<std::uint64_t>(snapshot.candidates.size()));
    for (const auto& candidate : snapshot.candidates) {
        HashCombine(&hash, candidate.stableKey);
        HashCombine(&hash, static_cast<std::uint64_t>(candidate.vatsimFacility));
        HashCombine(&hash, candidate.actionable);
        HashCombine(&hash, candidate.atis);
        HashCombine(&hash, static_cast<std::uint64_t>(candidate.visualRangeNm));
    }
    return hash;
}

std::string HashToHex(std::uint64_t hash) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << hash;
    return stream.str();
}

const char* ToStageToken(WorkflowStage stage) {
    switch (stage) {
        case WorkflowStage::Departure:
            return "DEPARTURE";
        case WorkflowStage::Enroute:
            return "ENROUTE";
        case WorkflowStage::Arrival:
            return "ARRIVAL";
        case WorkflowStage::None:
            return "NONE";
    }
    return "NONE";
}

std::unordered_map<std::string, ControllerSnapshot> IndexControllersByCallsign(
    const ControllerFeedSnapshot& controllerFeedSnapshot) {
    std::unordered_map<std::string, ControllerSnapshot> indexed;
    if (!controllerFeedSnapshot.available || controllerFeedSnapshot.stale) {
        return indexed;
    }

    for (const auto& controller : controllerFeedSnapshot.Controllers()) {
        const auto callsign = ToUpper(Trim(controller.callsign));
        if (!callsign.empty()) {
            indexed.emplace(callsign, controller);
        }
    }
    return indexed;
}

void SortRadioReachableCandidates(RadioReachableControllerSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return;
    }

    std::stable_sort(
        snapshot->candidates.begin(),
        snapshot->candidates.end(),
        [](const RadioReachableControllerCandidate& left,
           const RadioReachableControllerCandidate& right) {
            if (left.group != right.group) {
                return static_cast<int>(left.group) < static_cast<int>(right.group);
            }
            if (left.callsign != right.callsign) {
                return left.callsign < right.callsign;
            }
            return left.frequency < right.frequency;
        });
}

}  // namespace

const char* ToString(RadioReachableFacilityGroup group) {
    switch (group) {
        case RadioReachableFacilityGroup::Delivery:
            return "DEL";
        case RadioReachableFacilityGroup::Ground:
            return "GND";
        case RadioReachableFacilityGroup::Tower:
            return "TWR";
        case RadioReachableFacilityGroup::AppDep:
            return "APP_DEP";
        case RadioReachableFacilityGroup::Center:
            return "CTR";
        case RadioReachableFacilityGroup::Atis:
            return "ATIS";
        case RadioReachableFacilityGroup::Other:
            return "OTHER";
    }
    return "OTHER";
}

const char* ToString(RadioReachableSource source) {
    switch (source) {
        case RadioReachableSource::XPilotSeam:
            return "XPILOT_SEAM";
        case RadioReachableSource::AFVRadioRange:
            return "AFV_RADIO_RANGE";
        case RadioReachableSource::ControllerFeed:
            return "CONTROLLER_FEED";
        case RadioReachableSource::TestHarness:
            return "TEST_HARNESS";
        case RadioReachableSource::Unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

RadioReachableFacilityGroup ClassifyRadioReachableFacility(
    const ControllerSnapshot& controller) {
    const auto callsign = ToUpper(Trim(controller.callsign));
    if (controller.atis || callsign.find("_ATIS") != std::string::npos) {
        return RadioReachableFacilityGroup::Atis;
    }

    switch (controller.facility) {
        case kVatsimDeliveryFacility:
            return RadioReachableFacilityGroup::Delivery;
        case kVatsimGroundFacility:
            return RadioReachableFacilityGroup::Ground;
        case kVatsimTowerFacility:
            return RadioReachableFacilityGroup::Tower;
        case kVatsimApproachFacility:
            return RadioReachableFacilityGroup::AppDep;
        case kVatsimCenterFacility:
        case kVatsimFlightServiceFacility:
            return RadioReachableFacilityGroup::Center;
        default:
            break;
    }

    if (callsign.find("_CTR") != std::string::npos ||
        callsign.find("_FSS") != std::string::npos) {
        return RadioReachableFacilityGroup::Center;
    }
    if (callsign.find("_APP") != std::string::npos ||
        callsign.find("_DEP") != std::string::npos) {
        return RadioReachableFacilityGroup::AppDep;
    }
    if (callsign.find("_TWR") != std::string::npos) {
        return RadioReachableFacilityGroup::Tower;
    }
    if (callsign.find("_GND") != std::string::npos) {
        return RadioReachableFacilityGroup::Ground;
    }
    if (callsign.find("_DEL") != std::string::npos) {
        return RadioReachableFacilityGroup::Delivery;
    }

    return RadioReachableFacilityGroup::Other;
}

RadioReachableControllerSnapshot BuildRadioReachableControllerSnapshot(
    const std::vector<ControllerSnapshot>& reachableControllers,
    const RadioReachableBuildOptions& options) {
    RadioReachableControllerSnapshot snapshot;
    snapshot.available = options.available;
    snapshot.stale = options.stale;
    snapshot.generation = options.generation;
    snapshot.source = options.source;
    snapshot.changeReason = options.changeReason;

    if (!snapshot.available || snapshot.stale) {
        std::ostringstream stream;
        stream << "RADIO_RANGE available=" << (snapshot.available ? 1 : 0)
               << " stale=" << (snapshot.stale ? 1 : 0)
               << " source=" << ToString(snapshot.source)
               << " candidates=0";
        snapshot.statusLine = stream.str();
        snapshot.stableHash = BuildStableHash(snapshot);
        return snapshot;
    }

    snapshot.candidates.reserve(reachableControllers.size());
    for (const auto& controller : reachableControllers) {
        RadioReachableControllerCandidate candidate;
        candidate.callsign = ToUpper(Trim(controller.callsign));
        candidate.frequency = NormalizeFrequency(controller.frequency);
        candidate.vatsimFacility = controller.facility;
        candidate.group = ClassifyRadioReachableFacility(controller);
        candidate.source = snapshot.source;
        candidate.actionable = controller.actionable;
        candidate.atis = controller.atis;
        candidate.visualRangeNm = controller.visualRangeNm;
        candidate.firstSeenSeconds = options.nowSeconds;
        candidate.lastSeenSeconds = options.nowSeconds;
        candidate.stableKey = BuildStableKey(
            candidate.callsign,
            candidate.frequency,
            candidate.group);
        IncrementGroupCount(&snapshot.counts, candidate.group);
        snapshot.candidates.push_back(std::move(candidate));
    }

    SortRadioReachableCandidates(&snapshot);

    snapshot.stableHash = BuildStableHash(snapshot);

    std::ostringstream stream;
    stream << "RADIO_RANGE available=1 stale=0"
           << " source=" << ToString(snapshot.source)
           << " candidates=" << snapshot.candidates.size()
           << " hash=" << HashToHex(snapshot.stableHash)
           << " " << RadioReachableGroupCountSummary(snapshot);
    if (!snapshot.changeReason.empty()) {
        stream << " reason=" << snapshot.changeReason;
    }
    snapshot.statusLine = stream.str();
    return snapshot;
}

RadioReachableControllerSnapshot BuildRadioReachableControllerSnapshotFromTransceivers(
    const TransceiverResolutionSnapshot& transceiverSnapshot,
    const ControllerFeedSnapshot& controllerFeedSnapshot,
    const RadioReachableBuildOptions& options) {
    RadioReachableControllerSnapshot snapshot;
    snapshot.source = options.source == RadioReachableSource::Unknown
                          ? RadioReachableSource::AFVRadioRange
                          : options.source;
    snapshot.generation = options.generation;
    snapshot.changeReason = options.changeReason.empty()
                                ? "transceiver-resolution"
                                : options.changeReason;

    const auto sourceAvailable =
        transceiverSnapshot.available && controllerFeedSnapshot.available;
    snapshot.available = sourceAvailable;
    snapshot.stale =
        transceiverSnapshot.stale || controllerFeedSnapshot.stale || !sourceAvailable;

    if (!snapshot.available || snapshot.stale) {
        std::ostringstream stream;
        stream << "RADIO_RANGE available=" << (snapshot.available ? 1 : 0)
               << " stale=" << (snapshot.stale ? 1 : 0)
               << " source=" << ToString(snapshot.source)
               << " candidates=0";
        if (!transceiverSnapshot.statusLine.empty()) {
            stream << " transceiver=\"" << transceiverSnapshot.statusLine << "\"";
        }
        snapshot.statusLine = stream.str();
        snapshot.stableHash = BuildStableHash(snapshot);
        return snapshot;
    }

    const auto controllersByCallsign = IndexControllersByCallsign(controllerFeedSnapshot);
    int unmatchedCandidates = 0;
    snapshot.candidates.reserve(transceiverSnapshot.candidates.size());
    for (const auto& receivable : transceiverSnapshot.candidates) {
        const auto callsign = ToUpper(Trim(receivable.callsign));
        if (callsign.empty()) {
            continue;
        }

        const auto controllerEntry = controllersByCallsign.find(callsign);
        if (controllerEntry == controllersByCallsign.end()) {
            ++unmatchedCandidates;
            continue;
        }

        const auto& controller = controllerEntry->second;
        RadioReachableControllerCandidate candidate;
        candidate.callsign = callsign;
        candidate.frequency = NormalizeFrequency(
            receivable.frequency.empty() ? controller.frequency : receivable.frequency);
        candidate.vatsimFacility = controller.facility;
        candidate.group = ClassifyRadioReachableFacility(controller);
        candidate.source = snapshot.source;
        candidate.actionable = controller.actionable;
        candidate.atis = controller.atis;
        candidate.visualRangeNm = controller.visualRangeNm;
        candidate.hasDistanceNm = std::isfinite(receivable.distanceNm);
        candidate.distanceNm = candidate.hasDistanceNm ? receivable.distanceNm : 0.0;
        candidate.firstSeenSeconds = options.nowSeconds;
        candidate.lastSeenSeconds = options.nowSeconds;
        candidate.stableKey = BuildStableKey(
            candidate.callsign,
            candidate.frequency,
            candidate.group);
        IncrementGroupCount(&snapshot.counts, candidate.group);
        snapshot.candidates.push_back(std::move(candidate));
    }

    SortRadioReachableCandidates(&snapshot);
    snapshot.stableHash = BuildStableHash(snapshot);

    std::ostringstream stream;
    stream << "RADIO_RANGE available=1 stale=0"
           << " source=" << ToString(snapshot.source)
           << " candidates=" << snapshot.candidates.size()
           << " unmatched=" << unmatchedCandidates
           << " hash=" << HashToHex(snapshot.stableHash)
           << " " << RadioReachableGroupCountSummary(snapshot);
    if (!snapshot.changeReason.empty()) {
        stream << " reason=" << snapshot.changeReason;
    }
    snapshot.statusLine = stream.str();
    return snapshot;
}

bool RadioReachableGroupAllowedForStage(
    RadioReachableFacilityGroup group,
    const RadioReachablePhaseGateOptions& options) {
    if (group == RadioReachableFacilityGroup::Atis && !options.includeAtis) {
        return false;
    }

    switch (options.stage) {
        case WorkflowStage::Departure:
            return group == RadioReachableFacilityGroup::Delivery ||
                   group == RadioReachableFacilityGroup::Ground ||
                   group == RadioReachableFacilityGroup::Tower ||
                   group == RadioReachableFacilityGroup::AppDep ||
                   group == RadioReachableFacilityGroup::Center ||
                   (group == RadioReachableFacilityGroup::Atis && options.includeAtis);
        case WorkflowStage::Enroute:
            return group == RadioReachableFacilityGroup::Center;
        case WorkflowStage::Arrival:
            return group == RadioReachableFacilityGroup::Delivery ||
                   group == RadioReachableFacilityGroup::Ground ||
                   group == RadioReachableFacilityGroup::Tower ||
                   group == RadioReachableFacilityGroup::AppDep ||
                   group == RadioReachableFacilityGroup::Center ||
                   (group == RadioReachableFacilityGroup::Atis && options.includeAtis);
        case WorkflowStage::None:
            return false;
    }
    return false;
}

RadioReachableControllerSnapshot ApplyRadioReachablePhaseGate(
    const RadioReachableControllerSnapshot& snapshot,
    const RadioReachablePhaseGateOptions& options) {
    RadioReachableControllerSnapshot gated;
    gated.available = snapshot.available;
    gated.stale = snapshot.stale;
    gated.generation = snapshot.generation;
    gated.source = snapshot.source;
    gated.changeReason =
        options.reason.empty() ? "phase-gate" : options.reason;

    int rejected = 0;
    if (snapshot.available && !snapshot.stale) {
        gated.candidates.reserve(snapshot.candidates.size());
        for (const auto& candidate : snapshot.candidates) {
            if (!RadioReachableGroupAllowedForStage(candidate.group, options)) {
                ++rejected;
                continue;
            }
            IncrementGroupCount(&gated.counts, candidate.group);
            gated.candidates.push_back(candidate);
        }
        SortRadioReachableCandidates(&gated);
    } else {
        rejected = static_cast<int>(snapshot.candidates.size());
    }

    gated.stableHash = BuildStableHash(gated);

    std::ostringstream stream;
    stream << "RADIO_RANGE_GATE stage=" << ToStageToken(options.stage)
           << " available=" << (gated.available ? 1 : 0)
           << " stale=" << (gated.stale ? 1 : 0)
           << " source=" << ToString(gated.source)
           << " candidates=" << gated.candidates.size()
           << " rejected=" << rejected
           << " hash=" << HashToHex(gated.stableHash)
           << " " << RadioReachableGroupCountSummary(gated);
    if (!gated.changeReason.empty()) {
        stream << " reason=" << gated.changeReason;
    }
    gated.statusLine = stream.str();
    return gated;
}

RadioReachableCandidateDiff DiffRadioReachableSnapshots(
    const RadioReachableControllerSnapshot& previous,
    const RadioReachableControllerSnapshot& current) {
    RadioReachableCandidateDiff diff;
    diff.available = current.available;
    diff.stale = current.stale;
    diff.previousHash = previous.stableHash;
    diff.currentHash = current.stableHash;
    diff.previousCandidates = static_cast<int>(previous.candidates.size());
    diff.currentCandidates = static_cast<int>(current.candidates.size());

    if (!current.available || current.stale) {
        std::ostringstream stream;
        stream << "RADIO_RANGE_DIFF available=" << (diff.available ? 1 : 0)
               << " stale=" << (diff.stale ? 1 : 0)
               << " added=0 removed=0 unchanged=0";
        diff.statusLine = stream.str();
        return diff;
    }

    std::unordered_set<std::string> previousKeys;
    previousKeys.reserve(previous.candidates.size());
    for (const auto& candidate : previous.candidates) {
        previousKeys.insert(candidate.stableKey);
    }

    std::unordered_set<std::string> currentKeys;
    currentKeys.reserve(current.candidates.size());
    for (const auto& candidate : current.candidates) {
        currentKeys.insert(candidate.stableKey);
        if (previousKeys.find(candidate.stableKey) == previousKeys.end()) {
            diff.addedStableKeys.push_back(candidate.stableKey);
        } else {
            ++diff.unchanged;
        }
    }

    for (const auto& candidate : previous.candidates) {
        if (currentKeys.find(candidate.stableKey) == currentKeys.end()) {
            diff.removedStableKeys.push_back(candidate.stableKey);
        }
    }

    std::sort(diff.addedStableKeys.begin(), diff.addedStableKeys.end());
    std::sort(diff.removedStableKeys.begin(), diff.removedStableKeys.end());
    diff.added = static_cast<int>(diff.addedStableKeys.size());
    diff.removed = static_cast<int>(diff.removedStableKeys.size());

    std::ostringstream stream;
    stream << "RADIO_RANGE_DIFF available=1 stale=0"
           << " previous=" << diff.previousCandidates
           << " current=" << diff.currentCandidates
           << " added=" << diff.added
           << " removed=" << diff.removed
           << " unchanged=" << diff.unchanged;
    diff.statusLine = stream.str();
    return diff;
}

RadioReachableVerificationFeed BuildRadioReachableVerificationFeed(
    const RadioReachableControllerSnapshot& gatedSnapshot,
    const RadioReachableCandidateDiff* diff) {
    RadioReachableVerificationFeed feed;
    feed.available = gatedSnapshot.available;
    feed.stale = gatedSnapshot.stale;
    feed.generation = gatedSnapshot.generation;
    feed.inputCandidates = static_cast<int>(gatedSnapshot.candidates.size());

    std::unordered_set<std::string> verifyKeys;
    const auto useDiff = diff != nullptr && diff->available && !diff->stale;
    if (useDiff) {
        verifyKeys.insert(diff->addedStableKeys.begin(), diff->addedStableKeys.end());
    }

    if (feed.available && !feed.stale) {
        feed.controllers.reserve(gatedSnapshot.candidates.size());
        for (const auto& candidate : gatedSnapshot.candidates) {
            if (useDiff && verifyKeys.find(candidate.stableKey) == verifyKeys.end()) {
                continue;
            }
            if (!candidate.actionable) {
                continue;
            }

            ControllerSnapshot controller;
            controller.callsign = candidate.callsign;
            controller.frequency = candidate.frequency;
            controller.facility = candidate.vatsimFacility;
            controller.visualRangeNm = candidate.visualRangeNm;
            controller.actionable = candidate.actionable;
            controller.atis = candidate.atis;
            feed.controllers.push_back(std::move(controller));
        }
    }

    feed.selectedControllers = static_cast<int>(feed.controllers.size());
    std::ostringstream stream;
    stream << "RADIO_VERIFY available=" << (feed.available ? 1 : 0)
           << " stale=" << (feed.stale ? 1 : 0)
           << " input=" << feed.inputCandidates
           << " selected=" << feed.selectedControllers;
    if (useDiff) {
        stream << " diffAdded=" << diff->added
               << " diffRemoved=" << diff->removed
               << " diffUnchanged=" << diff->unchanged;
    } else {
        stream << " diff=none";
    }
    feed.statusLine = stream.str();
    return feed;
}

std::vector<std::string> RadioReachableCandidateSummaries(
    const RadioReachableControllerSnapshot& snapshot) {
    std::vector<std::string> summaries;
    summaries.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        std::ostringstream stream;
        stream << candidate.callsign << '@' << candidate.frequency
               << ':' << ToString(candidate.group)
               << ':' << ToString(candidate.source);
        summaries.push_back(stream.str());
    }
    return summaries;
}

std::string RadioReachableGroupCountSummary(
    const RadioReachableControllerSnapshot& snapshot) {
    std::ostringstream stream;
    stream << "DEL=" << snapshot.counts.delivery
           << ",GND=" << snapshot.counts.ground
           << ",TWR=" << snapshot.counts.tower
           << ",APP_DEP=" << snapshot.counts.appDep
           << ",CTR=" << snapshot.counts.center
           << ",ATIS=" << snapshot.counts.atis
           << ",OTHER=" << snapshot.counts.other;
    return stream.str();
}

std::vector<std::string> RadioReachableVerificationFeedSummaries(
    const RadioReachableVerificationFeed& feed) {
    std::vector<std::string> summaries;
    summaries.reserve(feed.controllers.size());
    for (const auto& controller : feed.controllers) {
        std::ostringstream stream;
        stream << ToUpper(Trim(controller.callsign)) << '@'
               << NormalizeFrequency(controller.frequency)
               << ":facility=" << controller.facility;
        summaries.push_back(stream.str());
    }
    return summaries;
}

}  // namespace xvatsim::brain
