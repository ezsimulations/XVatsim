#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xvatsim::brain {

constexpr double kBrainOwnedMaxRadioBoardCandidateDistanceNm = 300.0;

enum class OverlayMode {
    Dormant,
    Prewarm,
    Active,
    Locked,
    Cooldown,
};

enum class FlightPhase {
    Unknown,
    Parked,
    Taxi,
    GroundRoll,
    Departure,
    Climb,
    Cruise,
    Descent,
    Approach,
};

enum class WorkflowStage {
    None,
    Departure,
    Enroute,
    Arrival,
};

enum class AirportSource {
    None,
    CurrentLocation,
    OnboardFms,
    OnboardGps,
    VatsimFiled,
};

struct AircraftStateSnapshot {
    bool valid = false;
    bool onGround = false;
    // Compatibility summary of the raw battery-switch facts. The aircraft
    // worker sets this when any available battery switch is on. The Brain
    // remains responsible for deciding whether the aircraft is powered.
    bool batteryOn = false;
    bool batterySwitchFactsAvailable = false;
    int batterySwitchCount = 0;
    int batterySwitchOnCount = 0;
    bool electricalBusVoltageFactsAvailable = false;
    int electricalBusVoltageCount = 0;
    double maximumElectricalBusVoltage = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double altitudeMslFt = 0.0;
    double altitudeOperationalFt = 0.0;
    double altimeterSettingInHg = 29.92;
    bool hasAltimeterSetting = false;
    double altitudeAglFt = 0.0;
    double groundSpeedKt = 0.0;
    double verticalSpeedFpm = 0.0;
    bool hasTrack = false;
    double trackTrueDeg = 0.0;
};

struct PhaseSnapshot {
    FlightPhase phase = FlightPhase::Unknown;
};

struct FlightPlanSnapshot {
    bool available = false;
    std::string currentAirportIcao;
    double currentAirportLatDeg = 0.0;
    double currentAirportLonDeg = 0.0;
    bool hasCurrentAirportCoordinates = false;
    AirportSource currentAirportSource = AirportSource::None;
    std::string departureIcao;
    double departureLatDeg = 0.0;
    double departureLonDeg = 0.0;
    bool hasDepartureCoordinates = false;
    AirportSource departureSource = AirportSource::None;
    std::string destinationIcao;
    double destinationLatDeg = 0.0;
    double destinationLonDeg = 0.0;
    bool hasDestinationCoordinates = false;
    AirportSource destinationSource = AirportSource::None;
};

struct RadioStateSnapshot {
    bool valid = false;
    std::string com1ActiveFrequency;
    std::string com2ActiveFrequency;
    std::string com1StandbyFrequency;
    bool standbyAssistEnabled = false;
    bool com1Powered = false;
    bool com2Powered = false;
    bool com1TxAvailable = false;
    bool com1RxAvailable = false;
    bool com2TxAvailable = false;
    bool com2RxAvailable = false;
    bool com1TxActive = false;
    bool com1RxActive = false;
    bool com2TxActive = false;
    bool com2RxActive = false;
    bool modeCActive = false;
};

struct XPilotSessionSnapshot {
    bool loaded = false;
    bool connected = false;
    std::string callsign;
    std::string rawStatus;
    std::string statusLine;
};

struct PilotIdentitySnapshot {
    bool connected = false;
    bool ready = false;
    std::string callsign;
    std::string normalizedCallsign;
    std::string statusLine;
};

struct NetworkPlanSnapshot {
    bool feedAvailable = false;
    bool stale = true;
    bool matched = false;
    int cid = 0;
    std::string matchedCallsign;
    std::string departureIcao;
    double departureLatDeg = 0.0;
    double departureLonDeg = 0.0;
    bool hasDepartureCoordinates = false;
    std::string destinationIcao;
    double destinationLatDeg = 0.0;
    double destinationLonDeg = 0.0;
    bool hasDestinationCoordinates = false;
    double filedCruiseAltitudeFt = 0.0;
    bool hasFiledCruiseAltitude = false;
    std::string routeText;
    std::string statusLine;
};

