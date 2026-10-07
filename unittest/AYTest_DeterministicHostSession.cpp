#include "DetHostFixture.h"
#include <AYEntity/EntityImpl.h>
#include <AYTest.h>
#include <AYApplication/EngineModuleContext.h>
#include <thread>
using namespace dethost_test;
TEST_SUITE(DeterministicHostSession)
TEST_CASE(real_host_owns_each_tick_and_preserves_input_boundary_metadata) {
    Fixture f;auto r=Fixture::recipe();unsigned inputs=0;
    r.input=[&](const auto& request,auto& packet){
        CHECK(request.tick==inputs && request.hostFrameIndex==17 && request.inputFrameIndex==17);
        packet=detsession_scenario::input(request.tick);++inputs;return true;
    };
    CHECK_TRUE(f.bind(r));CHECK(deterministicHost(f.host)==&f.controller);
    f.frame(1.0f/32.0f,17);
    CHECK(inputs==2 && f.controller.session()->nextTick()==2);
    CHECK(f.controller.checkpoint()->globals.at(3)[0]==2);
}
TEST_CASE(pause_single_step_resume_and_live_restore_use_standard_host) {
    Fixture f;CHECK_TRUE(f.bind(Fixture::recipe()));f.frame();
    const auto saved=*f.controller.checkpoint();f.controller.pause();f.frame();
    CHECK(f.controller.session()->nextTick()==1);CHECK_TRUE(f.controller.stepOnce());
    CHECK(f.controller.session()->nextTick()==2);CHECK_TRUE(f.controller.restore(saved));
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==encodeDetCheckpoint(saved));
    CHECK_TRUE(f.controller.resume());f.frame();CHECK(f.controller.session()->nextTick()==2);
}
TEST_CASE(step_profile_changes_fail_before_input_or_world_writes) {
    Fixture f;unsigned calls=0;auto r=Fixture::recipe();r.input=[&](const auto&,auto&){++calls;return true;};
    CHECK_TRUE(f.bind(r));const auto before=encodeDetCheckpoint(*f.controller.checkpoint());
    (void)f.loop.setFixedTimestepRatio(1,60);f.frame(1.0f/30.0f);
    CHECK(f.controller.state()==DetHostState::Faulted && calls==0);
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==before);CHECK_FALSE(f.controller.resume());
}
TEST_CASE(native_host_configuration_is_not_a_rational_session_profile) {
    Fixture f;f.loop.setFixedTimestep(1.0f/64.0f);CHECK_FALSE(f.bind(Fixture::recipe()));
    CHECK(f.controller.state()==DetHostState::Faulted && !f.controller.session());
}
TEST_CASE(rejected_input_stops_without_ordinary_fallback) {
    Fixture f;auto r=Fixture::recipe();r.input=[](const auto&,auto&){return false;};
    CHECK_TRUE(f.bind(r));const auto before=encodeDetCheckpoint(*f.controller.checkpoint());f.frame();
    CHECK(f.controller.state()==DetHostState::Faulted);
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==before);f.frame();
    CHECK(f.controller.session()->nextTick()==0);
}
TEST_CASE(wrong_input_tick_and_exceptions_fail_closed) {
    Fixture f;auto r=Fixture::recipe();r.input=[](const auto&,auto& packet){++packet.tick;return true;};
    CHECK_TRUE(f.bind(r));f.frame();CHECK(f.controller.state()==DetHostState::Faulted);
    CHECK(f.controller.session()->nextTick()==0);CHECK_TRUE(f.controller.restartCurrent());
    CHECK_TRUE(f.controller.stepOnce()==false);
}
TEST_CASE(scene_change_closes_old_recording_and_selects_ordinary_edit_scene) {
    Fixture f;const auto base=path("scene.rpl");auto r=Fixture::recipe(DetHostMode::Record,base);
    CHECK_TRUE(f.controller.bind(f.host,{[r](const auto& scene)->std::optional<DetHostedSceneRecipe>{
        if(scene.mode()==ayt::scene::SceneMode::Edit)return std::nullopt;return r;}}));
    f.frame();const auto recorded=f.controller.recordingPath();
    ayt::scene::Scene edit(ayt::scene::SceneMode::Edit,"ordinary");
    f.host.scenes()->setCurrent(&edit);f.frame(0);
    CHECK(f.controller.state()==DetHostState::Ordinary && !f.controller.session());
    CHECK(f.scene->world().getAllEntities().empty());
    DetReplayReader reader;CHECK_TRUE(reader.open(recorded));CHECK(reader.tickCount()==1);
    f.host.scenes()->setCurrent(nullptr);f.frame(0);
}
TEST_CASE(world_shutdown_releases_session_and_seals_healthy_recording) {
    Fixture f;CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Record,path("shutdown.rpl"))));
    f.frame();const auto recorded=f.controller.recordingPath();f.scene->world().shutdown();
    CHECK(!f.controller.session() && f.controller.state()==DetHostState::Stopped);
    DetReplayReader reader;CHECK_TRUE(reader.open(recorded));CHECK(reader.tickCount()==1);
    f.scene->world().initialize();f.frame(0);CHECK(!f.controller.session());
}
TEST_CASE(record_replay_seek_and_completion_have_identical_state_and_no_live_input) {
    const auto base=path("replay.rpl");std::string recorded;std::vector<std::uint8_t> expected;
    {Fixture f;CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Record,base)));
        for(int i=0;i<9;++i)f.frame();expected=encodeDetCheckpoint(*f.controller.checkpoint());
        recorded=f.controller.recordingPath();CHECK_TRUE(f.controller.stop());CHECK_FALSE(f.controller.restore({}));}
    {Fixture f;auto r=Fixture::recipe(DetHostMode::Replay,recorded);unsigned calls=0;
        r.input=[&](const auto&,auto&){++calls;return false;};CHECK_TRUE(f.bind(r));
        CHECK_TRUE(f.controller.seek(5));CHECK_TRUE(f.controller.resume());
        for(int i=0;i<4;++i)f.frame();CHECK(f.controller.state()==DetHostState::Completed && calls==0);
        CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==expected);f.frame();
        CHECK(f.controller.session()->nextTick()==9);CHECK_FALSE(f.controller.resume());
        CHECK_FALSE(f.controller.seek(10));CHECK(f.controller.session()->nextTick()==9);}
}
TEST_CASE(recording_cannot_overwrite_an_existing_file) {
    const auto base=path("exists.rpl");std::string recorded;
    {Fixture f;CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Record,base)));recorded=f.controller.recordingPath();CHECK_TRUE(f.controller.stop());}
    const auto size=std::filesystem::file_size(recorded);
    {Fixture f;CHECK_FALSE(f.bind(Fixture::recipe(DetHostMode::Record,base)));CHECK(f.controller.state()==DetHostState::Faulted);}
    CHECK(std::filesystem::file_size(recorded)==size);
}
TEST_CASE(conflicting_controllers_do_not_replace_service_and_disconnect_restores_ordinary_lane) {
    Fixture f;CHECK_TRUE(f.bind(Fixture::recipe()));DeterministicHostController second;
    CHECK_FALSE(second.bind(f.host,{[](const auto&)->std::optional<DetHostedSceneRecipe>{return Fixture::recipe();}}));
    CHECK(deterministicHost(f.host)==&f.controller);f.controller.disconnect();
    CHECK(deterministicHost(f.host)==nullptr);auto* e=f.scene->world().createEntity();CHECK(e!=nullptr);f.frame();
}
TEST_CASE(module_install_publishes_controller_and_shutdown_detaches_it) {
    Fixture f;ayt::app::EngineModuleContext context(f.host);
    DeterministicHostIntegrationModule module({[](const auto&)->std::optional<DetHostedSceneRecipe>{return Fixture::recipe();}});
    CHECK(static_cast<bool>(module.install(context)));auto* controller=deterministicHost(f.host);CHECK(controller!=nullptr);
    f.frame();CHECK(controller->session()->nextTick()==1);module.shutdown(context);
    CHECK(deterministicHost(f.host)==nullptr && f.scene->world().getAllEntities().empty());
}
TEST_CASE(live_execution_failure_requires_explicit_restore_and_preserves_host_tick_boundary) {
    Fixture f;bool reject=true;auto r=Fixture::recipe();
    r.configure=[&](auto& s){
        return s.registerSchema({3,1,{1}}) && s.registerGlobalState(3)
            && s.registerSystem(1,0,[&](auto& c){++c.globals(3)[0];return !reject;});};
    CHECK_TRUE(f.bind(r));const auto saved=*f.controller.checkpoint();f.frame();
    CHECK(f.controller.state()==DetHostState::Faulted && f.controller.session()->faulted());
    CHECK_FALSE(f.controller.resume());reject=false;CHECK_TRUE(f.controller.restore(saved));
    CHECK_TRUE(f.controller.stepOnce());CHECK(f.controller.checkpoint()->globals.at(3)[0]==1);
}
TEST_CASE(replay_difference_faults_controller_and_reports_registered_field) {
    const auto base=path("difference.rpl");std::string recorded;
    {Fixture f;CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Record,base)));f.frame();
        recorded=f.controller.recordingPath();CHECK_TRUE(f.controller.stop());}
    {Fixture f;auto r=Fixture::recipe(DetHostMode::Replay,recorded);
        r.configure=[](auto& s){return detsession_scenario::configure(s,false,true);};
        CHECK_TRUE(f.bind(r));f.frame();CHECK(f.controller.state()==DetHostState::Faulted);
        CHECK(f.controller.difference().has_value());CHECK(f.controller.difference()->component==2);
        CHECK_FALSE(f.controller.resume());CHECK_FALSE(f.controller.seek(1));CHECK(f.controller.state()==DetHostState::Faulted);}
}
TEST_CASE(input_throw_stops_before_sim_and_rebind_does_not_fault_healthy_binding) {
    Fixture f;auto r=Fixture::recipe();r.input=[](const auto&,auto&)->bool{throw std::runtime_error("input failed");};
    CHECK_TRUE(f.bind(r));CHECK_FALSE(f.bind(Fixture::recipe()));CHECK(f.controller.state()==DetHostState::Running);
    f.frame();CHECK(f.controller.state()==DetHostState::Faulted && f.controller.session()->nextTick()==0);
}
TEST_CASE(host_shutdown_seals_recording_and_repeated_disconnect_is_safe) {
    Fixture f;CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Record,path("host-end.rpl"))));
    f.frame();const auto recorded=f.controller.recordingPath();f.loop.endHostedSession();
    CHECK(!f.controller.session());DetReplayReader reader;CHECK_TRUE(reader.open(recorded));
    f.controller.disconnect();f.controller.disconnect();CHECK(deterministicHost(f.host)==nullptr);
}
TEST_CASE(controlled_sim_and_input_stay_on_host_thread_with_parallel_scheduling_enabled) {
    Fixture f;const auto owner=std::this_thread::get_id();bool inputThread=false,simThread=false;
    auto r=Fixture::recipe();r.configure=[&](auto& s){
        return s.registerSystem(1,0,[&](auto&){simThread=std::this_thread::get_id()==owner;return true;});};
    r.input=[&](const auto&,auto&){inputThread=std::this_thread::get_id()==owner;return true;};
    CHECK_TRUE(f.bind(r));f.loop.setParallelEnabled(true);f.frame();
    CHECK(inputThread && simThread && f.controller.session()->nextTick()==1);
    f.loop.setParallelEnabled(false);
}
TEST_CASE(checkpoint_outside_write_rejection_exposes_kernel_diagnostic_and_stops_next_tick) {
    Fixture f;CHECK_TRUE(f.bind(Fixture::recipe()));const auto saved=*f.controller.checkpoint();
    auto* actor=f.controller.session()->presentationEntity(2);
    CHECK_TRUE(actor->getComponent<DetSimTransformComponent>()->translate(ayt::math::DetVec3::fromInts(1,0,0)));
    CHECK_FALSE(f.controller.checkpoint().has_value());CHECK_FALSE(f.controller.error().empty());
    f.frame();CHECK(f.controller.state()==DetHostState::Faulted && f.controller.session()->nextTick()==0);
    CHECK_TRUE(f.controller.restore(saved));CHECK_TRUE(f.controller.stepOnce());
}
TEST_SUITE_END
