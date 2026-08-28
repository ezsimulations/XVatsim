#pragma once

#include <functional>
#include <memory>
#include <string>

#include "XVatsim/brain/BrainOwnedRuntime.h"

namespace xvatsim::modules::metar {

struct VatsimMetarJsonResult {
    bool accepted = false;
    std::string stationIcao;
    std::string rawMetar;
    std::string reason;
};

std::wstring BuildVatsimMetarRequestPath(const std::string& normalizedIcao);

VatsimMetarJsonResult ExtractVatsimMetarJson(
    const std::string& requestedIcao,
    const std::string& payload);

class VatsimMetarClient final : public brain::BrainMetarWorker {
public:
#if defined(XVATSIM_METAR_PROOF_FIXTURES)
    struct ProofEndpoint {
        std::wstring host;
        unsigned short port = 0;
        bool secure = false;
    };

    using CancellationProbe = std::function<bool()>;
    using InjectedTransport = std::function<brain::BrainMetarWorkerFact(
        const brain::BrainMetarWorkerRequest&,
        const CancellationProbe&)>;

    explicit VatsimMetarClient(InjectedTransport injectedTransport = {});
    explicit VatsimMetarClient(ProofEndpoint endpoint);
#else
    VatsimMetarClient();
#endif
    ~VatsimMetarClient() override;
    VatsimMetarClient(const VatsimMetarClient&) = delete;
    VatsimMetarClient& operator=(const VatsimMetarClient&) = delete;

    bool Start(const brain::BrainMetarWorkerRequest& request) override;
    bool TryHarvest(brain::BrainMetarWorkerFact* fact) override;
    bool IsRunning() const override;
    void CancelAndJoin() override;
    brain::BrainMetarWorkerShutdownSnapshot ShutdownSnapshot() const override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace xvatsim::modules::metar
