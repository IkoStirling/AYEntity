#include "DetSessionScenario.h"
#include "DetTypedSessionScenario.h"
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity.h>
#include <AYEntity/SimToPresentBridgeSystem.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYTest.h>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace {
using namespace ayt::entity;
using namespace detsession_scenario;
struct Fixture {
    World& world=World::instance();
    std::unique_ptr<DeterministicSession> session;
    Fixture(DetSessionConfig config={}) {world.initialize();session=std::make_unique<DeterministicSession>(world,config);}
    ~Fixture(){session.reset();world.shutdown();}
};
struct ReplayPath {
    std::string base=(std::filesystem::temp_directory_path()/std::filesystem::path("ay-det-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".rpl")).string();
    std::string file=replayPath();
    std::string replayPath() const {return ayt::replay::FileReplayRecorder::rotationPathFor(base,0);}
    ~ReplayPath(){std::error_code error;std::filesystem::remove(file,error);}
};
std::vector<std::uint8_t> bytes(const std::string& path) {std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
void write(const std::string& path,const std::vector<std::uint8_t>& v) {std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(v.data()),v.size());}
}
TEST_SUITE(DeterministicSession)
TEST_CASE(configuration_rejects_duplicate_versions_ids_and_late_schema) {
    Fixture f;auto& s=*f.session;
    CHECK_FALSE(s.registerSchema({1,1,{1}}));CHECK_FALSE(s.registerSchema({2,0,{1}}));CHECK_FALSE(s.registerSchema({2,1,{2,1}}));
    CHECK_TRUE(s.registerSchema({2,1,{10,20}}));CHECK_FALSE(s.registerSchema({2,1,{10,20}}));
    CHECK_FALSE(s.registerGlobalState(3));CHECK_TRUE(s.registerGlobalState(2));
    CHECK_TRUE(s.registerRandomStream(1,0));CHECK_FALSE(s.registerRandomStream(1,3));
    CHECK_TRUE(s.addEntity(9));CHECK_FALSE(s.addEntity(9));CHECK_FALSE(s.registerSchema({3,1,{1}}));
    CHECK_TRUE(s.seal());CHECK_FALSE(s.registerSystem(1,0,[](auto&){return true;}));
}
TEST_CASE(world_owner_excludes_direct_ticks_and_structural_mutation) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(s.addEntity(1));CHECK_TRUE(s.seal());
    unsigned rejected=0;
    try{f.world.createEntity();}catch(const std::logic_error&){++rejected;}
    try{f.world.destroyEntity(s.presentationEntity(1));}catch(const std::logic_error&){++rejected;}
    try{f.world.fixedUpdate(D::fromInt(1));}catch(const std::logic_error&){++rejected;}
    try{f.world.fixedUpdate(0.1f);}catch(const std::logic_error&){++rejected;}
    CHECK_TRUE(rejected==4);CHECK_TRUE(s.nextTick()==0);
}
TEST_CASE(system_priority_then_stable_id_and_entity_order_are_explicit) {
    Fixture f;auto& s=*f.session;std::vector<unsigned> order;std::vector<SimEntityId> ids;
    CHECK_TRUE(s.registerSystem(20,0,[&](auto& c){order.push_back(20);ids=c.entities();return true;}));
    CHECK_TRUE(s.registerSystem(10,0,[&](auto&){order.push_back(10);return true;}));
    CHECK_TRUE(s.registerSystem(30,-1,[&](auto&){order.push_back(30);return true;}));
    CHECK_TRUE(s.addEntity(100));CHECK_TRUE(s.addEntity(2));CHECK_TRUE(s.seal());CHECK_TRUE(s.advance({0,1,{}}));
    CHECK_TRUE(order==std::vector<unsigned>({30,10,20}));CHECK_TRUE(ids==std::vector<SimEntityId>({2,100}));
}
TEST_CASE(input_validation_precedes_history_rng_or_callback_changes) {
    Fixture f;CHECK_TRUE(configure(*f.session));auto& s=*f.session;const auto saved=*s.checkpoint();
    CHECK_FALSE(s.advance(input(1)));auto p=input(0);p.version=2;CHECK_FALSE(s.advance(p));
    p=input(0);p.commands.push_back(p.commands[0]);CHECK_FALSE(s.advance(p));
    p=input(0);p.commands[0].payload.resize(65537);CHECK_FALSE(s.advance(p));
    CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));CHECK_FALSE(s.faulted());CHECK_TRUE(s.advance(input(0)));
}
TEST_CASE(canonical_input_bytes_use_little_endian_and_checksum) {
    DetTickInput p{0x0102030405060708ull,3,{{7,2,9,{0xaa,0xbb}}}};
    const auto b=encodeDetInput(p);CHECK_TRUE(b.size()==50 && b[0]==0x41 && b[8]==8 && b[15]==1 && b[20]==1 && b[40]==0xaa);
    DetTickInput out;std::string error;CHECK_TRUE(decodeDetInput(b,out,error) && out==p);
    auto bad=b;bad[40]^=1;CHECK_FALSE(decodeDetInput(bad,out,error));CHECK_TRUE(out==p);
}
TEST_CASE(deferred_spawn_despawn_restore_preserves_stable_identity) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(configure(s));const auto before=*s.checkpoint();
    const auto oldRuntime=s.presentationEntity(2)->getId();
    CHECK_TRUE(s.advance(input(0)));CHECK_TRUE(s.checkpoint()->actors.contains(1000));
    CHECK_TRUE(s.advance(input(1)));CHECK_FALSE(s.checkpoint()->actors.contains(1000));
    CHECK_TRUE(s.checkpoint()->retiredIds==std::vector<SimEntityId>({1000}));
    CHECK_TRUE(s.restore(before));CHECK_TRUE(!firstDetDifference(before,*s.checkpoint()));
    CHECK_TRUE(s.presentationEntity(2)->getId()==oldRuntime);CHECK_TRUE(s.advance(input(0)));
}
TEST_CASE(pending_events_survive_checkpoint_and_deliver_once_in_source_order) {
    Fixture f;auto& s=*f.session;std::vector<DetTickCommand> seen;
    CHECK_TRUE(s.registerSystem(20,-1,[](auto& c){if(c.tick()==0)c.emit(7);return true;}));
    CHECK_TRUE(s.registerSystem(10,0,[](auto& c){if(c.tick()==0)c.emit(8);return true;}));
    CHECK_TRUE(s.registerSystem(30,1,[&](auto& c){seen.assign(c.events().begin(),c.events().end());return true;}));
    CHECK_TRUE(s.seal());CHECK_TRUE(s.advance({0,1,{}}));const auto saved=*s.checkpoint();
    CHECK_TRUE(saved.pendingEvents.size()==2 && saved.pendingEvents[0].source==10);CHECK_TRUE(seen.empty());
    CHECK_TRUE(s.advance({1,1,{}}));CHECK_TRUE(seen==saved.pendingEvents);CHECK_TRUE(s.checkpoint()->pendingEvents.empty());
    CHECK_TRUE(s.restore(saved));CHECK_TRUE(s.advance({1,1,{}}));CHECK_TRUE(seen==saved.pendingEvents);
}
TEST_CASE(restoring_despawned_actor_recreates_runtime_handle_with_same_sim_state) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(configure(s));CHECK_TRUE(s.advance(input(0)));
    const auto saved=*s.checkpoint();const auto oldId=s.presentationEntity(1000)->getId();
    CHECK_TRUE(s.advance(input(1)));CHECK_TRUE(s.presentationEntity(1000)==nullptr);
    CHECK_TRUE(s.restore(saved));CHECK_TRUE(s.presentationEntity(1000)!=nullptr);
    CHECK_TRUE(s.presentationEntity(1000)->getId()!=oldId);
    CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));
    CHECK_TRUE(s.advance(input(1)));CHECK_TRUE(s.presentationEntity(1000)==nullptr);
}
TEST_CASE(retired_id_reuse_faults_and_explicit_restore_recovers) {
    Fixture f;auto& s=*f.session;
    CHECK_TRUE(s.registerSystem(1,0,[](auto& c){
        if(c.tick()==0)c.spawn(7);
        else if(c.tick()==1)c.despawn(7);
        else c.spawn(7);
        return true;
    }));
    CHECK_TRUE(s.seal());CHECK_TRUE(s.advance({0,1,{}}));CHECK_TRUE(s.advance({1,1,{}}));
    const auto saved=*s.checkpoint();CHECK_FALSE(s.advance({2,1,{}}));CHECK_TRUE(s.faulted());
    CHECK_TRUE(s.restore(saved));CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));
}
TEST_CASE(world_teardown_revokes_session_and_cannot_alias_new_runtime_handles) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(s.addEntity(1));CHECK_TRUE(s.seal());
    f.world.shutdown();f.world.initialize();auto* replacement=f.world.createEntity();
    CHECK_TRUE(replacement!=nullptr);CHECK_TRUE(s.presentationEntity(1)==nullptr);
    CHECK_FALSE(s.seal());CHECK_FALSE(s.advance({0,1,{}}));CHECK_FALSE(s.checkpoint().has_value());
    f.session.reset();CHECK_TRUE(f.world.findEntity(replacement->getId())==replacement);
}
TEST_CASE(registered_pose_words_globals_rng_and_events_replay_after_restore) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(configure(s));
    for(unsigned i=0;i<40;++i)CHECK_TRUE(s.advance(input(i)));
    const auto saved=*s.checkpoint();std::vector<DetSessionCheckpoint> samples;
    for(unsigned i=40;i<80;++i){CHECK_TRUE(s.advance(input(i)));samples.push_back(*s.checkpoint());}
    CHECK_TRUE(s.restore(saved));bool same=true;
    for(const auto& sample:samples){same &= s.advance(input(s.nextTick(),true));same &= !firstDetDifference(sample,*s.checkpoint());}
    CHECK_TRUE(same);
}
TEST_CASE(invalid_restore_is_atomic_for_manifest_fields_rng_events_and_retired_ids) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(configure(s));CHECK_TRUE(s.advance(input(0)));const auto saved=*s.checkpoint();
    auto bad=saved;bad.manifest[0]^=1;CHECK_FALSE(s.restore(bad));
    bad=saved;bad.actors.at(2).pose.position[0]=0x7f800000u;CHECK_FALSE(s.restore(bad));
    bad=saved;bad.actors.at(2).blocks.at(2).pop_back();CHECK_FALSE(s.restore(bad));
    bad=saved;bad.randomStreams.at(7).inc^=2;CHECK_FALSE(s.restore(bad));
    bad=saved;bad.pendingEvents.push_back(bad.pendingEvents[0]);CHECK_FALSE(s.restore(bad));
    bad=saved;bad.retiredIds.push_back(2);CHECK_FALSE(s.restore(bad));
    CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));
}
TEST_CASE(checkpoint_wire_rejects_corruption_truncation_and_preserves_output) {
    Fixture f;CHECK_TRUE(configure(*f.session));const auto saved=*f.session->checkpoint();
    const auto encoded=encodeDetCheckpoint(saved);DetSessionCheckpoint out;std::string error;
    CHECK_TRUE(decodeDetCheckpoint(encoded,out,error));CHECK_TRUE(!firstDetDifference(saved,out));
    auto broken=encoded;broken.back()^=1;CHECK_FALSE(decodeDetCheckpoint(broken,out,error));CHECK_TRUE(!firstDetDifference(saved,out));
    CHECK_FALSE(decodeDetCheckpoint(std::span(encoded).first(encoded.size()-1),out,error));
}
TEST_CASE(callback_failure_faults_session_until_explicit_registered_restore) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(s.registerSchema({2,1,{1}}));CHECK_TRUE(s.registerGlobalState(2));
    CHECK_TRUE(s.registerSystem(1,0,[](auto& c){++c.globals(2)[0];return false;}));CHECK_TRUE(s.seal());const auto saved=*s.checkpoint();
    CHECK_FALSE(s.advance({0,1,{}}));CHECK_TRUE(s.faulted());CHECK_FALSE(s.checkpoint().has_value());CHECK_FALSE(s.advance({0,1,{}}));
    CHECK_TRUE(s.restore(saved));CHECK_FALSE(s.faulted());CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));
}
TEST_CASE(checksummed_wire_still_rejects_invalid_events_retired_ids_and_pose) {
    Fixture f;CHECK_TRUE(configure(*f.session));CHECK_TRUE(f.session->advance(input(0)));
    const auto saved=*f.session->checkpoint();DetSessionCheckpoint out=saved;std::string error;
    auto bad=saved;bad.pendingEvents.push_back(bad.pendingEvents.front());
    CHECK_FALSE(decodeDetCheckpoint(encodeDetCheckpoint(bad),out,error));
    bad=saved;bad.retiredIds={9,8};CHECK_FALSE(decodeDetCheckpoint(encodeDetCheckpoint(bad),out,error));
    bad=saved;bad.retiredIds={2};CHECK_FALSE(decodeDetCheckpoint(encodeDetCheckpoint(bad),out,error));
    bad=saved;bad.actors.at(2).pose.profileVersion=UINT32_MAX;
    CHECK_FALSE(decodeDetCheckpoint(encodeDetCheckpoint(bad),out,error));
    CHECK_TRUE(!firstDetDifference(saved,out));
}
TEST_CASE(presentation_rate_and_external_sim_mutation_have_distinct_contracts) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(configure(s));CHECK_TRUE(s.advance(input(0)));const auto saved=*s.checkpoint();
    SimToPresentBridgeSystem bridge;bridge.onUpdate(0.25f);f.world.updatePresentation(0.001f,0.75f);
    CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));
    s.presentationEntity(2)->getComponent<DetSimTransformComponent>()->translate(V::fromInts(1,0,0));
    CHECK_FALSE(s.advance(input(1)));CHECK_TRUE(s.nextTick()==1);CHECK_TRUE(s.restore(saved));CHECK_TRUE(s.advance(input(1)));
}
TEST_CASE(divergence_identifies_stable_entity_schema_and_field_id) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(configure(s));auto expected=*s.checkpoint(),actual=expected;
    actual.actors.at(100).blocks.at(2)[1]=7;auto d=firstDetDifference(expected,actual);
    CHECK_TRUE(d && d->entity==100 && d->component==2 && d->field==20 && d->section=="state" && d->actual==7);
}
TEST_CASE(replay_roundtrip_seek_and_different_registration_order) {
    ReplayPath path;DetSessionCheckpoint final;
    {Fixture f;CHECK_TRUE(configure(*f.session));DetReplayWriter writer;CHECK_TRUE(writer.begin(*f.session,path.base,7));
        for(unsigned i=0;i<30;++i)CHECK_TRUE(writer.advance(input(i)));final=*f.session->checkpoint();CHECK_TRUE(writer.finish());}
    {Fixture f;CHECK_TRUE(configure(*f.session,true));DetReplayReader reader;CHECK_TRUE(reader.open(path.file));CHECK_TRUE(reader.tickCount()==30);
        CHECK_TRUE(reader.restoreInitial(*f.session));for(unsigned i=0;i<30 && !reader.atEnd();++i)CHECK_TRUE(reader.advance(*f.session));CHECK_TRUE(reader.atEnd());
        CHECK_TRUE(!firstDetDifference(final,*f.session->checkpoint()));CHECK_TRUE(reader.seek(*f.session,16));CHECK_TRUE(f.session->nextTick()==16);
        for(unsigned i=16;i<30 && !reader.atEnd();++i)CHECK_TRUE(reader.advance(*f.session));CHECK_TRUE(reader.atEnd());CHECK_TRUE(!firstDetDifference(final,*f.session->checkpoint()));}
}
TEST_CASE(replay_missing_seal_corrupt_record_and_manifest_change_are_rejected) {
    ReplayPath path;
    {Fixture f;CHECK_TRUE(configure(*f.session));DetReplayWriter writer;CHECK_TRUE(writer.begin(*f.session,path.base));CHECK_TRUE(writer.advance(input(0)));}
    DetReplayReader reader;CHECK_FALSE(reader.open(path.file));
    {Fixture f;CHECK_TRUE(configure(*f.session));DetReplayWriter writer;CHECK_TRUE(writer.begin(*f.session,path.base));CHECK_TRUE(writer.advance(input(0)));CHECK_TRUE(writer.finish());}
    CHECK_TRUE(reader.open(path.file));auto data=bytes(path.file);data.resize(data.size()-40);write(path.file,data);CHECK_FALSE(reader.open(path.file));
    // Reopen the valid bytes so this tests manifest compatibility, not a closed reader.
    {Fixture f;CHECK_TRUE(configure(*f.session));DetReplayWriter writer;CHECK_TRUE(writer.begin(*f.session,path.base));CHECK_TRUE(writer.advance(input(0)));CHECK_TRUE(writer.finish());}
    CHECK_TRUE(reader.open(path.file));
    Fixture other({2,1,1,60,0});CHECK_TRUE(configure(*other.session));CHECK_FALSE(reader.restoreInitial(*other.session));
}
TEST_CASE(empty_replay_and_extensionless_path_roundtrip_and_failed_reopen_closes_reader) {
    ReplayPath path;
    {Fixture f;CHECK_TRUE(configure(*f.session));DetReplayWriter writer;
        CHECK_TRUE(writer.begin(*f.session,path.base.substr(0,path.base.size()-4)));
        CHECK_TRUE(writer.path()==path.file);CHECK_TRUE(writer.finish());}
    Fixture f;CHECK_TRUE(configure(*f.session));DetReplayReader reader;
    CHECK_TRUE(reader.open(path.file));CHECK_TRUE(reader.tickCount()==0 && reader.atEnd());
    CHECK_TRUE(reader.restoreInitial(*f.session));CHECK_FALSE(reader.seek(*f.session,1));
    CHECK_FALSE(reader.open(path.file+".absent"));CHECK_FALSE(reader.atEnd());
    CHECK_FALSE(reader.restoreInitial(*f.session));
}
TEST_CASE(replay_reports_first_tick_and_registered_field_for_changed_logic) {
    ReplayPath path;
    {Fixture f;CHECK_TRUE(configure(*f.session));DetReplayWriter writer;CHECK_TRUE(writer.begin(*f.session,path.base));CHECK_TRUE(writer.advance(input(0)));CHECK_TRUE(writer.finish());}
    Fixture f;CHECK_TRUE(configure(*f.session,false,true));DetReplayReader reader;CHECK_TRUE(reader.open(path.file));CHECK_TRUE(reader.restoreInitial(*f.session));
    CHECK_FALSE(reader.advance(*f.session));CHECK_TRUE(reader.difference().has_value());
    const auto d=*reader.difference();CHECK_TRUE(d.tick==1 && d.entity==2 && d.component==2 && d.field==20);
}

