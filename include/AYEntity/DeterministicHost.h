#pragma once
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicLockstep.h>
#include <AYGameLoop/IGameLoop.h>
#include <AYModule/IModule.h>

namespace ayt::app { class IEngineHost; }
namespace ayt::scene { class Scene; }
namespace ayt::entity {
inline constexpr const char* kDeterministicHostService = "ayt.entity.DeterministicHost";
enum class DetHostMode { Live, Record, Replay };
enum class DetHostState { Detached, Ordinary, Running, Completed, Stopped, Faulted };
/// Host metadata is input acquisition context only; Sim consumes the encoded packet.
struct DetHostInputRequest {
    std::uint64_t tick = 0, hostSimTick = 0, hostFrameIndex = 0, inputFrameIndex = 0;
    std::uint32_t version = 1;
};
struct DetHostedSceneRecipe {
    DetSessionConfig config;
    DetHostMode mode = DetHostMode::Live;
    std::function<bool(DeterministicSession&)> configure;
    /// Called once per attempted live/record tick after step validation, never in Replay.
    /// Sample the Host's already-polled input and encode commands; native values cross here.
    /// Packet starts with the required tick/version; returning false stops before Sim writes.
    std::function<bool(const DetHostInputRequest&, DetTickInput&)> input;
    std::string replayPath;
    std::uint32_t checkpointInterval = 300;
    /// Opt-in fixed-roster networking in Live/Record; Replay consumes recorded inputs.
    /// Host owns advance; input samples one local contribution per session tick.
    std::optional<DetLockstepConfig> lockstep;
};
struct DetHostOptions {
    /// nullopt explicitly selects ordinary World Sim, e.g. for Edit/preview scenes.
    /// Exceptions/invalid recipes stop simulation; never silently fall back.
    std::function<std::optional<DetHostedSceneRecipe>(const scene::Scene&)> selectScene;
};

/** @brief Standard Entity/Host Scene binding for live, recorded and replayed Sim.
 * @note Link AYEntity::DeterminismHost. Attach to the standard Entity subsystem;
 * Core/Runtime modules and Host Scene/EventBus services must outlive this binding.
 * All public operations are owner-thread at an external frame boundary. Scene
 * changes close the old session before creating the new one; shutdown seals only
 * healthy recordings. Managed actors and callback state obey the session contract.
 * Use Host pause/step controls here; World clocks remain scheduling metadata.
 * The controlled Entity phase layer stays on the Host caller thread even with
 * parallel scheduling enabled; callbacks cannot reenter lifecycle/control.
 */
class DeterministicHostController {
public:
    DeterministicHostController();
    ~DeterministicHostController();
    DeterministicHostController(const DeterministicHostController&) = delete;
    DeterministicHostController& operator=(const DeterministicHostController&) = delete;
    /// Publish the borrowed service and attach one driver; conflicting bindings fail.
    /// A recipe failure keeps this binding Faulted until recovery or disconnect;
    /// the integration module disconnects automatically when installation fails.
    bool bind(app::IEngineHost&, DetHostOptions);
    /// Close/release the session, remove only this driver/service and unsubscribe.
    void disconnect() noexcept;
    /// Stop controlled Sim and seal a healthy recording; ordinary fallback needs disconnect.
    bool stop();
    /// Select/configure a fresh current Scene while paused; Record needs a fresh output path.
    bool restartCurrent();
    void pause();
    /// Faulted/stopped/completed sessions cannot silently resume; recover/restart first.
    bool resume();
    /// One standard Host fixed tick while paused, with normal input/replay validation.
    bool stepOnce();
    /// Live-only checkpoint restore, pauses first; Record history cannot be rewritten.
    bool restore(const DetSessionCheckpoint&);
    /// Replay-only verified seek; pauses first and resets the reader restart boundary.
    bool seek(std::uint64_t nextTick);
    /// Owner-thread ingress at an external frame boundary; authenticate member mapping first.
    /// Invalid packets return false; terminal protocol faults stop controlled simulation.
    bool receiveNetwork(std::uint32_t authenticatedMember, std::span<const std::uint8_t> packet);
    /// Retained outbound packets; application throttles transport/resends outside Sim.
    std::vector<std::vector<std::uint8_t>> networkPackets() const;
    /// Missing input/hash stalls this session tick while presentation continues.
    bool networkWaiting() const;
    bool networkSynchronized() const;
    std::vector<std::uint32_t> networkMissing() const;
    /// Admission/liveness policy is external; a lost fixed member faults this epoch.
    bool disconnectNetworkMember(std::uint32_t member);
    /// Live-only agreed checkpoint recovery, paused, with a strictly newer epoch.
    /// Every peer must agree the same checkpoint/epoch; no automatic state transfer.
    bool resetNetwork(const DetSessionCheckpoint&, std::uint32_t newEpoch);
    DetHostState state() const;
    /// Host diagnostic, or active session diagnostic (including checkpoint rejection).
    const std::string& error() const;
    std::optional<DetSessionCheckpoint> checkpoint() const;
    const DeterministicSession* session() const;
    std::string recordingPath() const;
    const std::optional<DetStateDifference>& difference() const;
private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
};

/// Resolve the borrowed, optional module-owned controller from this Host.
DeterministicHostController* deterministicHost(app::IEngineHost&) noexcept;
/** @brief Opt-in standard module node; requires AYEntity.Runtime and Host Scene services.
 * @note Configure selectScene before installation; install publishes the controller
 * through deterministicHost(host). Shutdown detaches before Runtime/Host teardown.
 */
class DeterministicHostIntegrationModule final : public module::IModule {
public:
    explicit DeterministicHostIntegrationModule(DetHostOptions);
    const module::ModuleDescriptor& descriptor() const noexcept override;
    module::ModuleResult install(module::IModuleContext&) override;
    void shutdown(module::IModuleContext&) noexcept override;
private:
    module::ModuleDescriptor _descriptor;
    DetHostOptions _options;
    DeterministicHostController _controller;
};
} // namespace ayt::entity
