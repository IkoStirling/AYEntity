#include <AYEntity/DeterministicHost.h>
#include <AYEntity/EntitySimulationDriver.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYApplication/IEngineHost.h>
#include <AYApplication/EngineModuleContext.h>
#include <AYEventSystem/EventBus.h>
#include <AYEventSystem/Events/SceneEvents.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <AYReplay/FileReplayRecorder.h>
#include <filesystem>

namespace ayt::entity {
struct DeterministicHostController::Impl final : IEntitySimulationDriver {
    app::IEngineHost* host = nullptr;
    game::ISubSystem* adapter = nullptr;
    DetHostOptions options;
    scene::Scene* sceneKey = nullptr;
    World* world = nullptr;
    bool resolved = false, dispatching = false;
    DetHostState state = DetHostState::Detached;
    std::string error, lastRecordingPath;
    std::optional<DetHostedSceneRecipe> recipe;
    std::optional<game::FixedTimestep> step;
    std::optional<std::uint64_t> lastHostTick;
    std::unique_ptr<DeterministicSession> session;
    std::unique_ptr<DetReplayWriter> writer;
    std::unique_ptr<DetReplayReader> reader;
    std::unique_ptr<DetRollbackReplayWriter> rollbackWriter;
    std::unique_ptr<DetRollbackReplayReader> rollbackReader;
    std::unique_ptr<DeterministicLockstep> peers;
    std::unique_ptr<DeterministicRollbackNetwork> rollback;
    std::optional<std::uint64_t> submittedTick;
    bool networkBlocked = false, manualPause = false;
    std::vector<event::ConnectionId> subscriptions;
    std::optional<DetStateDifference> difference;

