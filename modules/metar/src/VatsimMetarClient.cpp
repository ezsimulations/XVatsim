#include "XVatsim/modules/metar/VatsimMetarClient.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include "XVatsim/brain/BrainMetarRuntime.h"
#include "XVatsim/modules/metar/VatsimMetarDecoder.h"

namespace xvatsim::modules::metar {

namespace {

constexpr wchar_t kUserAgent[] = L"XVatsim/2.0.1";
constexpr wchar_t kHost[] = L"metar.vatsim.net";
constexpr std::size_t kMaxPayloadBytes = 65'536;
constexpr int kResolveTimeoutMs = 1'000;
constexpr int kConnectTimeoutMs = 2'000;
constexpr int kSendTimeoutMs = 2'000;
constexpr int kReceiveTimeoutMs = 5'000;
constexpr int kTotalRequestTimeoutMs = 10'000;

long long MonotonicMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

long long ElapsedMicroseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
}

DWORD RemainingTimeoutMs(
    std::chrono::steady_clock::time_point start,
    DWORD phaseLimitMs) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    const auto remaining = std::max<long long>(
        0, kTotalRequestTimeoutMs - elapsed);
    return static_cast<DWORD>(std::min<long long>(phaseLimitMs, remaining));
}

struct AsyncContext {
    HANDLE cancelEvent = nullptr;
    HANDLE phaseEvent = nullptr;
    HANDLE closingEvent = nullptr;
    std::atomic<DWORD> status{0};
    std::atomic<DWORD_PTR> operationResult{0};
    std::atomic<DWORD> error{0};
    std::atomic<DWORD> available{0};
    std::atomic<DWORD> transferred{0};
};

void CALLBACK WinHttpCallback(
    HINTERNET,
    DWORD_PTR contextValue,
    DWORD status,
    LPVOID statusInformation,
    DWORD statusInformationLength) {
    auto* context = reinterpret_cast<AsyncContext*>(contextValue);
    if (context == nullptr) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        SetEvent(context->closingEvent);
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        if (statusInformation != nullptr &&
            statusInformationLength >= sizeof(WINHTTP_ASYNC_RESULT)) {
            const auto* result =
                static_cast<const WINHTTP_ASYNC_RESULT*>(statusInformation);
            context->operationResult.store(
                result->dwResult, std::memory_order_release);
            context->error.store(result->dwError, std::memory_order_release);
        } else {
            context->error.store(ERROR_GEN_FAILURE, std::memory_order_release);
        }
        context->status.store(status, std::memory_order_release);
        SetEvent(context->phaseEvent);
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE &&
        statusInformation != nullptr && statusInformationLength >= sizeof(DWORD)) {
        context->available.store(
            *static_cast<const DWORD*>(statusInformation),
            std::memory_order_release);
    }
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        context->transferred.store(statusInformationLength,
                                   std::memory_order_release);
    }
    if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
        status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
        status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE ||
        status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        context->status.store(status, std::memory_order_release);
        SetEvent(context->phaseEvent);
    }
}

enum class WaitResult {
    Completed,
    Cancelled,
    Failed,
    TimedOut,
};

void PreparePhase(AsyncContext* context) {
    ResetEvent(context->phaseEvent);
    context->status.store(0, std::memory_order_release);
    context->operationResult.store(0, std::memory_order_release);
    context->error.store(0, std::memory_order_release);
    context->available.store(0, std::memory_order_release);
    context->transferred.store(0, std::memory_order_release);
}

WaitResult WaitForPhase(
    AsyncContext* context,
    DWORD expectedStatus,
    DWORD timeoutMs) {
    HANDLE handles[]{context->cancelEvent, context->phaseEvent};
    const auto wait = WaitForMultipleObjects(2, handles, FALSE, timeoutMs);
    if (wait == WAIT_OBJECT_0) return WaitResult::Cancelled;
    if (wait == WAIT_TIMEOUT) return WaitResult::TimedOut;
    if (wait != WAIT_OBJECT_0 + 1) return WaitResult::Failed;
    if (context->error.load(std::memory_order_acquire) != 0 ||
        context->status.load(std::memory_order_acquire) != expectedStatus) {
        return WaitResult::Failed;
    }
    return WaitResult::Completed;
}