struct ControllerSnapshot {
    std::string callsign;
    std::string frequency;
    int facility = 0;
    int visualRangeNm = 0;
    std::string sourceLastUpdated;
    std::string sourceLogonTime;
    // Compatibility fields. They are mechanically decoded facts only; the
    // Brain owns operational eligibility and display decisions.
    bool actionable = true;
    bool atis = false;
    std::string textAtis;
};

enum class VnasLiveOwnershipKind {
    ExactPosition,
    DynamicAssumption,
    ParentAssumption,
};

// Source-owned vNAS operating facts may be acquired globally by the adapter.
// Ordinary authority work filters them to exact radio-board controller
// identities before they enter the Brain decision identity. sourcePosition*
// identifies the static vNAS position whose polygon ownership may be joined;
// controller* remains the live VATSIM identity and contact frequency.
struct VnasLiveOwnershipFactSnapshot {
    std::string controllerCallsign;
    std::string controllerFrequency;
    std::string artccId;
    std::string facilityId;
    std::string livePositionId;
    std::string livePositionCallsign;
    std::string sourcePositionId;
    std::string sourcePositionCallsign;
    std::string sourcePositionFrequency;
    std::string tcpToken;
    // Source-published airports owned by the live position's facility/area.
    // These are group-to-child join keys for the canonical terminal map; they
    // are never inferred from radio range or an endpoint query.
    std::vector<std::string> ownedAirportCodes;
    // Airports for which this live controller/frequency is explicitly
    // published as the TDLS departure owner.
    std::vector<std::string> departureAirportCodes;
    VnasLiveOwnershipKind ownershipKind =
        VnasLiveOwnershipKind::ExactPosition;
    std::string proof;
};

struct VnasLiveOwnershipSnapshot {
    bool enabled = false;
    bool available = false;
    bool stale = true;
    bool facilityDataComplete = false;
    bool ownershipDataAvailable = false;
    std::uint64_t generation = 0;
    std::uint64_t stableHash = 0;
    std::uint64_t unresolvedLivePositions = 0;
    std::string statusLine;
    std::vector<VnasLiveOwnershipFactSnapshot> facts;
};

// A worker reports one bounded score per independent evidence family. Scores
// are deliberately ternary so no provider can outweigh the other providers or
// turn its confidence estimate into a hidden veto.
struct BrainControllerEvidenceVote {
    std::string source;
    int score = 0;
    std::string provenance;
    std::string reason;
    std::string deduplicationKey;
    bool contributesToScore = true;
};

// Canonical authority-only controller input. Radio-range presentation fields
// such as visual range are intentionally absent.
struct AuthorityControllerSnapshot {
    std::string callsign;
    std::string frequency;
    int facility = 0;
    bool actionable = true;
    bool atis = false;
    std::string textAtis;
};

struct ControllerFeedSnapshot {
    bool available = false;
    bool stale = true;
    bool controllerComponentPresent = false;
    bool controllerComponentComplete = false;
    bool controllerComponentTruncated = false;
    std::uint64_t generation = 0;
    int connectedControllers = 0;
    int retainedControllerRecords = 0;
    int rejectedControllerRecords = 0;
    std::string statusLine;
    const std::vector<ControllerSnapshot>* controllers = nullptr;
    // When present, this is the immutable owner behind controllers and may be
    // safely retained by background requests.
    std::shared_ptr<const std::vector<ControllerSnapshot>> ownedControllers;
    std::uint64_t controllerContentDigest = 0;
    // Authority identity deliberately excludes fetch generations, aggregate
    // controller counts, visual range, and feed presentation text. It is
    // compiled with the immutable controller vector on the fetch thread.
    std::uint64_t authorityControllerContentDigest = 0;
    std::shared_ptr<const std::vector<AuthorityControllerSnapshot>>
        ownedAuthorityControllers;

    const std::vector<ControllerSnapshot>& Controllers() const {
        static const std::vector<ControllerSnapshot> kEmptyControllers;
        if (!available || stale || controllers == nullptr) {
            return kEmptyControllers;
        }
        return *controllers;
    }
};

enum class BrainPdcSourceConnectionStatus {
    Unknown,
    Disconnected,
    Connecting,
    Connected,
};

