#include "XVatsim/brain/BrainPdcRuntime.h"

#include "XVatsim/brain/BrainOwnedRuntime.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace xvatsim::brain {
namespace {

constexpr std::uint64_t kMetadataPollMicroseconds = 5'000'000ULL;
constexpr std::uint64_t kReadContinuationMicroseconds = 250'000ULL;
constexpr std::uint64_t kRecoveryWindowBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMessagesCommittedPerCallback = 4;
constexpr std::size_t kMaximumStagedMessages = 32;

std::string NormalizeIdentity(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() &&
           std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
        --last;
    }
    std::string value(text.substr(first, last - first));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

bool SameIdentity(std::string_view left, std::string_view right) {
    return NormalizeIdentity(left) == NormalizeIdentity(right);
}

void RecordPdcSemanticMutation(BrainOwnedRuntimeState* state) {
    ++state->pdc.semanticGeneration;
    ++state->pdc.counters.semanticMutations;
    RecordBrainOwnedAccessoryDrawerContentMutation(
        state, BrainOwnedAccessoryDrawerId::Pdc);
}

void SetLogAvailability(BrainOwnedRuntimeState* state, bool available) {
    auto& pdc = state->pdc;
    const auto nextAvailability = available
        ? (BrainOwnedPdcMessageCount(pdc) == 0
               ? BrainPdcAvailability::QualifiedIdle
               : BrainPdcAvailability::Available)
        : BrainPdcAvailability::SourceUnavailable;
    if (pdc.sourceAvailable == available &&
        pdc.sourceQualified == available &&
        pdc.availability == nextAvailability) {
        return;
    }
    pdc.sourceAvailable = available;
    pdc.sourceQualified = available;
    pdc.availability = nextAvailability;
    RecordPdcSemanticMutation(state);
}

void ScheduleRetry(
    BrainPdcLogMonitorState* monitor,
    std::uint64_t nowMicroseconds,
    std::string reason) {
    constexpr std::uint64_t retrySeconds[]{1, 2, 5, 15, 30};
    const auto index = std::min<std::size_t>(
        monitor->retryStep, std::size(retrySeconds) - 1);
    monitor->nextServiceMicroseconds =
        nowMicroseconds + retrySeconds[index] * 1'000'000ULL;
    monitor->retryStep = static_cast<unsigned>(
        std::min<std::size_t>(index + 1, std::size(retrySeconds) - 1));
    monitor->reason = std::move(reason);
}

std::uint64_t ReplayHighWater(const BrainPdcLogMonitorState& monitor) {
    for (const auto& cursor : monitor.replayCursors) {
        if (!cursor.file.path.empty() &&
            SameBrainPdcLogFile(cursor.file, monitor.file)) {
            return cursor.highWaterOffset;
        }
    }
    return 0;
}

void RememberReplayHighWater(
    BrainPdcLogMonitorState* monitor,
    std::uint64_t throughOffset) {
    for (auto& cursor : monitor->replayCursors) {
        if (!cursor.file.path.empty() &&
            SameBrainPdcLogFile(cursor.file, monitor->file)) {
            cursor.highWaterOffset =
                std::max(cursor.highWaterOffset, throughOffset);
            return;
        }
    }
    auto& destination = monitor->replayCursors[
        monitor->nextReplayCursorSlot++ % monitor->replayCursors.size()];
    destination.file = monitor->file;
    destination.highWaterOffset = throughOffset;
}

void ClearReplayForFile(
    BrainPdcLogMonitorState* monitor,
    const BrainPdcLogFileFact& file) {
    for (auto& cursor : monitor->replayCursors) {
        if (!cursor.file.path.empty() && SameBrainPdcLogFile(cursor.file, file)) {
            cursor = {};
        }
    }
}

void BeginFileSnapshot(
    BrainPdcRuntimeState* pdc,
    const BrainPdcLogFileFact& file) {
    auto& monitor = pdc->logMonitor;
    monitor.file = file;
    monitor.snapshotEndOffset = file.sizeBytes;
    monitor.cursorOffset = file.sizeBytes > kRecoveryWindowBytes
        ? file.sizeBytes - kRecoveryWindowBytes
        : 0;
    monitor.discardLeadingFragment = monitor.cursorOffset != 0;
    monitor.readingSnapshot = monitor.cursorOffset < monitor.snapshotEndOffset;
    monitor.sessionOpen = false;
    monitor.sessionOpenOffset = 0;
    monitor.sessionCallsign.clear();
    monitor.stagedDirectMessages.clear();
    monitor.stagedCommitIndex = 0;
    monitor.committingStagedMessages = false;
    monitor.recoveryRequired = false;
    if (monitor.cursorOffset != 0) ++pdc->counters.capacityLosses;
}

void ObserveLogRecord(
    BrainOwnedRuntimeState* state,
    BrainPdcLogRecordFact record) {
    auto& pdc = state->pdc;
    auto& monitor = pdc.logMonitor;
    if (record.kind == BrainPdcLogRecordKind::ClientIdentity) {
        return;
    }
    if (record.kind == BrainPdcLogRecordKind::SessionOpened) {
        if (record.incoming || !record.fieldsComplete ||
            !SameIdentity(record.sender, pdc.boundCallsign)) {
            ++pdc.counters.logExcludedRecords;
            return;
        }
        monitor.sessionOpen = true;
        monitor.sessionCallsign = record.sender;
        monitor.sessionOpenOffset = record.beginOffset;
        monitor.stagedDirectMessages.clear();
        monitor.stagedCommitIndex = 0;
        monitor.committingStagedMessages = false;
        return;
    }
    if (record.kind == BrainPdcLogRecordKind::SessionClosed) {
        if (record.incoming || !record.fieldsComplete ||
            !SameIdentity(record.sender, pdc.boundCallsign)) {
            ++pdc.counters.logExcludedRecords;
            return;
        }
        monitor.sessionOpen = false;
        monitor.sessionCallsign.clear();
        monitor.stagedDirectMessages.clear();
        monitor.stagedCommitIndex = 0;
        monitor.committingStagedMessages = false;
        return;
    }

    ++pdc.counters.observationsEvaluated;
    if (!record.incoming || record.channel != BrainPdcLogChannel::Direct) {
        ++pdc.counters.logExcludedRecords;
        return;
    }
    if (!record.fieldsComplete || !record.bodyWithinLimits ||
        !record.utf8Valid || record.body.find('\0') != std::string::npos ||
        NormalizeIdentity(record.body).empty()) {
        ++pdc.counters.observationsRejectedMechanical;
        return;
    }
    if (!monitor.sessionOpen ||
        !SameIdentity(monitor.sessionCallsign, pdc.boundCallsign) ||
        !SameIdentity(record.recipient, pdc.boundCallsign)) {
        ++pdc.counters.rejectedMessages;
        return;
    }
    if (record.endOffset <= ReplayHighWater(monitor)) {
        ++pdc.counters.connectedDuplicates;
        return;
    }
    if (monitor.stagedDirectMessages.size() >= kMaximumStagedMessages) {
        monitor.stagedDirectMessages.erase(
            monitor.stagedDirectMessages.begin());
        ++pdc.counters.capacityLosses;
    }
    if (monitor.stagedDirectMessages.capacity() == 0) {
        monitor.stagedDirectMessages.reserve(kMaximumStagedMessages);
    }
    monitor.stagedDirectMessages.push_back(std::move(record));
}

void FinishSnapshot(
    BrainOwnedRuntimeState* state,
    const BrainPdcLogFact& fact,
    std::uint64_t nowMicroseconds) {
    auto& pdc = state->pdc;
    auto& monitor = pdc.logMonitor;
    monitor.readingSnapshot = false;
    const auto ageSeconds = fact.observedUnixSeconds -
        monitor.file.modifiedUnixSeconds;
    const bool currentSession = monitor.sessionOpen &&
        SameIdentity(monitor.sessionCallsign, pdc.boundCallsign) &&
        ageSeconds >= -10 && ageSeconds <= 180;
    SetLogAvailability(state, currentSession);
    monitor.retryStep = 0;
    if (currentSession) {
        monitor.committingStagedMessages = true;
        monitor.stagedCommitIndex = 0;
        monitor.reason = "brain-validating-staged-direct-messages";
    } else {
        monitor.stagedDirectMessages.clear();
        monitor.stagedCommitIndex = 0;
        monitor.committingStagedMessages = false;
        RememberReplayHighWater(&monitor, monitor.cursorOffset);
        monitor.nextServiceMicroseconds =
            nowMicroseconds + kMetadataPollMicroseconds;
        monitor.reason = monitor.sessionOpen
            ? "xpilot-log-session-does-not-match-active-flight"
            : "xpilot-log-has-no-open-active-session";
    }
}

void CommitStagedMessages(
    BrainOwnedRuntimeState* state,
    std::uint64_t nowMicroseconds) {
    auto& pdc = state->pdc;
    auto& monitor = pdc.logMonitor;
    const auto stop = std::min(
        monitor.stagedDirectMessages.size(),
        monitor.stagedCommitIndex + kMessagesCommittedPerCallback);
    for (; monitor.stagedCommitIndex < stop; ++monitor.stagedCommitIndex) {
        auto& record = monitor.stagedDirectMessages[monitor.stagedCommitIndex];
        const auto revision =
            "PDC-L" + std::to_string(pdc.productLifecycleEpoch) + "-" +
            std::to_string(monitor.file.volumeSerial) + "-" +
            std::to_string(monitor.file.fileIndex) + "-" +
            std::to_string(monitor.file.creationTicks) + "-I" +
            std::to_string(monitor.fileIncarnation) + "-O" +
            std::to_string(record.beginOffset);
        (void)AdmitBrainOwnedPdcMessage(
            state,
            std::move(record.sender),
            std::move(record.body),
            revision,
            static_cast<std::int64_t>(std::min<std::uint64_t>(
                record.endOffset,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()))),
            std::move(record.timeUtc),
            nowMicroseconds);
        RememberReplayHighWater(&monitor, record.endOffset);
    }
    if (monitor.stagedCommitIndex == monitor.stagedDirectMessages.size()) {
        monitor.stagedDirectMessages.clear();
        monitor.stagedCommitIndex = 0;
        monitor.committingStagedMessages = false;
        RememberReplayHighWater(&monitor, monitor.cursorOffset);
        monitor.nextServiceMicroseconds =
            nowMicroseconds + kMetadataPollMicroseconds;
        monitor.reason = "monitoring-xpilot-private-messages";
    }
}

