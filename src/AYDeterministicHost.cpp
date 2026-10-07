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
            if (writer && state == DetHostState::Running && !session->faulted()) {
                if (!writer->finish()) { fail(writer->error()); ok = false; }
            }
            writer.reset(); reader.reset(); session.reset(); recipe.reset(); step.reset();
            if (world && world->_hostedSimulationObserver == this)
                world->_hostedSimulationObserver = nullptr;
            world = nullptr; lastHostTick.reset();
            if (state != DetHostState::Faulted) state = DetHostState::Stopped;
        } catch (...) {
            // Destruction still releases ownership before World storage disappears.
            writer.reset(); reader.reset(); session.reset(); recipe.reset(); step.reset();
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
            step = game::FixedTimestep::fromRatio(recipe->config.stepNumerator, recipe->config.stepDenominator);
            if (!step || !matches(host->gameLoop().getFixedStep()))
                return fail("Host/session rational fixed step mismatch");
            recipe->config.stepNumerator = step->numerator();
            recipe->config.stepDenominator = step->denominator();
            session = std::make_unique<DeterministicSession>(*world, recipe->config);
            if (!recipe->configure(*session) || !session->seal())
                return fail(session->error().empty() ? "Scene configuration rejected" : session->error());
            if (recipe->mode == DetHostMode::Record) {
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
                if (!reader->open(recipe->replayPath) || !reader->restoreInitial(*session))
                    return fail(reader->error());
            }
            state = reader && reader->atEnd() ? DetHostState::Completed : DetHostState::Running;
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
            if (reader) {
                ok = reader->advance(*session);
                difference = reader->difference();
                if (!ok) return fail(reader->error());
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
            if (reader && reader->atEnd()) {
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
    p.host->gameLoop().pause(); return p.close();
}
bool DeterministicHostController::restartCurrent() {
    auto& p = *_impl; if (!p.host || p.dispatching) return false;
    p.host->gameLoop().pause(); (void)p.close();
    p.resolved = false; p.state = DetHostState::Stopped;
    return p.sync(World::instance());
}
void DeterministicHostController::pause() { if (_impl->host) _impl->host->gameLoop().pause(); }
bool DeterministicHostController::resume() {
    auto& p = *_impl; if (!p.host || p.dispatching ||
        (p.state != DetHostState::Running && p.state != DetHostState::Ordinary)) return false;
    p.host->gameLoop().resume(); return true;
}
bool DeterministicHostController::stepOnce() {
    auto& p = *_impl; if (!p.host || p.dispatching || p.state != DetHostState::Running) return false;
    const auto before = p.session->nextTick(); p.host->gameLoop().pause(); p.host->gameLoop().stepOnce();
    return p.session && p.session->nextTick() == before + 1 && p.state != DetHostState::Faulted;
}
bool DeterministicHostController::restore(const DetSessionCheckpoint& checkpoint) {
    auto& p = *_impl;
    if (!p.host || p.dispatching || !p.session || !p.recipe || p.recipe->mode != DetHostMode::Live) return false;
    p.host->gameLoop().pause();
    if (!p.session->restore(checkpoint)) { p.error = p.session->error(); return false; }
    p.state = DetHostState::Running; p.error.clear(); p.difference.reset(); return true;
}
bool DeterministicHostController::seek(std::uint64_t tick) {
    auto& p = *_impl; if (!p.host || p.dispatching || !p.reader || !p.session) return false;
    p.host->gameLoop().pause();
    if (tick > p.reader->tickCount()) { p.error = "Replay seek is past the completion boundary"; return false; }
    if (!p.reader->seek(*p.session, tick)) {
        p.difference = p.reader->difference(); return p.fail(p.reader->error());
    }
    p.state = p.reader->atEnd() ? DetHostState::Completed : DetHostState::Running;
    p.error.clear(); p.difference.reset(); return true;
}
DetHostState DeterministicHostController::state() const { return _impl->state; }
const std::string& DeterministicHostController::error() const {
    if (_impl->error.empty() && _impl->session) return _impl->session->error();
    return _impl->error;
}
std::optional<DetSessionCheckpoint> DeterministicHostController::checkpoint() const {
    return _impl->session ? _impl->session->checkpoint() : std::nullopt;
}
const DeterministicSession* DeterministicHostController::session() const { return _impl->session.get(); }
std::string DeterministicHostController::recordingPath() const { return _impl->lastRecordingPath; }
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