void CloseHandleIfPresent(HINTERNET* handle) {
    if (handle != nullptr && *handle != nullptr) {
        WinHttpCloseHandle(*handle);
        *handle = nullptr;
    }
}

brain::BrainMetarWorkerFact FailureFact(
    const brain::BrainMetarWorkerRequest& request,
    brain::BrainMetarWorkerStatus status,
    std::string diagnostic,
    std::chrono::steady_clock::time_point start,
    brain::BrainMetarTransportStage terminalStage =
        brain::BrainMetarTransportStage::Startup,
    brain::BrainMetarWinHttpOperation operation =
        brain::BrainMetarWinHttpOperation::None,
    std::uint64_t operationResult = 0,
    std::uint32_t error = 0,
    std::uint32_t progress = brain::BrainMetarTransportProgressNone) {
    brain::BrainMetarWorkerFact fact;
    fact.request = request;
    fact.status = status;
    fact.diagnostic = std::move(diagnostic);
    fact.terminalStage = terminalStage;
    fact.winHttpOperation = operation;
    fact.winHttpResult = operationResult;
    fact.winHttpError = error;
    fact.transportProgress = progress;
    fact.completedMonotonicMs = MonotonicMilliseconds();
    fact.networkElapsedUs = ElapsedMicroseconds(start);
    return fact;
}

std::uint32_t WaitError(
    WaitResult wait,
    const AsyncContext& context) {
    if (wait == WaitResult::Cancelled) return ERROR_OPERATION_ABORTED;
    if (wait == WaitResult::TimedOut) return ERROR_TIMEOUT;
    return context.error.load(std::memory_order_acquire);
}

brain::BrainMetarDecodeStatus ToBrainDecodeStatus(
    VatsimMetarDecodeStatus status) {
    switch (status) {
        case VatsimMetarDecodeStatus::Decoded:
            return brain::BrainMetarDecodeStatus::Decoded;
        case VatsimMetarDecodeStatus::MalformedJson:
            return brain::BrainMetarDecodeStatus::MalformedJson;
        case VatsimMetarDecodeStatus::RootTypeMismatch:
            return brain::BrainMetarDecodeStatus::RootTypeMismatch;
        case VatsimMetarDecodeStatus::ResourceFailure:
            return brain::BrainMetarDecodeStatus::ResourceFailure;
        case VatsimMetarDecodeStatus::NotAttempted:
            break;
    }
    return brain::BrainMetarDecodeStatus::NotAttempted;
}

brain::BrainMetarDecodedFieldType ToBrainFieldType(
    VatsimMetarJsonFieldType type) {
    return static_cast<brain::BrainMetarDecodedFieldType>(
        static_cast<int>(type));
}

}  // namespace

std::wstring BuildVatsimMetarRequestPath(const std::string& normalizedIcao) {
    std::string checked;
    if (!brain::NormalizeStrictMetarIcao(normalizedIcao, &checked)) return {};
    return L"/" + std::wstring(checked.begin(), checked.end()) +
        L"?format=json";
}