void ObserveConnection(
    BrainOwnedRuntimeState* state,
    bool connected) {
    auto& monitor = state->pdc.logMonitor;
    if (monitor.connectionKnown && monitor.connected == connected) return;
    monitor.connectionKnown = true;
    monitor.connected = connected;
    ++monitor.connectionEpoch;
    monitor.pendingRequestId = 0;
    monitor.recoveryRequired = true;
    monitor.readingSnapshot = false;
    monitor.committingStagedMessages = false;
    monitor.stagedDirectMessages.clear();
    monitor.stagedCommitIndex = 0;
    monitor.nextServiceMicroseconds = 0;
    monitor.reason = connected
        ? "xpilot-connected-log-recovery-required"
        : "waiting-for-xpilot-connection";
    if (!connected) SetLogAvailability(state, false);
}

}  // namespace

BrainPdcLogServiceResult ServiceBrainOwnedPdcLogMonitor(
    BrainOwnedRuntimeState* state,
    const BrainPdcLogContext& context,
    BrainPdcLogTransport* transport,
    std::uint64_t nowMicroseconds) {
    BrainPdcLogServiceResult result;
    if (state == nullptr || transport == nullptr || !state->pdc.initialized ||
        state->pdc.pluginAdminSuspended) {
        result.reason = "pdc-log-monitor-not-running";
        return result;
    }

    const auto semanticGenerationBefore = state->pdc.semanticGeneration;
    BrainPdcLogFact fact;
    const bool factAvailable = transport->TryHarvestPdcLogFact(&fact);

    const auto flightCallsign = NormalizeIdentity(context.flightCallsign);
    const auto networkCallsign = NormalizeIdentity(context.xpilotCallsign);
    const auto departure = NormalizeIdentity(context.departureIcao);
    const auto destination = NormalizeIdentity(context.destinationIcao);
    const bool callsignsAgree = networkCallsign.empty() ||
        networkCallsign == flightCallsign;
    const bool contextReady = context.flightContextActive &&
        context.xpilotConnected && !flightCallsign.empty() &&
        !departure.empty() && callsignsAgree && flightCallsign.size() <= 64 &&
        departure.size() <= 8 && destination.size() <= 8;

    ObserveConnection(state, context.xpilotConnected);
    auto* pdc = &state->pdc;
    const bool bindingChanged = contextReady &&
        (!SameIdentity(pdc->boundCallsign, flightCallsign) ||
         !SameIdentity(pdc->boundDepartureIcao, departure) ||
         !SameIdentity(pdc->boundDestinationIcao, destination));
    if (bindingChanged) {
        if (!pdc->boundPlanIdentity.empty()) {
            ResetBrainOwnedPdcProductState(state, false);
            pdc = &state->pdc;
            ObserveConnection(state, context.xpilotConnected);
        }
        pdc->boundCallsign = flightCallsign;
        pdc->boundDepartureIcao = departure;
        pdc->boundDestinationIcao = destination;
        pdc->boundPlanIdentity = BuildBrainOwnedPlanIdentityKey(
            flightCallsign, departure, destination);
        pdc->boundFlightIdentity = pdc->boundPlanIdentity + "|pdc-epoch=" +
            std::to_string(pdc->productLifecycleEpoch);
        pdc->acquisitionArmed = true;
        pdc->logMonitor.recoveryRequired = true;
        pdc->logMonitor.nextServiceMicroseconds = 0;
        RecordPdcSemanticMutation(state);
    }

    auto& monitor = pdc->logMonitor;
    if (factAvailable) {
        result.factConsumed = true;
        result.issue = fact.issue;
        ++pdc->counters.logFactsConsumed;
        pdc->counters.logBytesRead += fact.bytesRead;
        pdc->counters.logWorkerWallMicroseconds +=
            fact.workerWallMicroseconds;
        pdc->counters.logWorkerCpuMicroseconds +=
            fact.workerCpuMicroseconds;
        pdc->counters.logIgnoredProtocolLines +=
            fact.ignoredProtocolLines;
        pdc->counters.logMalformedRecords += fact.malformedLines +
            fact.oversizedLines;

        const bool stale = !contextReady ||
            fact.request.productEpoch != pdc->productLifecycleEpoch ||
            fact.request.connectionEpoch != monitor.connectionEpoch ||
            fact.request.requestId != monitor.pendingRequestId;
        if (stale) {
            ++pdc->counters.logStaleFacts;
            if (fact.request.requestId == monitor.pendingRequestId) {
                monitor.pendingRequestId = 0;
            }
            monitor.reason = "ignored-stale-pdc-log-fact";
        } else {
            monitor.pendingRequestId = 0;
            const bool limitedDiscovery =
                fact.issue == BrainPdcLogIssue::DiscoveryLimitReached &&
                !fact.files.empty();
            if (fact.issue != BrainPdcLogIssue::None && !limitedDiscovery) {
                monitor.recoveryRequired = true;
                monitor.readingSnapshot = false;
                monitor.committingStagedMessages = false;
                monitor.stagedDirectMessages.clear();
                SetLogAvailability(state, false);
                ScheduleRetry(
                    &monitor, nowMicroseconds, "xpilot-log-read-unavailable");
            } else if (fact.request.operation == BrainPdcLogOperation::Discover) {
                if (limitedDiscovery) ++pdc->counters.capacityLosses;
                const BrainPdcLogFileFact* newest = nullptr;
                for (const auto& candidate : fact.files) {
                    if (candidate.path.size() <= 4096 &&
                        (!newest || candidate.path > newest->path)) {
                        newest = &candidate;
                    }
                }
                if (newest == nullptr || !newest->metadataAvailable) {
                    monitor.recoveryRequired = true;
                    monitor.readingSnapshot = false;
                    SetLogAvailability(state, false);
                    ScheduleRetry(
                        &monitor, nowMicroseconds, "xpilot-network-log-not-found");
                } else {
                    const bool sameFile = !monitor.file.path.empty() &&
                        SameBrainPdcLogFile(monitor.file, *newest);
                    const bool rewritten = sameFile &&
                        (newest->sizeBytes < monitor.cursorOffset ||
                         (newest->sizeBytes == monitor.file.sizeBytes &&
                          newest->modificationTicks !=
                              monitor.file.modificationTicks));
                    if (rewritten) {
                        ++monitor.fileIncarnation;
                        ClearReplayForFile(&monitor, *newest);
                    }
                    if (monitor.recoveryRequired || !sameFile || rewritten) {
                        BeginFileSnapshot(pdc, *newest);
                    } else {
                        monitor.file = *newest;
                        monitor.snapshotEndOffset = newest->sizeBytes;
                        monitor.readingSnapshot =
                            monitor.cursorOffset < monitor.snapshotEndOffset;
                    }
                    monitor.retryStep = 0;
                    monitor.nextServiceMicroseconds = monitor.readingSnapshot
                        ? nowMicroseconds
                        : nowMicroseconds + kMetadataPollMicroseconds;
                    monitor.reason = monitor.readingSnapshot
                        ? "reading-xpilot-log-snapshot"
                        : "xpilot-log-unchanged";
                }
            } else if (fact.nextOffset < monitor.cursorOffset ||
                       fact.nextOffset > monitor.snapshotEndOffset ||
                       fact.records.size() >
                           kPdcLogMaximumRecordsPerFact) {
                monitor.recoveryRequired = true;
                monitor.readingSnapshot = false;
                SetLogAvailability(state, false);
                ScheduleRetry(
                    &monitor, nowMicroseconds, "invalid-pdc-log-cursor-fact");
            } else {
                monitor.cursorOffset = fact.nextOffset;
                monitor.discardLeadingFragment =
                    fact.discardLeadingFragment;
                for (auto& record : fact.records) {
                    ObserveLogRecord(state, std::move(record));
                }
                if (fact.reachedRequestedEnd) {
                    FinishSnapshot(state, fact, nowMicroseconds);
                } else if (fact.partialLine &&
                           fact.nextOffset == fact.request.beginOffset) {
                    monitor.readingSnapshot = false;
                    monitor.nextServiceMicroseconds =
                        nowMicroseconds + kMetadataPollMicroseconds;
                    monitor.reason = "waiting-for-complete-xpilot-log-line";
                } else {
                    monitor.readingSnapshot = true;
                    monitor.nextServiceMicroseconds =
                        nowMicroseconds + kReadContinuationMicroseconds;
                    monitor.reason = "continuing-xpilot-log-snapshot";
                }
            }
        }
    }

    if (!contextReady) {
        if (!context.xpilotConnected) SetLogAvailability(state, false);
        result.reason = callsignsAgree
            ? "pdc-flight-context-not-ready"
            : "pdc-flight-and-xpilot-callsigns-disagree";
        result.presentationChanged =
            semanticGenerationBefore != state->pdc.semanticGeneration;
        return result;
    }

    if (monitor.committingStagedMessages) {
        CommitStagedMessages(state, nowMicroseconds);
    }

    if (!monitor.committingStagedMessages &&
        monitor.pendingRequestId == 0 &&
        nowMicroseconds >= monitor.nextServiceMicroseconds) {
        BrainPdcLogRequest request;
        request.requestId = ++monitor.nextRequestId;
        request.productEpoch = pdc->productLifecycleEpoch;
        request.connectionEpoch = monitor.connectionEpoch;
        request.operation = monitor.readingSnapshot &&
                !monitor.recoveryRequired
            ? BrainPdcLogOperation::ReadRange
            : BrainPdcLogOperation::Discover;
        if (request.operation == BrainPdcLogOperation::ReadRange) {
            request.file = monitor.file;
            request.beginOffset = monitor.cursorOffset;
            request.endOffset = monitor.snapshotEndOffset;
            request.discardLeadingFragment =
                monitor.discardLeadingFragment;
        }
        if (transport->TrySubmitPdcLogRequest(request)) {
            monitor.pendingRequestId = request.requestId;
            ++pdc->counters.logRequestsSubmitted;
            result.requestSubmitted = true;
            monitor.reason = request.operation == BrainPdcLogOperation::Discover
                ? "requested-xpilot-log-metadata"
                : "requested-xpilot-log-range";
        } else {
            monitor.nextServiceMicroseconds = nowMicroseconds + 1'000'000ULL;
            monitor.reason = "pdc-log-worker-mailbox-busy";
        }
    }

    result.reason = monitor.reason;
    result.presentationChanged =
        semanticGenerationBefore != state->pdc.semanticGeneration;
    return result;
}

}  // namespace xvatsim::brain