struct BrainPdcMechanicalObservation {
    bool sourceQualified = false;
    bool capabilitiesQualified = false;
    bool tupleMechanicallyComplete = false;
    bool senderBodyRead = false;
    bool sequenceOnlyFastPath = false;
    std::uint64_t pluginInstanceIdentity = 0;
    std::uint64_t capabilityGeneration = 0;
    std::uint64_t mechanicalIssueMask = 0;
    std::uint64_t observedMonotonicMicroseconds = 0;
    std::uint64_t samplerElapsedMicroseconds = 0;
    std::size_t senderByteCount = 0;
    std::size_t bodyByteCount = 0;
    std::uint64_t sessionLocalDigest = 0;
    std::int64_t sequenceBefore = 0;
    std::int64_t sequenceAfter = 0;
    BrainPdcSourceConnectionStatus statusBefore =
        BrainPdcSourceConnectionStatus::Unknown;
    BrainPdcSourceConnectionStatus statusAfter =
        BrainPdcSourceConnectionStatus::Unknown;
    std::string callsignBefore;
    std::string callsignAfter;
    std::string sender;
    std::string body;
};

enum class BrainRawVatsimAtisMechanicalIssue : std::uint32_t {
    None = 0,
    RootMissing = 1U << 0U,
    RootWrongType = 1U << 1U,
    RootCountExceeded = 1U << 2U,
    AggregateFieldBytesExceeded = 1U << 3U,
    ConservativeMemoryExceeded = 1U << 4U,
    RecordWrongType = 1U << 5U,
    CallsignWrongType = 1U << 6U,
    CallsignUnsafeOrOverLimit = 1U << 7U,
    FrequencyWrongType = 1U << 8U,
    FrequencyUnsafeOrOverLimit = 1U << 9U,
    InformationCodeWrongType = 1U << 10U,
    InformationCodeUnsafeOrOverLimit = 1U << 11U,
    TimestampWrongType = 1U << 12U,
    TimestampUnsafeOrOverLimit = 1U << 13U,
    TextWrongType = 1U << 14U,
    TextLineCountExceeded = 1U << 15U,
    TextLineUnsafeOrOverLimit = 1U << 16U,
    TextBodyExceeded = 1U << 17U,
};

constexpr std::uint32_t BrainRawVatsimAtisIssueBit(
    BrainRawVatsimAtisMechanicalIssue issue) {
    return static_cast<std::uint32_t>(issue);
}

struct BrainRawVatsimAtisRecord {
    std::string callsign;
    std::string frequency;
    std::string informationCode;
    std::vector<std::string> textLines;
    std::string lastUpdated;
    std::string logonTime;
    std::uint32_t mechanicalIssueMask = 0;
    std::size_t retainedFieldBytes = 0;
    bool mechanicallyComplete = false;
};