struct VatsimMetarClient::Implementation {
#if defined(XVATSIM_METAR_PROOF_FIXTURES)
    explicit Implementation(InjectedTransport transport)
        : injectedTransport(std::move(transport)) {
        cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    explicit Implementation(ProofEndpoint endpoint)
        : proofEndpoint(std::move(endpoint)) {
#else
    Implementation() {
#endif
        cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~Implementation() {
        if (cancelEvent != nullptr) CloseHandle(cancelEvent);
    }

    brain::BrainMetarWorkerFact Fetch(
        const brain::BrainMetarWorkerRequest& request) {
        const auto started = std::chrono::steady_clock::now();
        std::string normalized;
        const auto path = BuildVatsimMetarRequestPath(request.airportIcao);
        if (!brain::NormalizeStrictMetarIcao(request.airportIcao, &normalized) ||
            path.empty()) {
            return FailureFact(request,
                brain::BrainMetarWorkerStatus::InvalidRequest,
                "strict-icao-rejected", started);
        }
#if defined(XVATSIM_METAR_PROOF_FIXTURES)
        if (injectedTransport) {
            auto fact = injectedTransport(request, [this]() {
                return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
            });
            if (fact.completedMonotonicMs == 0) {
                fact.completedMonotonicMs = MonotonicMilliseconds();
            }
            if (fact.networkElapsedUs == 0) {
                fact.networkElapsedUs = ElapsedMicroseconds(started);
            }
            return fact;
        }
#endif

        AsyncContext context;
        context.cancelEvent = cancelEvent;
        context.phaseEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        context.closingEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (context.phaseEvent == nullptr || context.closingEvent == nullptr) {
            const auto error = GetLastError();
            if (context.phaseEvent != nullptr) CloseHandle(context.phaseEvent);
            if (context.closingEvent != nullptr) CloseHandle(context.closingEvent);
            return FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "event-creation-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::CreateEventHandle,
                FALSE, error);
        }

        std::uint32_t transportProgress =
            brain::BrainMetarTransportProgressNone;

        HINTERNET session = WinHttpOpen(
            kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
            WINHTTP_FLAG_ASYNC);
        HINTERNET connection = nullptr;
        HINTERNET requestHandle = nullptr;
        bool callbackRegistered = false;
        auto finish = [&](brain::BrainMetarWorkerFact fact) {
            const auto active = activeRequest.exchange(
                nullptr, std::memory_order_acq_rel);
            if (requestHandle != nullptr) {
                if (active == requestHandle || !callbackRegistered) {
                    if (callbackRegistered) ResetEvent(context.closingEvent);
                    WinHttpCloseHandle(requestHandle);
                }
                requestHandle = nullptr;
                if (callbackRegistered) {
                    callbacksClosed.store(
                        WaitForSingleObject(context.closingEvent, 450) ==
                            WAIT_OBJECT_0,
                        std::memory_order_release);
                }
            }
            CloseHandleIfPresent(&connection);
            CloseHandleIfPresent(&session);
            CloseHandle(context.phaseEvent);
            CloseHandle(context.closingEvent);
            handlesClosed.store(true, std::memory_order_release);
            return fact;
        };
        if (session == nullptr) {
            const auto error = GetLastError();
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "session-open-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::OpenSession,
                FALSE, error));
        }
        if (!WinHttpSetTimeouts(
                session, kResolveTimeoutMs, kConnectTimeoutMs,
                kSendTimeoutMs, kReceiveTimeoutMs)) {
            const auto error = GetLastError();
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "timeout-configuration-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::ConfigureTimeouts,
                FALSE, error));
        }
        const wchar_t* requestHost = kHost;
        INTERNET_PORT requestPort = INTERNET_DEFAULT_HTTPS_PORT;
        DWORD requestFlags = WINHTTP_FLAG_SECURE;
#if defined(XVATSIM_METAR_PROOF_FIXTURES)
        if (!proofEndpoint.host.empty() && proofEndpoint.port != 0) {
            requestHost = proofEndpoint.host.c_str();
            requestPort = proofEndpoint.port;
            requestFlags = proofEndpoint.secure ? WINHTTP_FLAG_SECURE : 0;
        }
