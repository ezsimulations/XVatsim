#include "PdcLogMonitorContractProbe.h"

#include "XVatsim/brain/BrainOwnedRuntime.h"
#include "XVatsim/modules/runtime_workers/AsyncDiagnosticsWriter.h"
#include "XVatsim/modules/runtime_workers/PdcLogReader.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace xvatsim::tools::pdc_log_monitor_contract {
namespace {

using namespace xvatsim::brain;
using namespace xvatsim::modules::runtime_workers;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto token = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path() /
            ("xvatsim-pdc-log-gate-" + std::to_string(token));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool Require(bool condition, std::string message, std::string* failure) {
    if (condition) return true;
    if (failure != nullptr) *failure = std::move(message);
    return false;
}

const BrainOwnedAccessoryOrbPresentation* PdcOrb(
    const BrainOwnedAccessoryPresentationHandle& presentation) {
    if (presentation.snapshot == nullptr) return nullptr;
    const auto& orbs = presentation.snapshot->orbs;
    const auto found = std::find_if(orbs.begin(), orbs.end(), [](const auto& orb) {
        return orb.drawer == BrainOwnedAccessoryDrawerId::Pdc;
    });
    return found == orbs.end() ? nullptr : &*found;
}

bool CheckParserMatrix(std::string* failure) {
    struct Case {
        const char* line;
        BrainPdcLogRecordKind kind;
        BrainPdcLogChannel channel;
        bool incoming;
    };
    const Case cases[]{
        {"[06:00:00.000] >>> #APDAL250:SERVER:SYNTHETIC",
         BrainPdcLogRecordKind::SessionOpened, BrainPdcLogChannel::Direct, false},
        {"[06:00:00.500] >>> $IDDAL250:SERVER:123:xPilot:3:0:SYNTHETIC",
         BrainPdcLogRecordKind::ClientIdentity, BrainPdcLogChannel::Direct, false},
        {"[06:00:01.000] <<< #TMML_GND:DAL250:PRIVATE ONE",
         BrainPdcLogRecordKind::Text, BrainPdcLogChannel::Direct, true},
        {"[06:00:02.000] <<< #TMML_TWR:@120500:PUBLIC RADIO",
         BrainPdcLogRecordKind::Text, BrainPdcLogChannel::Radio, true},
        {"[06:00:03.000] <<< #TMML_CTR:*:BROADCAST",
         BrainPdcLogRecordKind::Text, BrainPdcLogChannel::Broadcast, true},
        {"[06:00:04.000] <<< #TMSUPERVISOR:*s:WALLOP",
         BrainPdcLogRecordKind::Text, BrainPdcLogChannel::Wallop, true},
        {"[06:00:05.000] <<< #TMSERVER:DAL250:SERVER INFO",
         BrainPdcLogRecordKind::Text, BrainPdcLogChannel::Server, true},
        {"[06:00:06.000] >>> #TMDAL250:ML_GND:OUTGOING",
         BrainPdcLogRecordKind::Text, BrainPdcLogChannel::Direct, false},
        {"[06:00:07.000] >>> #DPDAL250:SERVER:SYNTHETIC",
         BrainPdcLogRecordKind::SessionClosed, BrainPdcLogChannel::Direct, false},
    };
    std::uint64_t offset = 0;
    for (const auto& test : cases) {
        BrainPdcLogRecordFact fact;
        const auto size = std::char_traits<char>::length(test.line);
        if (!ParseXPilotNetworkLogLine(
                test.line, offset, offset + size + 1, &fact) ||
            fact.kind != test.kind || fact.channel != test.channel ||
            fact.incoming != test.incoming || !fact.fieldsComplete) {
            return Require(false, "xPilot protocol parser matrix failed", failure);
        }
        offset += size + 1;
    }
    return true;
}

bool CheckReaderMechanicalFacts(std::string* failure) {
    TemporaryDirectory temporary;
    const auto logPath = temporary.path() / "NetworkLog-20260916-055900.txt";
    {
        std::ofstream log(logPath, std::ios::binary);
        log << "NOT A LOG LINE\n"
               "[05:59:00.000] <<< $XXSERVER:DAL250:IGNORED\n"
               "[05:59:01.000] <<< #TMML_GND:DAL250:BAD";
        const char invalidUtf8[]{static_cast<char>(0xC3), '(', '\n'};
        log.write(invalidUtf8, sizeof(invalidUtf8));
        log << "[05:59:02.000] <<< #TMML_TWR:DAL250:";
        log << std::string(kPdcLogMaximumLineBytes, 'X') << '\n';
        log << "[05:59:03.000] <<< #TMML_DEL:DAL250:PARTIAL";
    }

    PdcLogReader reader(temporary.path());
    BrainPdcLogRequest discover;
    discover.operation = BrainPdcLogOperation::Discover;
    discover.requestId = 1;
    const auto discovered = reader.Read(discover);
    if (discovered.issue != BrainPdcLogIssue::None ||
        discovered.files.size() != 1 ||
        !discovered.files.front().metadataAvailable) {
        return Require(false, "PDC reader discovery facts failed", failure);
    }

    BrainPdcLogRequest read;
    read.operation = BrainPdcLogOperation::ReadRange;
    read.requestId = 2;
    read.file = discovered.files.front();
    read.endOffset = read.file.sizeBytes;
    const auto fact = reader.Read(read);
    const auto invalidUtf8 = std::find_if(
        fact.records.begin(), fact.records.end(), [](const auto& record) {
            return record.kind == BrainPdcLogRecordKind::Text &&
                record.sender == "ML_GND";
        });
    if (fact.issue != BrainPdcLogIssue::None || fact.malformedLines != 1 ||
        fact.ignoredProtocolLines != 1 || fact.oversizedLines != 1 ||
        !fact.partialLine || invalidUtf8 == fact.records.end() ||
        invalidUtf8->utf8Valid || !invalidUtf8->bodyWithinLimits) {
        return Require(false, "PDC reader mechanical error facts failed", failure);
    }

    PdcLogReader missing(temporary.path() / "missing");
    const auto unavailable = missing.Read(discover);
    return Require(
        unavailable.issue == BrainPdcLogIssue::DirectoryUnavailable,
        "PDC reader unavailable-directory fact failed", failure);
}

class ScriptedTransport final : public BrainPdcLogTransport {
public:
    BrainPdcLogRequest request;
    BrainPdcLogFact fact;
    bool ready = false;