struct ReceivableControllerSnapshot {
    std::string callsign;
    std::string frequency;
    double distanceNm = 0.0;
    double score = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct TransceiverParserHygieneCounters {
    int invalidClientCallsign = 0;
    int invalidTransceiverFrequency = 0;
    int invalidPosition = 0;
    int invalidHeight = 0;
    int parseException = 0;
    int emptyPayload = 0;
    bool maxTransceiverTruncation = false;
};

struct TransceiverSourceEvidenceSnapshot {
    bool feedCacheExists = false;
    int cachedTransceiverCount = 0;
    bool sourceControllerCountKnown = false;
    int sourceControllerCount = 0;
    bool cacheFresh = false;
    bool cacheStale = true;
    bool holdoverUsed = false;
    bool holdoverExpired = false;
    bool hasFeedAgeSeconds = false;
    long long feedAgeSeconds = 0;
    bool fetchAttempted = false;
    bool fetchInProgress = false;
    bool fetchFailed = false;
    std::string failureReason;
    TransceiverParserHygieneCounters parser;
};

struct TransceiverStationEvidenceSnapshot {
    std::string sourceFrequency;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double heightAglFt = 0.0;
    bool hasAircraftDistance = false;
    double aircraftDistanceNm = 0.0;
    double maxCandidateDistanceNm = kBrainOwnedMaxRadioBoardCandidateDistanceNm;
    bool withinMaxCandidateDistance = false;
    bool hasReceivableRange = false;
    double receivableRangeNm = 0.0;
    bool withinReceivableRange = false;
    double score = 0.0;
    bool bestByModuleScore = false;
    bool transceiverFrequencyGuard = false;
};

struct TransceiverControllerEvidenceSnapshot {
    std::string callsign;
    std::string controllerFrequency;
    std::string textAtis;
    int facility = 0;
    bool actionable = false;
    bool atis = false;
    int visualRangeNm = 0;
    bool hasTransceiverEntry = false;
    int matchingTransceiverCount = 0;
    std::string resolvedDisplayFrequency;
    std::string displayFrequencySource;
    std::string displayFrequencyUnavailableReason;
    std::string pathUnavailableReason;
    bool controllerFrequencyGuard = false;
    bool transceiverFrequencyGuard = false;
    std::vector<TransceiverStationEvidenceSnapshot> stations;
};

struct TransceiverResolutionSnapshot {
    bool available = false;
    bool stale = true;
    int receivableControllers = 0;
    int distanceRejectedControllers = 0;
    double maxCandidateDistanceNm = kBrainOwnedMaxRadioBoardCandidateDistanceNm;
    std::string statusLine;
    std::string resolutionPath;
    // Transceiver resolver paths are evidence producers. When
    // controllerEvidence is present, this vector is a legacy compatibility
    // projection only; brain-owned workers build live candidates from evidence.
    bool candidatesCompatibilityOnly = false;
    int droppedBeforeBrainControllers = 0;
    std::vector<ReceivableControllerSnapshot> candidates;
    TransceiverSourceEvidenceSnapshot sourceEvidence;
    std::vector<TransceiverControllerEvidenceSnapshot> controllerEvidence;
};

struct RouteWaypointSnapshot {
    std::string ident;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
};

struct RouteSectorMatchSnapshot {
    std::string identifier;
    double entryDistanceNm = 0.0;
    std::vector<std::string> matchTokens;
    std::vector<std::string> controllerCallsignPatterns;
    std::vector<std::string> controllerPrefixes;
    bool centerCoverage = false;
    bool terminalCoverage = false;
};

// Source facts parsed from VATSpy [Airports] records. These records describe
// published endpoint callsign aliases; they do not decide controller relevance.
struct AirportCallsignAliasSnapshot {
    std::string airportIcao;
    std::string callsignPrefix;
    std::string boundaryId;
    std::string source;
    std::string sourceRecord;
};

struct RouteSectorSnapshot {
    bool available = false;
    bool stale = true;
    bool routeResolved = false;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string departureIcao;
    std::string destinationIcao;
    std::string statusLine;
    std::vector<RouteWaypointSnapshot> waypoints;
    std::vector<RouteSectorMatchSnapshot> currentSectors;
    std::vector<RouteSectorMatchSnapshot> nextSectors;
    std::vector<AirportCallsignAliasSnapshot> airportCallsignAliases;
};

enum class AuthorityRelevanceKind {
    Center,
    Terminal,
    Extension,
};

struct AuthorityRelevanceSourceEvidenceSnapshot {
    bool scheduled = false;
    std::string scheduleReason;
    bool controllerFeedAvailable = false;
    bool controllerFeedStale = true;
    std::uint64_t controllerFeedGeneration = 0;
    bool sourceControllerCountKnown = false;
    int sourceControllerCount = 0;
    bool routeSnapshotAvailable = false;
    bool routeSnapshotStale = true;
    bool routeResolved = false;
    std::string workStage;
    double workWindowNm = 0.0;
    int workDeferredSectorCount = 0;
    std::vector<std::string> routeAuthorityKeys;
    std::vector<std::string> routeAuthorityMatchKeys;
    bool cacheHit = false;
    std::string cacheStatus;
    std::string cacheReason;
};

struct AuthorityDecisionEvidenceSnapshot {
    std::string authorityId;
    std::string authoritySource;
    std::string authorityKind;
    std::string polygonKey;
    std::string matchedPattern;
    bool accepted = false;
    bool routeScopeMatched = false;
    bool relevant = false;
    std::vector<std::string> rejectionReasons;
};

struct AuthorityPolygonEvidenceSnapshot {
    std::string callsign;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    std::string authoritySource;
    std::string authorityKind;
    bool routeKeyMatch = false;
    bool routeFamilyMatch = false;
    bool routeEndpointMatch = false;
    bool inQueryScope = false;
    std::string queryScopeOutReason;
    bool routeGeometryRelevant = false;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
    bool activePolygon = false;
    std::string activeProofSource;
    std::string activeProofDetail;
    bool routeKeyCompatible = false;
    bool relevant = false;
    std::string filteredReason;
};

struct AuthorityControllerEvidenceSnapshot {
    std::string callsign;
    std::string frequency;
    int facility = 0;
    bool actionable = false;
    bool atis = false;
    bool guardFrequency = false;
    bool emptyCallsign = false;
    bool airportLocalCandidate = false;
    bool airspaceAuthorityCandidate = false;
    bool sourceControllerConsidered = false;
    std::vector<std::string> evidenceReasons;
    std::vector<AuthorityDecisionEvidenceSnapshot> authorityDecisions;
    std::vector<AuthorityPolygonEvidenceSnapshot> activePolygons;
};

struct AuthorityRelevanceEvidenceSnapshot {
    AuthorityRelevanceSourceEvidenceSnapshot source;
    std::vector<AuthorityControllerEvidenceSnapshot> controllerEvidence;
    std::vector<AuthorityPolygonEvidenceSnapshot> polygonEvidence;
    std::vector<AuthorityPolygonEvidenceSnapshot> activePolygonEvidence;
};

struct RelevantAuthoritySnapshot {
    std::string callsign;
    std::string frequency;
    std::string authorityId;
    std::string polygonId;
    std::string polygonKey;
    std::string matchedPattern;
    std::string proofSource;
    std::string proofDetail;
    AuthorityRelevanceKind kind = AuthorityRelevanceKind::Center;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
};

enum class LiveAuthorityAssignmentState {
    ActiveExact,
    ActiveDynamic,
    ActiveParent,
    Conflict,
    Unmapped,
    Stale,
    Offline,
};

inline bool IsActiveLiveAuthorityState(LiveAuthorityAssignmentState state) {
    return state == LiveAuthorityAssignmentState::ActiveExact ||
           state == LiveAuthorityAssignmentState::ActiveDynamic ||
           state == LiveAuthorityAssignmentState::ActiveParent;
}

struct LiveAuthorityAssignmentSnapshot {
    LiveAuthorityAssignmentState state =
        LiveAuthorityAssignmentState::ActiveExact;
    // Mechanical source-position join before the Brain applies global conflict
    // and publication policy. Incremental refresh may reuse this immutable
    // fact, but only the Brain-owned state field above controls consumers.
    LiveAuthorityAssignmentState candidateState =
        LiveAuthorityAssignmentState::ActiveExact;
    // Mechanical catalog fact used by the Brain's multi-controller conflict
    // policy. This is not itself an authority decision.
    bool explicitTerminalPosition = false;
    std::string canonicalPolygonId;
    std::string polygonKey;
    std::string positionId;
    std::string callsign;
    std::string frequency;
    int facility = 0;
    std::string authoritySource;
    AuthorityRelevanceKind kind = AuthorityRelevanceKind::Center;
    std::string matchedPattern;
    std::string ownershipProof;
    std::vector<std::string> departureAirportCodes;
    std::uint64_t dynamicOwnershipGeneration = 0;
    bool aircraftInside = false;
    bool routeIntersects = false;
    double routeEntryDistanceNm = 0.0;
};

struct LiveAuthorityPolygonStateSnapshot {
    LiveAuthorityAssignmentState state =
        LiveAuthorityAssignmentState::Offline;
    std::string canonicalPolygonId;
    std::string polygonKey;
    std::string authoritySource;
    AuthorityRelevanceKind kind = AuthorityRelevanceKind::Center;
    std::vector<std::string> controllerCallsigns;
};

struct LiveAuthorityMapSnapshot {
    bool available = false;
    bool stale = true;
    // False on a mechanical worker fact packet. Only the Brain may set this
    // after applying conflict, freshness, and publication decisions.
    bool brainDecisionsApplied = false;
    std::uint64_t stableHash = 0;
    std::uint64_t canonicalMapIdentity = 0;
    std::uint64_t controllerFeedGeneration = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t terminalBoundaryGeneration = 0;
    std::uint64_t dynamicOwnershipGeneration = 0;
    std::uint64_t polygonDefinitionCount = 0;
    std::uint64_t polygonPartCount = 0;
    std::uint64_t dataGapCount = 0;
    // Incremental-refresh audit only. These counters describe worker fact
    // processing; they never grant or suppress controller authority.
    std::uint64_t verifiedControllerFactCount = 0;
    std::uint64_t reusedControllerFactCount = 0;
    std::uint64_t removedControllerFactCount = 0;
    std::uint64_t verifiedDynamicOwnershipFactCount = 0;
    std::uint64_t reusedDynamicOwnershipFactCount = 0;
    std::vector<LiveAuthorityAssignmentSnapshot> assignments;
    std::vector<LiveAuthorityPolygonStateSnapshot> polygonStates;
    std::vector<std::string> conflictedPolygonIds;
    std::vector<std::string> unmappedControllerCallsigns;
    std::string statusLine;
};

struct AuthorityRelevanceSnapshot {
    bool available = false;
    bool stale = true;
    std::uint64_t controllerFeedGeneration = 0;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t terminalCoverageGeneration = 0;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string diagnosticWorkStage;
    double diagnosticWindowNm = 0.0;
    int diagnosticDeferredSectorCount = 0;
    std::string statusLine;
    std::vector<std::string> diagnostics;
    LiveAuthorityMapSnapshot liveMap;
    bool liveRelevantAuthoritiesBrainOwned = false;
    int droppedBeforeBrainControllers = 0;
    AuthorityRelevanceEvidenceSnapshot evidence;
    std::vector<RelevantAuthoritySnapshot> relevantAuthorities;
};

struct AirportSectorSnapshot {
    bool available = false;
    bool stale = true;
    bool hasCenterCoverageData = false;
    bool hasTerminalCoverageData = false;
    std::uint64_t centerBoundaryGeneration = 0;
    std::uint64_t authorityCatalogGeneration = 0;
    std::uint64_t terminalCoverageGeneration = 0;
    std::string diagnosticCacheStatus;
    std::string diagnosticReason;
    std::string airportIcao;
    std::string statusLine;
    std::vector<RouteSectorMatchSnapshot> coveringSectors;
};

enum class BoardSource {
    None,
    Departure,
    Arrival,
    Enroute,
};

enum class StationRole {
    Other,
    Delivery,
    Ground,
    Tower,
    Departure,
    Approach,
    Center,
    Atis,
    Ctaf,
    Unicom,
};

// Canonical Arrival frequency hierarchy shared by the display and Standby
// Assist. Countries without CTAF put UNICOM ahead of Tower; CTAF arrivals put
// Tower ahead of CTAF. Ground and Delivery always follow the arrival radio.
constexpr int ArrivalFrequencyHierarchyRank(
    StationRole role,
    bool unicomArrival) noexcept {
    switch (role) {
        case StationRole::Center:
            return 0;
        case StationRole::Approach:
        case StationRole::Departure:
            return 10;
        case StationRole::Tower:
            return unicomArrival ? 30 : 20;
        case StationRole::Ctaf:
            return 30;
        case StationRole::Unicom:
            return 20;
        case StationRole::Ground:
            return 40;
        case StationRole::Delivery:
            return 50;
        case StationRole::Atis:
            return 60;
        case StationRole::Other:
        default:
            return 70;
    }
}

enum class DisplayRelation {
    Unknown,
    CurrentPolygon,
    NextPolygon,
    ArrivalPrep,
    Filtered,
    Hidden,
};

// The sole Brain-owned authority classification for an APP/DEP candidate
// evaluated against a filed endpoint. Vote totals may still be retained in
// diagnostics, but they must never be used to derive this relationship.
enum class TerminalEndpointRelationship {
    NotApplicable,
    DirectEndpoint,
    ExplicitParent,
    NearbyOnly,
    Unrelated,
    Unknown,
};

struct BoardStationSnapshot {
    StationRole role = StationRole::Other;
    std::string callsign;
    std::string frequency;
    bool tuned = false;
    bool sectorActive = false;
    bool online = false;
    bool offline = false;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string polygonKey;
    TerminalEndpointRelationship terminalRelationship =
        TerminalEndpointRelationship::NotApplicable;
    std::string terminalRelationshipProof;
    std::string sourceEvidenceId;
    std::string sourceEvidenceType;
    std::string sourceEvidenceDomain;
    std::string sourceDecisionId;
    std::string sourceEvidenceLinkStatus;
    std::string sourceEvidenceMissingReason;
    std::string stableCompletionKey;
};

struct ModuleBoardSnapshot {
    bool available = false;
    BoardSource source = BoardSource::None;
    std::string airportIcao;
    std::vector<BoardStationSnapshot> stations;
};

struct FinalDisplayStationSnapshot {
    StationRole role = StationRole::Other;
    std::string callsign;
    std::string frequency;
    std::string annotation;
    bool tuned = false;
    bool next = false;
    bool standby = false;
    bool sectorActive = false;
    bool online = false;
    bool offline = false;
    bool hasRouteEntryDistance = false;
    double routeEntryDistanceNm = 0.0;
    std::string polygonKey;
    DisplayRelation displayRelation = DisplayRelation::Unknown;
    TerminalEndpointRelationship terminalRelationship =
        TerminalEndpointRelationship::NotApplicable;
    std::string terminalRelationshipProof;
    std::string sourceEvidenceId;
    std::string sourceEvidenceType;
    std::string sourceEvidenceDomain;
    std::string sourceDecisionId;
    std::string sourceEvidenceLinkStatus;
    std::string sourceEvidenceMissingReason;
    std::string displayDecisionId;
    std::string overlayCapDecisionId;
    std::string stableCompletionKey;
    std::string sourceOwnedStableCompletionKey;
    std::string generatedFallbackStableCompletionKey;
    bool sourceOwnedStableCompletionKeyPresent = false;
    bool sourceOwnedKeyMigrationReady = false;
    bool sourceOwnedKeyPlanContextAvailable = false;
    bool sourceOwnedKeyBehaviorConsumerEnabled = false;
};

struct FinalDisplaySnapshot {
    bool available = false;
    BoardSource source = BoardSource::None;
    std::string airportIcao;
    std::vector<FinalDisplayStationSnapshot> stations;
};

enum class OverlayTone {
    Normal,
    Active,
    Next,
};

struct OverlayTextLine {
    std::string text;
    OverlayTone tone = OverlayTone::Normal;
    bool centerFrequencySelectable = false;
    std::string centerCallsign;
    std::string centerFrequency;
    std::string centerStableKey;
    std::string centerPlanKey;
};

enum class OverlayVersionTone {
    Unknown,
    Current,
    UpdateAvailable,
    Error,
};

struct OverlayVersionSnapshot {
    std::string text;
    std::string alternateText;
    OverlayVersionTone tone = OverlayVersionTone::Unknown;
    bool rotateAlternate = false;
};

enum class OverlayNoticeSeverity {
    Info,
    Success,
    Warning,
    Error,
};

struct OverlayNoticeSnapshot {
    bool visible = false;
    OverlayNoticeSeverity severity = OverlayNoticeSeverity::Info;
    std::string title;
    std::vector<std::string> bodyLines;
    std::string dismissText = "Close";
    bool dismissible = true;
};

enum class OverlayUpdateStatus {
    Unknown,
    Checking,
    Current,
    Available,
    Failed,
};

struct OverlayUpdateSnapshot {
    std::string installedVersion;
    std::string latestVersion;
    std::string downloadPageUrl;
    std::string errorClass;
    OverlayUpdateStatus status = OverlayUpdateStatus::Unknown;
    bool critical = false;
    bool manualNoticeRequested = false;
    bool automaticNoticeRequested = false;
};

enum class RecommendationKind {
    None,
    Controller,
    DepartureCtaf,
    ArrivalCtaf,
};

struct RecommendationSnapshot {
    bool visible = false;
    bool satisfied = false;
    RecommendationKind kind = RecommendationKind::None;
    std::string line;
    std::vector<OverlayTextLine> lines;
    std::string airportIcao;
    std::string frequency;
};

struct ManualQuerySnapshot {
    bool visible = false;
    std::string line;
};

struct OverlayViewModel {
    OverlayMode mode = OverlayMode::Dormant;
    bool visible = false;
    std::string title;
    std::string headerRightText;
    RadioStateSnapshot radioState;
    OverlayVersionSnapshot version;
    OverlayNoticeSnapshot systemNotice;
    std::vector<OverlayTextLine> bodyLines;
    bool showMessageAcknowledge = false;
    bool showMessageRecall = false;
};

}  // namespace xvatsim::brain
