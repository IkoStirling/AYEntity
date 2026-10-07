#pragma once
#include "DetSessionScenario.h"
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/World.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYGameLoop.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <filesystem>
namespace dethost_test {
using namespace ayt::entity;
struct Fixture {
    ayt::game::GameLoop& loop=ayt::game::GameLoop::instance();
    ayt::app::IEngineHost& host=ayt::app::defaultEngineHost();
    std::unique_ptr<ayt::app::EngineRuntimeScope> scope;
    std::unique_ptr<ayt::scene::Scene> scene;
    DeterministicHostController controller;
    Fixture() {
        loop.endHostedSession(); ayt::game::SubSystemRegistry::instance().clearAll();
        loop.setRenderThreadEnabled(false);loop.setParallelEnabled(false);
        loop.setTimeScale(1);(void)loop.setFixedTimestepRatio(1,64);
        loop.registerSubSystem(createEntitySubSystem().release());
        if(!loop.prepareHostedSession())throw std::runtime_error("Host prepare failed");
        scope=std::make_unique<ayt::app::EngineRuntimeScope>(host);
        scene=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-host");
        host.scenes()->setCurrent(scene.get());
    }
    ~Fixture() {
        controller.disconnect();host.scenes()->setCurrent(nullptr);scene.reset();scope.reset();
        loop.endHostedSession();ayt::game::SubSystemRegistry::instance().clearAll();
        (void)loop.setFixedTimestepRatio(1,60);loop.setTimeScale(1);
    }
    static DetHostedSceneRecipe recipe(DetHostMode mode=DetHostMode::Live,std::string path={}) {
        DetHostedSceneRecipe r;r.config.stepNumerator=1;r.config.stepDenominator=64;
        r.mode=mode;r.replayPath=std::move(path);r.checkpointInterval=3;
        r.configure=[](auto& s){return detsession_scenario::configure(s);};
        r.input=[](const auto& request,auto& packet){packet=detsession_scenario::input(request.tick);return true;};
        return r;
    }
    bool bind(DetHostedSceneRecipe r) {
        return controller.bind(host,{[r=std::move(r)](const auto&)->std::optional<DetHostedSceneRecipe>{return r;}});
    }
    void frame(float dt=1.0f/64.0f,std::uint64_t index=1){loop.tickHostedFrame({dt,index,index});}
};
inline std::string path(const char* name) {
    auto dir=std::filesystem::temp_directory_path()/"aliyat-deterministic-host-tests";
    std::filesystem::create_directories(dir);
    auto base=(dir/name).string();
    auto rotated=base.substr(0,base.size()-4)+"_000.rpl";
    std::filesystem::remove(rotated);return base;
}
}