    bool TrySubmitPdcLogRequest(const BrainPdcLogRequest& value) override {
        request = value;
        return true;
    }

    bool TryHarvestPdcLogFact(BrainPdcLogFact* value) override {
        if (!ready || value == nullptr) return false;
        *value = std::move(fact);
        ready = false;
        return true;
    }
};

bool CheckStaleFactAndCapacity(std::string* failure) {
    BrainOwnedRuntimeState staleState;
    InitializeBrainOwnedPdcRuntime(&staleState);
    ScriptedTransport transport;
    const BrainPdcLogContext first{
        true, true, "DAL250", "DAL250", "YMML", "YSSY"};
    const auto firstService = ServiceBrainOwnedPdcLogMonitor(
        &staleState, first, &transport, 1'000'000);
    if (!firstService.requestSubmitted) {
        return Require(false, "stale-fact probe did not submit", failure);
    }
    transport.fact.request = transport.request;
    transport.ready = true;
    const BrainPdcLogContext replacement{
        true, true, "DAL251", "DAL251", "YMML", "YSSY"};
    (void)ServiceBrainOwnedPdcLogMonitor(
        &staleState, replacement, &transport, 1'000'001);
    if (staleState.pdc.counters.logStaleFacts != 1 ||
        BrainOwnedPdcMessageCount(staleState.pdc) != 0 ||
        staleState.pdc.boundCallsign != "DAL251") {
        return Require(false, "Brain did not reject the stale worker fact", failure);
    }

    BrainOwnedRuntimeState bounded;
    InitializeBrainOwnedPdcRuntime(&bounded);
    bounded.pdc.boundCallsign = "DAL250";
    bounded.pdc.boundDepartureIcao = "YMML";
    bounded.pdc.boundDestinationIcao = "YSSY";
    bounded.pdc.boundFlightIdentity = "DAL250|YMML|YSSY";
    for (int index = 0; index < 40; ++index) {
        if (!AdmitBrainOwnedPdcMessage(
                &bounded,
                "ML_DEL",
                index < 2 ? "REPEATED BODY" : "PRIVATE " + std::to_string(index),
                "source-offset-" + std::to_string(index),
                index,
                "06:01:00.000",
                2'000'000 + index)) {
            return Require(false, "bounded Brain admission failed", failure);
        }
    }
    const auto duplicate = AdmitBrainOwnedPdcMessage(
        &bounded,
        "ML_DEL",
        "DIFFERENT BODY",
        "source-offset-39",
        41,
        "06:01:01.000",
        2'000'100);
    return Require(
        !duplicate && BrainOwnedPdcMessageCount(bounded.pdc) == 32 &&
            bounded.pdc.counters.historyEvictions == 8 &&
            bounded.pdc.retainedBytes <= 256U * 1024U &&
            bounded.pdc.olderArtifacts.front().body == "PRIVATE 8",
        "Brain history bounds or exact-source deduplication failed", failure);
}

bool PumpUntil(
    BrainOwnedRuntimeState* state,
    const BrainPdcLogContext& context,
    AsyncDiagnosticsWriter* worker,
    std::uint64_t* nowMicroseconds,
    std::size_t expectedMessages,
    std::string* failure) {
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto service = ServiceBrainOwnedPdcLogMonitor(
            state, context, worker, *nowMicroseconds);
        ++*nowMicroseconds;
        if (service.requestSubmitted &&
            !worker->WaitUntilIdleForTesting(std::chrono::seconds(2))) {
            return Require(false, "PDC log worker did not complete", failure);
        }
        if (BrainOwnedPdcMessageCount(state->pdc) == expectedMessages &&
            state->pdc.logMonitor.pendingRequestId == 0 &&
            !state->pdc.logMonitor.readingSnapshot &&
            !state->pdc.logMonitor.committingStagedMessages) {
            return true;
        }
    }
    return Require(false, "Brain did not settle the expected PDC messages", failure);
}