#endif
        connection = WinHttpConnect(session, requestHost, requestPort, 0);
        if (connection == nullptr) {
            const auto error = GetLastError();
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "connect-handle-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::Connect,
                FALSE, error));
        }
        requestHandle = WinHttpOpenRequest(
            connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
        if (requestHandle == nullptr) {
            const auto error = GetLastError();
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "request-handle-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::OpenRequest,
                FALSE, error));
        }
        callbacksClosed.store(false, std::memory_order_release);
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(requestHandle, WINHTTP_OPTION_REDIRECT_POLICY,
                              &redirectPolicy, sizeof(redirectPolicy))) {
            const auto error = GetLastError();
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "redirect-policy-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::ConfigureRedirects,
                FALSE, error));
        }
        const auto callback = WinHttpSetStatusCallback(
            requestHandle, WinHttpCallback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
                WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
                WINHTTP_CALLBACK_FLAG_DATA_AVAILABLE |
                WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
                WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
                WINHTTP_CALLBACK_FLAG_HANDLES,
            0);
        if (callback == WINHTTP_INVALID_STATUS_CALLBACK) {
            const auto error = GetLastError();
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "callback-registration-failed", started,
                brain::BrainMetarTransportStage::Startup,
                brain::BrainMetarWinHttpOperation::RegisterCallback,
                FALSE, error));
        }
        callbackRegistered = true;
        activeRequest.store(requestHandle, std::memory_order_release);

        PreparePhase(&context);
        const auto sendResult = WinHttpSendRequest(
            requestHandle, L"Accept: application/json\r\n", -1L,
            WINHTTP_NO_REQUEST_DATA, 0, 0,
            reinterpret_cast<DWORD_PTR>(&context));
        const auto sendError = sendResult ? ERROR_SUCCESS : GetLastError();
        if (!sendResult && sendError != ERROR_IO_PENDING) {
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "send-start-failed", started,
                brain::BrainMetarTransportStage::SendStart,
                brain::BrainMetarWinHttpOperation::SendRequest,
                FALSE, sendError, transportProgress));
        }
        auto wait = WaitForPhase(
            &context, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,
            RemainingTimeoutMs(
                started,
                kResolveTimeoutMs + kConnectTimeoutMs + kSendTimeoutMs));
        if (wait != WaitResult::Completed) {
            return finish(FailureFact(request,
                wait == WaitResult::Cancelled
                    ? brain::BrainMetarWorkerStatus::Cancelled
                    : brain::BrainMetarWorkerStatus::TransportFailure,
                wait == WaitResult::Cancelled
                    ? "cancelled-send-completion"
                    : wait == WaitResult::TimedOut
                        ? "send-completion-timeout"
                        : "send-completion-failed",
                started,
                brain::BrainMetarTransportStage::SendCompletion,
                brain::BrainMetarWinHttpOperation::SendRequest,
                context.operationResult.load(std::memory_order_acquire),
                WaitError(wait, context), transportProgress));
        }
        transportProgress |= brain::BrainMetarSendCompletionObserved;

        PreparePhase(&context);
        const auto receiveResult = WinHttpReceiveResponse(requestHandle, nullptr);
        const auto receiveError = receiveResult ? ERROR_SUCCESS : GetLastError();
        if (!receiveResult && receiveError != ERROR_IO_PENDING) {
            return finish(FailureFact(request,
                brain::BrainMetarWorkerStatus::TransportFailure,
                "receive-start-failed", started,
                brain::BrainMetarTransportStage::ReceiveStart,
                brain::BrainMetarWinHttpOperation::ReceiveResponse,
                FALSE, receiveError, transportProgress));
        }
        wait = WaitForPhase(
            &context, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,
            RemainingTimeoutMs(started, kReceiveTimeoutMs));
        if (wait != WaitResult::Completed) {
            return finish(FailureFact(request,
                wait == WaitResult::Cancelled
                    ? brain::BrainMetarWorkerStatus::Cancelled
                    : brain::BrainMetarWorkerStatus::TransportFailure,
                wait == WaitResult::Cancelled
                    ? "cancelled-response-headers"
                    : wait == WaitResult::TimedOut
                        ? "response-headers-timeout"
                        : "response-headers-failed",
                started,
                brain::BrainMetarTransportStage::ResponseHeaders,
                brain::BrainMetarWinHttpOperation::ReceiveResponse,
                context.operationResult.load(std::memory_order_acquire),
                WaitError(wait, context), transportProgress));
        }
        transportProgress |= brain::BrainMetarResponseHeadersReceived;

        DWORD httpStatus = 0;
        DWORD httpStatusSize = sizeof(httpStatus);
        const auto headerResult = WinHttpQueryHeaders(
                requestHandle,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &httpStatus, &httpStatusSize,
                WINHTTP_NO_HEADER_INDEX);
        const auto headerError = headerResult ? ERROR_SUCCESS : GetLastError();
        if (!headerResult || httpStatus != 200) {
            auto fact = FailureFact(request,
                brain::BrainMetarWorkerStatus::HttpFailure,
                headerResult ? "http-status-rejected" : "header-query-failed",
                started,
                headerResult
                    ? brain::BrainMetarTransportStage::HttpStatus
                    : brain::BrainMetarTransportStage::ResponseHeaders,
                brain::BrainMetarWinHttpOperation::QueryHeaders,
                headerResult, headerError, transportProgress);
            fact.httpStatus = static_cast<int>(httpStatus);
            return finish(std::move(fact));
        }
        transportProgress |= brain::BrainMetarHttp200Accepted;

        std::string payload;
        for (;;) {
            PreparePhase(&context);
            const auto queryResult =
                WinHttpQueryDataAvailable(requestHandle, nullptr);
            const auto queryError = queryResult ? ERROR_SUCCESS : GetLastError();
            if (!queryResult && queryError != ERROR_IO_PENDING) {
                auto fact = FailureFact(request,
                    brain::BrainMetarWorkerStatus::TransportFailure,
                    "data-availability-start-failed", started,
                    brain::BrainMetarTransportStage::DataAvailability,
                    brain::BrainMetarWinHttpOperation::QueryDataAvailable,
                    FALSE, queryError, transportProgress);
                fact.httpStatus = 200;
                fact.payloadBytes = payload.size();
                return finish(std::move(fact));
            }
            wait = WaitForPhase(
                &context, WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,
                RemainingTimeoutMs(started, kReceiveTimeoutMs));
            if (wait != WaitResult::Completed) {
                auto fact = FailureFact(request,
                    wait == WaitResult::Cancelled
                        ? brain::BrainMetarWorkerStatus::Cancelled
                        : brain::BrainMetarWorkerStatus::TransportFailure,
                    wait == WaitResult::Cancelled
                        ? "cancelled-data-availability"
                        : wait == WaitResult::TimedOut
                            ? "data-availability-timeout"
                            : "data-availability-failed",
                    started,
                    brain::BrainMetarTransportStage::DataAvailability,
                    brain::BrainMetarWinHttpOperation::QueryDataAvailable,
                    context.operationResult.load(std::memory_order_acquire),
                    WaitError(wait, context), transportProgress);
                fact.httpStatus = 200;
                fact.payloadBytes = payload.size();
                return finish(std::move(fact));
            }
            const auto available = context.available.load(std::memory_order_acquire);
            if (available == 0) break;
            if (payload.size() + available > kMaxPayloadBytes) {
                auto fact = FailureFact(request,
                    brain::BrainMetarWorkerStatus::PayloadRejected,
                    "payload-too-large", started,
                    brain::BrainMetarTransportStage::PayloadValidation,
                    brain::BrainMetarWinHttpOperation::QueryDataAvailable,
                    TRUE, ERROR_SUCCESS, transportProgress);
                fact.httpStatus = 200;
                fact.payloadBytes = payload.size() + available;
                return finish(std::move(fact));
            }
            std::vector<char> buffer(available);
            PreparePhase(&context);
            const auto readResult = WinHttpReadData(
                requestHandle, buffer.data(), available, nullptr);
            const auto readError = readResult ? ERROR_SUCCESS : GetLastError();
            if (!readResult && readError != ERROR_IO_PENDING) {
                auto fact = FailureFact(request,
                    brain::BrainMetarWorkerStatus::TransportFailure,
                    "read-start-failed", started,
                    brain::BrainMetarTransportStage::Read,
                    brain::BrainMetarWinHttpOperation::ReadData,
                    FALSE, readError, transportProgress);
                fact.httpStatus = 200;
                fact.payloadBytes = payload.size();
                return finish(std::move(fact));
            }
            wait = WaitForPhase(
                &context, WINHTTP_CALLBACK_STATUS_READ_COMPLETE,
                RemainingTimeoutMs(started, kReceiveTimeoutMs));
            if (wait != WaitResult::Completed) {
                auto fact = FailureFact(request,
                    wait == WaitResult::Cancelled
                        ? brain::BrainMetarWorkerStatus::Cancelled
                        : brain::BrainMetarWorkerStatus::TransportFailure,
                    wait == WaitResult::Cancelled
                        ? "cancelled-read"
                        : wait == WaitResult::TimedOut
                            ? "read-timeout" : "read-failed",
                    started,
                    brain::BrainMetarTransportStage::Read,
                    brain::BrainMetarWinHttpOperation::ReadData,
                    context.operationResult.load(std::memory_order_acquire),
                    WaitError(wait, context), transportProgress);
                fact.httpStatus = 200;
                fact.payloadBytes = payload.size();
                return finish(std::move(fact));
            }
            const auto transferred =
                context.transferred.load(std::memory_order_acquire);
            if (transferred > available ||
                payload.size() + transferred > kMaxPayloadBytes) {
                auto fact = FailureFact(request,
                    brain::BrainMetarWorkerStatus::PayloadRejected,
                    "payload-bound-rejected", started,
                    brain::BrainMetarTransportStage::PayloadValidation,
                    brain::BrainMetarWinHttpOperation::ReadData,
                    TRUE, ERROR_SUCCESS, transportProgress);
                fact.httpStatus = 200;
                fact.payloadBytes = payload.size() + transferred;
                return finish(std::move(fact));
            }
            payload.append(buffer.data(), transferred);
        }
        transportProgress |= brain::BrainMetarPayloadReadComplete;

        const auto decoded = DecodeVatsimMetarPayload(payload);
        if (decoded.decodeStatus == VatsimMetarDecodeStatus::Decoded) {
            transportProgress |= brain::BrainMetarJsonDecoded;
        }
        brain::BrainMetarWorkerFact fact;
        fact.status = brain::BrainMetarWorkerStatus::Success;
        fact.request = request;
        fact.decodeStatus = ToBrainDecodeStatus(decoded.decodeStatus);
        fact.reportCardinality = decoded.reportCardinality;
        fact.reportCardinalityLimitExceeded =
            decoded.reportCardinalityLimitExceeded;
        fact.decodingElapsedMicroseconds =
            decoded.decodingElapsedMicroseconds;
        fact.decodeDiagnostic = decoded.diagnostic;
        if (decoded.soleReport.has_value()) {
            const auto& fields = decoded.soleReport.value();
            fact.stationIcao = fields.returnedStation;
            fact.rawMetar = fields.rawMetar;
            fact.stationFieldType = ToBrainFieldType(fields.stationFieldType);
            fact.metarFieldType = ToBrainFieldType(fields.metarFieldType);
            fact.stationFieldMissing = fields.stationFieldMissing;
            fact.metarFieldMissing = fields.metarFieldMissing;
            fact.stationFieldMalformed = fields.stationFieldMalformed;
            fact.metarFieldMalformed = fields.metarFieldMalformed;
            fact.stationByteLimitExceeded = fields.stationByteLimitExceeded;
            fact.rawMetarByteLimitExceeded =
                fields.rawMetarByteLimitExceeded;
            fact.rawMetarContainsNul = fields.rawMetarContainsNul;
            fact.rawMetarHasLeadingWhitespace =
                fields.rawMetarHasLeadingWhitespace;
            fact.rawMetarHasTrailingWhitespace =
                fields.rawMetarHasTrailingWhitespace;
        }
        fact.httpStatus = 200;
        fact.completedMonotonicMs = MonotonicMilliseconds();
        fact.networkElapsedUs = ElapsedMicroseconds(started);
        fact.payloadBytes = payload.size();
        fact.terminalStage = brain::BrainMetarTransportStage::Completed;
        fact.winHttpOperation =
            brain::BrainMetarWinHttpOperation::ReadData;
        fact.winHttpResult = TRUE;
        fact.transportProgress = transportProgress;
        fact.diagnostic = "vatsim-http-response-decoded";
        return finish(std::move(fact));
    }