    bool fail(std::string message) {
        error = std::move(message); state = DetHostState::Faulted;
        if (host) host->gameLoop().pause();
        return false;
    }
    bool matches(const std::optional<game::FixedTimestep>& actual) const {
        return actual && actual->isRational() && step
            && actual->numerator() == step->numerator()
            && actual->denominator() == step->denominator()
            && actual->deltaTime().bits() == step->deltaTime().bits();
    }
    bool close() noexcept {
        bool ok = true;
        try {
            if (rollbackWriter && state == DetHostState::Running && !session->faulted()) {
                if (!rollbackWriter->finish(*rollback)) { fail(rollbackWriter->error()); ok = false; }
            }
            if (writer && state == DetHostState::Running && !session->faulted()) {
                if (!writer->finish()) { fail(writer->error()); ok = false; }
            }
            rollbackWriter.reset(); rollbackReader.reset(); rollback.reset(); peers.reset(); submittedTick.reset(); networkBlocked = false; writer.reset(); reader.reset(); session.reset(); recipe.reset(); step.reset();
            if (world && world->_hostedSimulationObserver == this)
                world->_hostedSimulationObserver = nullptr;
            world = nullptr; lastHostTick.reset();
            if (state != DetHostState::Faulted) state = DetHostState::Stopped;
        } catch (...) {
            // Destruction still releases ownership before World storage disappears.
            rollbackWriter.reset(); rollbackReader.reset(); rollback.reset(); peers.reset(); submittedTick.reset(); networkBlocked = false; writer.reset(); reader.reset(); session.reset(); recipe.reset(); step.reset();
            if (world && world->_hostedSimulationObserver == this)
                world->_hostedSimulationObserver = nullptr;
            world = nullptr; state = DetHostState::Faulted; ok = false;
        }
        return ok;
    }
    bool sync(World& dispatched) {
        if (!host) return false;
        auto* current = host->scenes()->current();
        if (resolved && current == sceneKey) {
            if (world && world != &dispatched) return fail("Host Scene and active World disagree");
            return state != DetHostState::Faulted;
        }
        if (!close()) return false;
        sceneKey = current; resolved = true; difference.reset(); error.clear();
        if (!current) { state = DetHostState::Ordinary; return true; }
        if (&current->world() != &dispatched) return fail("Host Scene and active World disagree");
        try {
            recipe = options.selectScene(*current);
            if (!recipe) { state = DetHostState::Ordinary; return true; }
            state = DetHostState::Stopped;
            world = &dispatched;
            if (world->_hostedSimulationObserver)
                return fail("World already has a Host simulation observer");
            world->_hostedSimulationObserver = this;
            if (!recipe->configure) return fail("Scene recipe needs a configuration callback");
            if (recipe->lockstep && recipe->mode == DetHostMode::Replay)
                return fail("Replay cannot install a live network owner");
            if (recipe->rollback && (recipe->lockstep || recipe->mode == DetHostMode::Replay))
                return fail("Rollback requires Live/Record mode and one exclusive network owner");
            if (recipe->rollbackArchive && (recipe->mode != DetHostMode::Record || !recipe->rollback))
                return fail("Indexed archive options require Record+rollback");
            step = game::FixedTimestep::fromRatio(recipe->config.stepNumerator, recipe->config.stepDenominator);
            if (!step || !matches(host->gameLoop().getFixedStep()))
                return fail("Host/session rational fixed step mismatch");
            recipe->config.stepNumerator = step->numerator();
            recipe->config.stepDenominator = step->denominator();
            session = std::make_unique<DeterministicSession>(*world, recipe->config);
            if (!recipe->configure(*session) || !session->seal())
                return fail(session->error().empty() ? "Scene configuration rejected" : session->error());
            if (recipe->mode == DetHostMode::Record && !recipe->rollback) {
                auto base = recipe->replayPath;
                if (base.empty()) return fail("Recording path is empty");
                if (!std::filesystem::path(base).has_extension()) base += ".rpl";
                const auto path = replay::FileReplayRecorder::rotationPathFor(base, 0);
                if (std::filesystem::exists(path)) return fail("Recording destination already exists");
                writer = std::make_unique<DetReplayWriter>();
                if (!writer->begin(*session, base, recipe->checkpointInterval)) return fail(writer->error());
                lastRecordingPath = writer->path();
            } else if (recipe->mode == DetHostMode::Replay) {
                reader = std::make_unique<DetReplayReader>();
                if (!reader->open(recipe->replayPath)) {
                    rollbackReader = std::make_unique<DetRollbackReplayReader>();
                    if (!rollbackReader->open(recipe->replayPath) || !rollbackReader->restoreInitial(*session))
                        return fail(rollbackReader->error());
                    reader.reset();
                } else if (!reader->restoreInitial(*session)) return fail(reader->error());
            }
            if (recipe->lockstep) {
                peers = std::make_unique<DeterministicLockstep>(*session, *recipe->lockstep,
                    writer ? DeterministicLockstep::TickSink{[this](auto input) {
                        return writer->advance(std::move(input));
                    }} : DeterministicLockstep::TickSink{});
            }
            if (recipe->rollback) {
                rollback = std::make_unique<DeterministicRollbackNetwork>(*session, *recipe->rollback);
                if (recipe->mode == DetHostMode::Record) {
                    rollbackWriter = std::make_unique<DetRollbackReplayWriter>();
                    const bool opened = recipe->rollbackArchive
                        ? rollbackWriter->begin(*rollback, recipe->replayPath, *recipe->rollbackArchive, recipe->checkpointInterval)
                        : rollbackWriter->begin(*rollback, recipe->replayPath, recipe->checkpointInterval);
                    if (!opened) return fail(rollbackWriter->error());
                    lastRecordingPath = rollbackWriter->path();
                }
            }
            state = ((reader && reader->atEnd()) || (rollbackReader && rollbackReader->atEnd())) ? DetHostState::Completed : DetHostState::Running;
            if (state == DetHostState::Completed) host->gameLoop().pause();
            return true;
        } catch (const std::exception& e) { return fail(e.what()); }
        catch (...) { return fail("Scene selection/configuration threw"); }
    }
    std::optional<bool> fixedTick(World& dispatched, const game::FrameContext& context) override {
        if (dispatching) return fail("Reentrant Host simulation tick");
        struct Guard { bool& active; ~Guard() { active = false; } } guard{dispatching};
        dispatching = true;
        if (!sync(dispatched)) return false;
        if (state == DetHostState::Ordinary) return std::nullopt;
        if (state != DetHostState::Running || !session) {
            if (host) host->gameLoop().pause();
            return false;
        }
        if (!matches(context.fixedStep)) return fail("Host fixed step changed during the session");
        if (lastHostTick && context.simTick <= *lastHostTick)
            return fail("Duplicate/nonmonotonic Host fixed tick");
        try {
            bool ok;
            if (rollbackReader) {
                ok = rollbackReader->advance(*session); difference = rollbackReader->difference();
                if (!ok) return fail(rollbackReader->error());
            } else if (reader) {
                ok = reader->advance(*session);
                difference = reader->difference();
                if (!ok) return fail(reader->error());
            } else if (rollback) {
                if (rollback->faulted()) return fail(rollback->error());
                const auto target = rollback->localInputTick();
                if (!submittedTick || *submittedTick != target) {
                    DetTickInput packet{target, recipe->config.inputVersion, {}};
                    const DetHostInputRequest request{target, context.simTick,
                        context.hostFrameIndex, context.inputFrameIndex, packet.version};
                    if (recipe->input && !recipe->input(request, packet)) return fail("Host predictive input rejected");
                    if (packet.tick != target || packet.version != recipe->config.inputVersion)
                        return fail("Host predictive input tick/version mismatch");
                    if (!rollback->submitLocal(std::move(packet))) return fail(rollback->error());
                    submittedTick = target;
                }
                if (!rollback->ready()) { networkBlocked = true; error.clear(); return false; }
                networkBlocked = false;
                ok = rollback->advance();
                if (!ok) return fail(rollback->error());
                if (rollbackWriter && !rollbackWriter->sync(*rollback)) return fail(rollbackWriter->error());
            } else if (peers) {
                if (peers->faulted()) return fail(peers->error());
                if (!submittedTick || *submittedTick != session->nextTick()) {
                    DetTickInput packet{session->nextTick(), recipe->config.inputVersion, {}};
                    const DetHostInputRequest request{packet.tick, context.simTick,
                        context.hostFrameIndex, context.inputFrameIndex, packet.version};
                    if (recipe->input && !recipe->input(request, packet))
                        return fail("Host network input source rejected tick");
                    if (packet.tick != session->nextTick() || packet.version != recipe->config.inputVersion)
                        return fail("Host network input tick/version mismatch");
                    if (!peers->submit(std::move(packet))) return fail(peers->error());
                    submittedTick = session->nextTick();
                }
                // Block the standard fixed phase without advancing either clock.
                // Authenticated ingress resumes the wait; explicit user pause stays paused.
                if (!peers->ready()) { networkBlocked = true; error.clear(); return false; }
                networkBlocked = false;
                ok = peers->advance();
                if (!ok) return fail(peers->error());
            } else {
                DetTickInput packet{session->nextTick(), recipe->config.inputVersion, {}};
                const DetHostInputRequest request{packet.tick, context.simTick,
                    context.hostFrameIndex, context.inputFrameIndex, packet.version};
                if (recipe->input && !recipe->input(request, packet))
                    return fail("Host input source rejected tick");
                ok = writer ? writer->advance(std::move(packet)) : session->advance(std::move(packet));
                if (!ok) return fail(writer ? writer->error() : session->error());
            }
            lastHostTick = context.simTick;
            if ((reader && reader->atEnd()) || (rollbackReader && rollbackReader->atEnd())) {
                state = DetHostState::Completed; host->gameLoop().pause();
            }
            error.clear(); return true;
        } catch (const std::exception& e) { return fail(e.what()); }
        catch (...) { return fail("Host input/tick threw"); }
    }
    bool presentationBoundary(World& dispatched) override {
        if (!dispatching) (void)sync(dispatched);
        // Diagnostics/UI/presentation keep running after controlled Sim stops.
        return true;
    }
    float presentationAlpha(float alpha) const override { return networkBlocked ? 1.0f : alpha; }
    void hostShutdown() noexcept override { (void)close(); }
    void worldShutdown(World& dying) noexcept override { if (world == &dying) (void)close(); }
};

DeterministicHostController::DeterministicHostController() : _impl(std::make_shared<Impl>()) {}
DeterministicHostController::~DeterministicHostController() { disconnect(); }
bool DeterministicHostController::bind(app::IEngineHost& host, DetHostOptions options) {
    if (_impl->host) { _impl->error = "Controller is already bound"; return false; }
    if (!options.selectScene || !host.scenes())
        return _impl->fail("Binding needs a selector and an unbound Host Scene service");
    if (host.findService(kDeterministicHostService))
        return _impl->fail("Host deterministic controller service already exists");
    auto* adapter = host.findSubSystem("Entity");
    if (!adapter || !attachEntitySimulationDriver(*adapter, _impl))
        return _impl->fail("Standard Entity subsystem missing or already controlled");
    auto& p = *_impl; p.host = &host; p.adapter = adapter; p.options = std::move(options);
    try {
        host.provideService(kDeterministicHostService, this);
        const std::weak_ptr<Impl> weak = _impl;
        p.subscriptions.push_back(host.eventBus().subscribe<event::SceneCurrentChangedEvent>(
            [weak](const auto&) { if (auto state = weak.lock()) {
                if (!state->dispatching) (void)state->sync(World::instance());
            }}));
        return p.sync(World::instance());
    } catch (const std::exception& e) { return p.fail(e.what()); }
    catch (...) { return p.fail("Host service/subscription installation threw"); }
}
void DeterministicHostController::disconnect() noexcept {
    auto& p = *_impl; if (!p.host || p.dispatching) return;
    auto* host = p.host; (void)p.close();
    try {
        for (auto id : p.subscriptions) host->eventBus().unsubscribe(id);
        if (host->findSubSystem("Entity") == p.adapter)
            (void)detachEntitySimulationDriver(*p.adapter, _impl.get());
        if (host->findService(kDeterministicHostService) == this)
            host->provideService(kDeterministicHostService, nullptr);
    } catch (...) {}
    p.subscriptions.clear(); p.host = nullptr; p.adapter = nullptr;
    p.sceneKey = nullptr; p.resolved = false; p.state = DetHostState::Detached;
}
bool DeterministicHostController::stop() {
    auto& p = *_impl; if (!p.host || p.dispatching) return false;
    p.manualPause = true; p.host->gameLoop().pause(); return p.close();
}
bool DeterministicHostController::restartCurrent() {
    auto& p = *_impl; if (!p.host || p.dispatching) return false;
    p.manualPause = true; p.host->gameLoop().pause(); (void)p.close();
    p.resolved = false; p.state = DetHostState::Stopped;
    return p.sync(World::instance());
}
void DeterministicHostController::pause() { if (_impl->host) { _impl->manualPause = true; _impl->host->gameLoop().pause(); } }
bool DeterministicHostController::resume() {
    auto& p = *_impl; if (!p.host || p.dispatching ||
        (p.state != DetHostState::Running && p.state != DetHostState::Ordinary)) return false;
    p.manualPause = false;
    if (p.peers && p.networkBlocked && !p.peers->ready()) return true;
    if (p.rollback && p.networkBlocked && !p.rollback->ready() && !p.rollback->synchronized()) return true;
    p.host->gameLoop().resume(); return true;
}
bool DeterministicHostController::stepOnce() {
    auto& p = *_impl; if (!p.host || p.dispatching || p.state != DetHostState::Running) return false;
    const auto before = p.session->nextTick(); p.manualPause = true; p.host->gameLoop().pause(); p.host->gameLoop().stepOnce();
    if(p.rollbackReader)return p.state!=DetHostState::Faulted &&
        (p.session->nextTick()>before || p.state==DetHostState::Completed);
    return p.session && p.session->nextTick() == before + 1 && p.state != DetHostState::Faulted;
}
bool DeterministicHostController::restore(const DetSessionCheckpoint& checkpoint) {
    auto& p = *_impl;
    if (!p.host || p.dispatching || !p.session || !p.recipe || p.recipe->mode != DetHostMode::Live || p.peers || p.rollback) return false;
    p.host->gameLoop().pause();
    if (!p.session->restore(checkpoint)) { p.error = p.session->error(); return false; }
    p.state = DetHostState::Running; p.error.clear(); p.difference.reset(); return true;
}
bool DeterministicHostController::seek(std::uint64_t tick) {
    auto& p = *_impl;
    if (p.rollbackReader) {
        const auto& segments = p.rollbackReader->segments();
        for (auto it=segments.rbegin();it!=segments.rend();++it)
            if (tick>=it->firstTick && tick<=it->endTick) return seekRollback(it->epoch,tick);
        p.error = "Seek inside recovery gap/outside replay"; return false;
    }
    if (!p.host || p.dispatching || !p.reader || !p.session) return false;
    p.host->gameLoop().pause();
    if (tick > p.reader->tickCount()) { p.error = "Replay seek is past the completion boundary"; return false; }
    if (!p.reader->seek(*p.session, tick)) {
        p.difference = p.reader->difference(); return p.fail(p.reader->error());
    }
    p.state = p.reader->atEnd() ? DetHostState::Completed : DetHostState::Running;
    p.error.clear(); p.difference.reset(); return true;
}
bool DeterministicHostController::receiveNetwork(std::uint32_t member, std::span<const std::uint8_t> packet) {
    auto& p = *_impl;
    if (p.host && !p.dispatching && p.rollback &&
        (p.state == DetHostState::Running || p.state == DetHostState::Faulted)) {
        const auto oldEpoch = p.rollback->config().epoch;
        const bool ok = p.rollback->receive(member, packet);
        if (p.rollback->faulted()) return p.fail(p.rollback->error());
        if (p.rollback->config().epoch != oldEpoch) {
            p.submittedTick.reset(); p.lastHostTick.reset(); p.networkBlocked = true;
            p.state = DetHostState::Running; p.host->gameLoop().pause();
        }
        if (ok && p.rollbackWriter && !p.rollbackWriter->sync(*p.rollback)) return p.fail(p.rollbackWriter->error());
        p.error = ok ? std::string{} : p.rollback->error();
        if (ok && p.networkBlocked && (p.rollback->ready() || p.rollback->synchronized()) && !p.manualPause) p.host->gameLoop().resume();
        return ok;
    }
    if (!p.host || p.dispatching || !p.peers || p.state != DetHostState::Running) return false;
    const bool ok = p.peers->receive(member, packet);
    if (p.peers->faulted()) return p.fail(p.peers->error());
    if (!ok) p.error = p.peers->error(); else p.error.clear();
    if (ok && p.networkBlocked && p.peers->ready() && !p.manualPause) {
        p.host->gameLoop().resume();
    }
    return ok;
}
bool DeterministicHostController::seekRollback(std::uint32_t epoch,std::uint64_t tick) {
    auto& p=*_impl;if(!p.host || p.dispatching || !p.rollbackReader || !p.session)return false;
    p.manualPause=true;p.host->gameLoop().pause();
    if(!p.rollbackReader->seek(*p.session,epoch,tick)) {
        p.difference=p.rollbackReader->difference();p.error=p.rollbackReader->error();
        if(p.difference || p.session->faulted())return p.fail(p.error);return false;
    }
    p.state=p.rollbackReader->atEnd()?DetHostState::Completed:DetHostState::Running;
    p.difference.reset();p.error.clear();return true;
}
DetRollbackDiagnostics DeterministicHostController::rollbackDiagnostics() const {
    return _impl->rollback?_impl->rollback->diagnostics():DetRollbackDiagnostics{};
}
std::vector<std::vector<std::uint8_t>> DeterministicHostController::networkPackets() const {
    const auto& p = *_impl;
    if (!p.dispatching && p.rollback && p.state == DetHostState::Running) return p.rollback->packets();
    return !p.dispatching && p.peers && p.state == DetHostState::Running ? p.peers->packets()
        : std::vector<std::vector<std::uint8_t>>{};
}
bool DeterministicHostController::networkWaiting() const {
    return (_impl->peers && !_impl->peers->ready()) || (_impl->rollback && !_impl->rollback->ready());
}
bool DeterministicHostController::networkSynchronized() const {
    return _impl->rollback ? _impl->rollback->synchronized() : _impl->peers && _impl->peers->synchronized();
}
std::vector<std::uint32_t> DeterministicHostController::networkMissing() const {
    return _impl->rollback ? _impl->rollback->missing() : _impl->peers ? _impl->peers->missing() : std::vector<std::uint32_t>{};
}
bool DeterministicHostController::disconnectNetworkMember(std::uint32_t member) {
    auto& p = *_impl;
    if (p.host && !p.dispatching && p.rollback && p.state == DetHostState::Running) {
        p.rollback->disconnect(member);
        return p.rollback->faulted() ? p.fail(p.rollback->error()) : true;
    }
    if (!p.host || p.dispatching || !p.peers || p.state != DetHostState::Running) return false;
    p.peers->disconnect(member);
    if (p.peers->faulted()) return p.fail(p.peers->error());
    return true;
}
bool DeterministicHostController::resetNetwork(const DetSessionCheckpoint& checkpoint, std::uint32_t epoch) {
    auto& p = *_impl;
    if (!p.host || p.dispatching || !p.peers || !p.recipe || p.recipe->mode != DetHostMode::Live
        || !epoch || epoch <= p.recipe->lockstep->epoch) return false;
    p.host->gameLoop().pause();
    p.manualPause = true;
    auto config = *p.recipe->lockstep; config.epoch = epoch;
    try {
        // Validate the new barrier before restore, including bounded checkpoint encoding.
        DetLockstepBarrier candidate(config, checkpoint.manifest, detCheckpointHash(checkpoint), checkpoint.nextTick);
        if (!p.session->restore(checkpoint)) { p.error = p.session->error(); return false; }
        p.peers = std::make_unique<DeterministicLockstep>(*p.session, config);
        p.recipe->lockstep = std::move(config); p.submittedTick.reset(); p.lastHostTick.reset(); p.networkBlocked = false;
        p.state = DetHostState::Running; p.error.clear(); p.difference.reset(); return true;
    } catch (const std::exception& e) { return p.fail(e.what()); }
}
DetHostState DeterministicHostController::state() const { return _impl->state; }
bool DeterministicHostController::beginNetworkRecovery(std::uint32_t epoch) {
    auto& p = *_impl;
    if (!p.host || p.dispatching || !p.rollback) return false;
    if (p.rollbackWriter && !p.rollbackWriter->sync(*p.rollback)) return p.fail(p.rollbackWriter->error());
    if (!p.rollback->beginRecovery(epoch)) { p.error = p.rollback->error(); return false; }
    if (p.rollbackWriter && !p.rollbackWriter->sync(*p.rollback)) return p.fail(p.rollbackWriter->error());
    p.host->gameLoop().pause(); p.networkBlocked = true; p.submittedTick.reset(); p.lastHostTick.reset();
    p.state = DetHostState::Running; p.error.clear(); return true;
}
std::uint64_t DeterministicHostController::networkConfirmedNextTick() const {
    return _impl->rollback ? _impl->rollback->confirmedNextTick() : _impl->session ? _impl->session->nextTick() : 0;
}
std::uint64_t DeterministicHostController::networkVerifiedNextTick() const {
    return _impl->rollback ? _impl->rollback->verifiedNextTick() : _impl->session ? _impl->session->nextTick() : 0;
}
std::uint64_t DeterministicHostController::rollbackCount() const {
    return _impl->rollback ? _impl->rollback->history().rollbackCount() : 0;
}
std::uint32_t DeterministicHostController::networkEpoch() const {
    return _impl->rollbackReader ? _impl->rollbackReader->epoch() : _impl->rollback ? _impl->rollback->config().epoch :
        _impl->recipe && _impl->recipe->lockstep ? _impl->recipe->lockstep->epoch : 0;
}
std::vector<DetConfirmedEvent> DeterministicHostController::takeConfirmedEvents() {
    if (_impl->dispatching) return {};
    if (_impl->rollbackReader) return _impl->rollbackReader->takeConfirmedEvents();
    return _impl->rollback ? _impl->rollback->takeConfirmedEvents() : std::vector<DetConfirmedEvent>{};
}
const std::string& DeterministicHostController::error() const {
    if (_impl->error.empty() && _impl->session) return _impl->session->error();
    return _impl->error;
}
std::optional<DetSessionCheckpoint> DeterministicHostController::checkpoint() const {
    return _impl->session ? _impl->session->checkpoint() : std::nullopt;
}
const DeterministicSession* DeterministicHostController::session() const { return _impl->session.get(); }
std::string DeterministicHostController::recordingPath() const { return _impl->lastRecordingPath; }
std::optional<DetReplayArchiveRecovery> DeterministicHostController::replayRecovery() const {
    return _impl->rollbackReader?_impl->rollbackReader->recovery():std::nullopt;
}
const std::optional<DetStateDifference>& DeterministicHostController::difference() const { return _impl->difference; }
DeterministicHostController* deterministicHost(app::IEngineHost& host) noexcept {
    return host.service<DeterministicHostController>(kDeterministicHostService);
}

DeterministicHostIntegrationModule::DeterministicHostIntegrationModule(DetHostOptions options)
    : _descriptor{.id="AYEntity.DeterminismHost", .displayName="Deterministic Scene sessions",
        .version="0.1.0", .dependencies={module::ModuleDependency::required(std::string(kEntityRuntimeModuleId))}},
      _options(std::move(options)) {}
const module::ModuleDescriptor& DeterministicHostIntegrationModule::descriptor() const noexcept { return _descriptor; }
module::ModuleResult DeterministicHostIntegrationModule::install(module::IModuleContext& context) {
    auto* engine = dynamic_cast<app::EngineModuleContext*>(&context);
    if (!engine || !_controller.bind(engine->host(), _options)) {
        const auto error = engine ? _controller.error() : "Deterministic Host needs EngineModuleContext";
        _controller.disconnect();
        return module::ModuleResult::failure(module::ModuleErrorCode::TypeRegistrationFailed, error);
    }
    return module::ModuleResult::success();
}
void DeterministicHostIntegrationModule::shutdown(module::IModuleContext&) noexcept { _controller.disconnect(); }
} // namespace ayt::entity