bool CheckEndToEnd(std::string* failure) {
    TemporaryDirectory temporary;
    const auto logPath = temporary.path() / "NetworkLog-20260916-060000.txt";
    {
        std::ofstream log(logPath, std::ios::binary);
        log << "[06:00:00.000] >>> $IDDAL250:SERVER:123:xPilot:3:0:SYNTHETIC\n"
               "[06:00:00.100] >>> #APDAL250:SERVER:SYNTHETIC\n"
               "[06:00:00.200] <<< #APREMOTE1:SERVER:SYNTHETIC\n"
               "[06:00:00.300] <<< #AAREMOTE2:SERVER:SYNTHETIC\n"
               "[06:00:00.400] <<< #DPREMOTE1:123456\n"
               "[06:00:01.000] <<< #TMML_GND:DAL250:PRIVATE ONE\n"
               "[06:00:02.000] <<< #TMML_TWR:@120500:PUBLIC RADIO\n"
               "[06:00:03.000] <<< #TMSERVER:DAL250:SERVER INFO\n"
               "[06:00:04.000] >>> #TMDAL250:ML_GND:OUTGOING\n"
               "[06:00:04.500] <<< #TMML_GND:N999:OTHER CALLSIGN\n"
               "[06:00:05.000] <<< #TMML_DEL:DAL250:PDC CLEARANCE\n";
    }

    AsyncDiagnosticsWriter worker;
    DiagnosticsWriterOptions options;
    options.logDirectory = temporary.path() / "diagnostics";
    options.pdcLogDirectoryForTesting = temporary.path();
    if (!Require(worker.Start(std::move(options)),
                 "PDC log worker did not start", failure)) {
        return false;
    }

    BrainOwnedRuntimeState state;
    InitializeBrainOwnedPdcRuntime(&state);
    BrainPdcLogContext context{
        true, true, "DAL250", "DAL250", "YMML", "YSSY"};
    std::uint64_t nowMicroseconds = 10'000'000;
    const auto stopAndFail = [&](std::string message) {
        worker.Stop();
        return Require(false, std::move(message), failure);
    };

    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 2, failure)) {
        worker.Stop();
        return false;
    }
    if (BrainOwnedPdcUnreadCount(state.pdc) != 2 ||
        !state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->body != "PDC CLEARANCE" ||
        state.pdc.olderArtifacts.size() != 1 ||
        state.pdc.olderArtifacts.front().body != "PRIVATE ONE" ||
        state.pdc.counters.logExcludedRecords != 6 ||
        state.pdc.counters.rejectedMessages != 1) {
        return stopAndFail("Brain admission did not exclude public/server/outgoing text");
    }

    (void)RequestBrainOwnedAccessoryDrawerSelection(
        &state, {BrainOwnedAccessoryDrawerId::Pdc, 1, 1, 1, 2});
    auto presentation = ProjectBrainOwnedAccessoryPresentation(&state, nullptr);
    const auto* orb = PdcOrb(presentation);
    if (presentation.snapshot == nullptr || orb == nullptr ||
        orb->tone != BrainOwnedAccessoryOrbPresentation::Tone::Amber ||
        orb->categoryText != "NEW" ||
        presentation.snapshot->drawerTitle != "PDC / PRIVATE MESSAGES" ||
        presentation.snapshot->entries.size() != 2 ||
        presentation.snapshot->entries[0].body != "PDC CLEARANCE" ||
        presentation.snapshot->entries[1].body != "PRIVATE ONE") {
        return stopAndFail("PDC ORB or newest-first Drawer projection failed");
    }

    const std::vector<std::string> visible{
        presentation.snapshot->entries.front().stableKey};
    if (!AcknowledgeBrainOwnedPdcVisibleEntries(&state, visible)) {
        return stopAndFail("visible PDC messages were not acknowledged");
    }
    if (BrainOwnedPdcUnreadCount(state.pdc) != 1) {
        return stopAndFail("off-viewport PDC message was acknowledged before close");
    }
    (void)RequestBrainOwnedAccessoryDrawerSelection(
        &state, {BrainOwnedAccessoryDrawerId::Pdc, 2, 3, 3, 4});
    presentation = ProjectBrainOwnedAccessoryPresentation(&state, nullptr);
    orb = PdcOrb(presentation);
    if (orb == nullptr ||
        orb->tone != BrainOwnedAccessoryOrbPresentation::Tone::Cyan ||
        orb->categoryText != "IDLE" || BrainOwnedPdcUnreadCount(state.pdc) != 0) {
        return stopAndFail(
            "closing PDC Drawer did not acknowledge the full retained batch");
    }
    (void)RequestBrainOwnedAccessoryDrawerSelection(
        &state, {BrainOwnedAccessoryDrawerId::Pdc, 3, 5, 5, 6});

    context.xpilotConnected = false;
    (void)ServiceBrainOwnedPdcLogMonitor(
        &state, context, &worker, nowMicroseconds++);
    if (BrainOwnedPdcMessageCount(state.pdc) != 2 || state.pdc.sourceAvailable) {
        return stopAndFail("disconnect did not preserve the Brain message cache");
    }
    context.xpilotConnected = true;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 2, failure) ||
        !state.pdc.sourceAvailable) {
        worker.Stop();
        return false;
    }

    const auto requestsBeforeSubCadence = state.pdc.counters.logRequestsSubmitted;
    for (int callback = 0; callback < 100; ++callback) {
        (void)ServiceBrainOwnedPdcLogMonitor(
            &state, context, &worker, nowMicroseconds + callback * 10'000ULL);
    }
    if (state.pdc.counters.logRequestsSubmitted != requestsBeforeSubCadence) {
        return stopAndFail("PDC log monitor polled before the five-second cadence");
    }

    {
        std::ofstream log(logPath, std::ios::binary | std::ios::app);
        log << "[06:00:09.000] <<< #TMSY_DEL:DAL250:PART";
    }
    nowMicroseconds += 5'100'000ULL;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 2, failure)) {
        worker.Stop();
        return false;
    }
    {
        std::ofstream log(logPath, std::ios::binary | std::ios::app);
        log << "IAL MESSAGE\n";
    }
    nowMicroseconds += 5'100'000ULL;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 3, failure)) {
        worker.Stop();
        return false;
    }
    if (!state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->body != "PARTIAL MESSAGE") {
        return stopAndFail("partial xPilot line was not admitted after completion");
    }

    {
        std::ofstream log(logPath, std::ios::binary | std::ios::app);
        log << "[06:00:10.000] <<< #TMSY_DEL:DAL250:PRIVATE TWO\n";
    }
    nowMicroseconds += 5'100'000ULL;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 4, failure)) {
        worker.Stop();
        return false;
    }
    presentation = ProjectBrainOwnedAccessoryPresentation(&state, nullptr);
    orb = PdcOrb(presentation);
    if (orb == nullptr ||
        orb->tone != BrainOwnedAccessoryOrbPresentation::Tone::Amber ||
        orb->categoryText != "NEW" ||
        presentation.snapshot == nullptr || presentation.snapshot->entries.size() != 4 ||
        presentation.snapshot->entries[0].body != "PRIVATE TWO" ||
        presentation.snapshot->entries[1].body != "PARTIAL MESSAGE" ||
        presentation.snapshot->entries[2].body != "PDC CLEARANCE" ||
        presentation.snapshot->entries[3].body != "PRIVATE ONE") {
        return stopAndFail("new private message did not restore amber/newest-first state");
    }

    const auto bytesBeforeUnchanged = state.pdc.counters.logBytesRead;
    const auto messagesBeforeUnchanged = BrainOwnedPdcMessageCount(state.pdc);
    const auto semanticBeforeUnchanged = state.pdc.semanticGeneration;
    nowMicroseconds += 5'100'000ULL;
    const auto unchanged = ServiceBrainOwnedPdcLogMonitor(
        &state, context, &worker, nowMicroseconds++);
    if (!unchanged.requestSubmitted ||
        !worker.WaitUntilIdleForTesting(std::chrono::seconds(2))) {
        return stopAndFail("unchanged metadata check did not complete");
    }
    (void)ServiceBrainOwnedPdcLogMonitor(
        &state, context, &worker, nowMicroseconds++);
    if (state.pdc.counters.logBytesRead != bytesBeforeUnchanged ||
        BrainOwnedPdcMessageCount(state.pdc) != messagesBeforeUnchanged ||
        state.pdc.semanticGeneration != semanticBeforeUnchanged) {
        return stopAndFail("unchanged five-second check created read or UI churn");
    }

    const auto rotatedPath =
        temporary.path() / "NetworkLog-20260916-060100.txt";
    {
        std::ofstream log(rotatedPath, std::ios::binary);
        log << "[06:01:00.000] >>> #APDAL250:SERVER:SYNTHETIC\n"
               "[06:01:01.000] <<< #TMML_DEL:DAL250:ROTATED MESSAGE\n"
               "[06:01:02.000] <<< $XXSERVER:DAL250:";
        log << std::string(1024, 'F') << '\n';
    }
    nowMicroseconds += 5'100'000ULL;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 5, failure) ||
        !state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->body != "ROTATED MESSAGE") {
        worker.Stop();
        return false;
    }

    {
        std::ofstream log(rotatedPath, std::ios::binary | std::ios::trunc);
        log << "[06:02:00.000] >>> #APDAL250:SERVER:SYNTHETIC\n"
               "[06:02:01.000] <<< #TMML_DEL:DAL250:TRUNCATED MESSAGE\n";
    }
    nowMicroseconds += 5'100'000ULL;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 6, failure) ||
        !state.pdc.capturedArtifact.has_value() ||
        state.pdc.capturedArtifact->body != "TRUNCATED MESSAGE") {
        worker.Stop();
        return false;
    }
    {
        std::ofstream log(rotatedPath, std::ios::binary | std::ios::app);
        log << "[06:02:02.000] <<< #TMML_DEL:DAL250:TRUNCATED MESSAGE\n";
    }
    nowMicroseconds += 5'100'000ULL;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, 7, failure)) {
        worker.Stop();
        return false;
    }

    const auto countBeforeReplay = BrainOwnedPdcMessageCount(state.pdc);
    context.xpilotConnected = false;
    (void)ServiceBrainOwnedPdcLogMonitor(
        &state, context, &worker, nowMicroseconds++);
    context.xpilotConnected = true;
    if (!PumpUntil(
            &state, context, &worker, &nowMicroseconds, countBeforeReplay, failure) ||
        BrainOwnedPdcMessageCount(state.pdc) != countBeforeReplay) {
        worker.Stop();
        return false;
    }

    BrainPdcLogContext nextFlight{
        true, true, "DAL251", "DAL251", "YMML", "YSSY"};
    (void)ServiceBrainOwnedPdcLogMonitor(
        &state, nextFlight, &worker, nowMicroseconds++);
    if (BrainOwnedPdcMessageCount(state.pdc) != 0 ||
        state.pdc.boundCallsign != "DAL251") {
        return stopAndFail("confirmed callsign change did not clear PDC history");
    }

    if (!worker.WaitUntilIdleForTesting(std::chrono::seconds(2))) {
        return stopAndFail("final PDC worker request did not complete");
    }

    const auto snapshot = worker.Snapshot();
    worker.Stop();
    return Require(snapshot.submittedPdcLogRequests >= 5 &&
                       snapshot.submittedPdcLogRequests ==
                           snapshot.completedPdcLogRequests &&
                       snapshot.rejectedPdcLogRequests == 0,
                   "PDC mailbox accounting was not balanced", failure);
}

}  // namespace

int RunPdcLogMonitorContractProbe() {
    std::string failure;
    if (!CheckParserMatrix(&failure) ||
        !CheckReaderMechanicalFacts(&failure) ||
        !CheckStaleFactAndCapacity(&failure) ||
        !CheckEndToEnd(&failure)) {
        std::cerr << "PDC_XPILOT_LOG_MONITOR_GATE_FAILED: " << failure << '\n';
        return 1;
    }
    std::cout
        << "PDC_XPILOT_LOG_MONITOR_GATE_PASSED cadence_seconds=5 "
           "brain_admission=direct_incoming_only drawer_order=newest_first "
           "unread=amber drawer_close=cyan_idle unchanged_io=zero\n";
    return 0;
}

}  // namespace xvatsim::tools::pdc_log_monitor_contract