#if defined(XVATSIM_METAR_PROOF_FIXTURES)
    InjectedTransport injectedTransport;
    ProofEndpoint proofEndpoint;
#endif
    HANDLE cancelEvent = nullptr;
    mutable std::mutex mutex;
    std::thread worker;
    std::atomic<bool> running{false};
    bool resultReady = false;
    brain::BrainMetarWorkerFact result;
    std::atomic<bool> handlesClosed{true};
    std::atomic<bool> callbacksClosed{true};
    std::atomic<HINTERNET> activeRequest{nullptr};
    std::atomic<long long> lastShutdownLatencyMs{0};
};

#if defined(XVATSIM_METAR_PROOF_FIXTURES)
VatsimMetarClient::VatsimMetarClient(InjectedTransport injectedTransport)
    : implementation_(
          std::make_unique<Implementation>(std::move(injectedTransport))) {}

VatsimMetarClient::VatsimMetarClient(ProofEndpoint endpoint)
    : implementation_(
          std::make_unique<Implementation>(std::move(endpoint))) {}
#else
VatsimMetarClient::VatsimMetarClient()
    : implementation_(std::make_unique<Implementation>()) {}
#endif

VatsimMetarClient::~VatsimMetarClient() {
    CancelAndJoin();
}

bool VatsimMetarClient::Start(
    const brain::BrainMetarWorkerRequest& request) {
    auto& impl = *implementation_;
    if (impl.running.load(std::memory_order_acquire)) return false;
    if (impl.worker.joinable()) impl.worker.join();
    ResetEvent(impl.cancelEvent);
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        impl.resultReady = false;
        impl.result = {};
    }
    impl.handlesClosed.store(false, std::memory_order_release);
    impl.callbacksClosed.store(true, std::memory_order_release);
    impl.running.store(true, std::memory_order_release);
    try {
        impl.worker = std::thread([&impl, request]() {
            auto fact = impl.Fetch(request);
            {
                std::lock_guard<std::mutex> lock(impl.mutex);
                impl.result = std::move(fact);
                impl.resultReady = true;
            }
            impl.handlesClosed.store(true, std::memory_order_release);
            impl.running.store(false, std::memory_order_release);
        });
    } catch (...) {
        impl.running.store(false, std::memory_order_release);
        impl.handlesClosed.store(true, std::memory_order_release);
        return false;
    }
    return true;
}