TEST_CASE(semantic_validator_rejects_restore_before_world_mutation_and_faults_tick) {
    Fixture f;auto& s=*f.session;
    CHECK_TRUE(s.registerTypedSchema({2,1,{{10,std::uint32_t{3}}}}));
    CHECK_TRUE(s.registerValidator(7,1,[](const auto& state,std::string& error){
        for(const auto& [id,a]:state.actors)if(a.blocks.at(2)[0]>10){error="counter limit";return false;}return true;}));
    CHECK_TRUE(s.registerSystem(1,0,[](auto& c){c.write(1,2,10,std::uint32_t{11});return true;}));
    CHECK_TRUE(s.addEntity(1));CHECK_TRUE(s.seal());const auto saved=*s.checkpoint();auto* actor=s.presentationEntity(1);
    auto bad=saved;bad.actors.at(1).blocks.at(2)[0]=11;bad.actors.emplace(2,bad.actors.at(1));
    CHECK_FALSE(s.restore(bad));CHECK_TRUE(s.presentationEntity(1)==actor);CHECK_TRUE(s.presentationEntity(2)==nullptr);
    CHECK_TRUE(!firstDetDifference(saved,*s.checkpoint()));
    CHECK_FALSE(s.advance({0,1,{}}));CHECK_TRUE(s.faulted());CHECK_TRUE(s.error().find("validator 7")!=std::string::npos);
    CHECK_TRUE(s.restore(saved));CHECK_FALSE(s.faulted());
}
TEST_CASE(semantic_validator_checks_prospective_structural_boundary) {
    Fixture f;auto& s=*f.session;
    CHECK_TRUE(s.registerValidator(1,2,[](const auto& state,std::string& e){e="only one actor";return state.actors.size()<=1;}));
    CHECK_TRUE(s.registerSystem(1,0,[](auto& c){c.spawn(2);return true;}));CHECK_TRUE(s.addEntity(1));CHECK_TRUE(s.seal());
    const auto saved=*s.checkpoint();CHECK_FALSE(s.advance({0,1,{}}));CHECK_TRUE(s.faulted());CHECK_TRUE(s.presentationEntity(2)==nullptr);
    CHECK_TRUE(s.restore(saved));
}
TEST_CASE(semantic_validator_manifest_order_versions_and_configuration) {
    Fixture f;auto& s=*f.session;auto accept=[](const auto&,std::string&){return true;};
    CHECK_FALSE(s.registerValidator(0,1,accept));CHECK_FALSE(s.registerValidator(1,0,accept));
    CHECK_TRUE(s.registerValidator(2,1,accept));CHECK_TRUE(s.registerValidator(1,3,accept));CHECK_FALSE(s.registerValidator(1,4,accept));
    CHECK_TRUE(s.seal());CHECK_TRUE(s.manifest()[4]==3);CHECK_FALSE(s.registerValidator(3,1,accept));
    const auto bytes=encodeDetCheckpoint(*s.checkpoint());DetSessionCheckpoint out;std::string error;
    CHECK_TRUE(decodeDetCheckpoint(bytes,out,error));CHECK_TRUE(!firstDetDifference(*s.checkpoint(),out));
}
TEST_CASE(semantic_validator_reentry_and_exceptions_reject_initial_state) {
    Fixture f;auto& s=*f.session;
    CHECK_TRUE(s.registerValidator(1,1,[&s](const auto&,std::string&){(void)s.seal();return true;}));
    CHECK_FALSE(s.seal());CHECK_FALSE(s.sealed());CHECK_TRUE(s.error().find("reentry")!=std::string::npos);
}
TEST_CASE(world_adapter_and_owned_kernel_share_full_registered_state_and_restore) {
    Fixture f({1,1,1,64,0});DeterministicSession kernel({1,1,1,64,0});
    CHECK_TRUE(dettyped_scenario::configure(*f.session));CHECK_TRUE(dettyped_scenario::configure(kernel,true));
    std::optional<DetSessionCheckpoint> middle;
    for(unsigned tick=0;tick<1000;++tick){
        CHECK_TRUE(f.session->advance(dettyped_scenario::input(tick)));
        CHECK_TRUE(kernel.advance(dettyped_scenario::input(tick,true)));
        CHECK(encodeDetCheckpoint(*f.session->checkpoint())==encodeDetCheckpoint(*kernel.checkpoint()));
        if(tick==499)middle=f.session->checkpoint();
    }
    CHECK_TRUE(kernel.presentationEntity(2)==nullptr);
    const auto expected=encodeDetCheckpoint(*kernel.checkpoint());
    CHECK_TRUE(f.session->restore(*middle));CHECK_TRUE(kernel.restore(*middle));
    for(unsigned tick=500;tick<1000;++tick){CHECK_TRUE(f.session->advance(dettyped_scenario::input(tick)));CHECK_TRUE(kernel.advance(dettyped_scenario::input(tick)));}
    CHECK(encodeDetCheckpoint(*f.session->checkpoint())==expected && encodeDetCheckpoint(*kernel.checkpoint())==expected);
}
TEST_SUITE_END;