bool VatsimMetarClient::TryHarvest(brain::BrainMetarWorkerFact* fact) {
    if (fact == nullptr) return false;
    auto& impl = *implementation_;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        if (!impl.resultReady) return false;
        *fact = impl.result;
        impl.resultReady = false;
    }
    if (impl.worker.joinable()) impl.worker.join();
    return true;
}

bool VatsimMetarClient::IsRunning() const {
    return implementation_->running.load(std::memory_order_acquire);
}

void VatsimMetarClient::CancelAndJoin() {
    auto& impl = *implementation_;
    const auto started = std::chrono::steady_clock::now();
    if (impl.cancelEvent != nullptr) SetEvent(impl.cancelEvent);
    const auto active = impl.activeRequest.exchange(
        nullptr, std::memory_order_acq_rel);
    if (active != nullptr) WinHttpCloseHandle(active);
    if (impl.worker.joinable()) impl.worker.join();
    impl.running.store(false, std::memory_order_release);
    impl.handlesClosed.store(true, std::memory_order_release);
    impl.lastShutdownLatencyMs.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count(),
        std::memory_order_release);
}

brain::BrainMetarWorkerShutdownSnapshot
VatsimMetarClient::ShutdownSnapshot() const {
    brain::BrainMetarWorkerShutdownSnapshot snapshot;
    snapshot.lastShutdownLatencyMs =
        implementation_->lastShutdownLatencyMs.load(std::memory_order_acquire);
    snapshot.running = implementation_->running.load(std::memory_order_acquire);
    snapshot.handlesClosed =
        implementation_->handlesClosed.load(std::memory_order_acquire);
    snapshot.callbacksClosed =
        implementation_->callbacksClosed.load(std::memory_order_acquire);
    return snapshot;
}

}  // namespace xvatsim::modules::metar
