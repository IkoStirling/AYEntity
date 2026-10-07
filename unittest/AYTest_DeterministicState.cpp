#include "DetTypedSessionScenario.h"
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity.h>
#include <AYTest.h>
#include <AYReplay/ReplayHash.h>
#include <AYReplay/FileReplayRecorder.h>
#include <chrono>
#include <filesystem>
#include <limits>

namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
struct Fixture {
    World& world=World::instance();
    std::unique_ptr<DeterministicSession> session;
    Fixture(){world.initialize();session=std::make_unique<DeterministicSession>(world);}
    ~Fixture(){session.reset();world.shutdown();}
};
template<class F> bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
DetTypedStateSchema scalarSchema(){return {2,1,{{10,false},{20,D::fromBits(0x80000000u)},{30,std::int32_t{-1}}}};}
}
static_assert(DetStateScalar<D> && DetStateScalar<V> && !DetStateScalar<float> && !DetStateScalar<double> && !DetStateScalar<void*>);
TEST_SUITE(DeterministicState)
TEST_CASE(all_fixed_width_integer_bool_identity_and_math_values_roundtrip) {
    const std::vector<DetStateValue> values={false,true,std::int8_t{-128},std::uint8_t{255},std::int16_t{-32768},std::uint16_t{65535},
        std::numeric_limits<std::int32_t>::min(),UINT32_MAX,std::numeric_limits<std::int64_t>::min(),UINT64_MAX,
        D::fromBits(0x80000000u),D::fromBits(1),D::fromBits(0x7f7fffffu),
        ayt::math::DetVec2::fromBits({0x80000000u,1}),V::fromBits({1,2,3}),
        ayt::math::DetQuaternion::fromBits({1,2,3,4}),DetEntityRef{UINT64_MAX}};
    for(const auto& value:values) {
        const auto encoded=encodeDetStateValue(value),again=encodeDetStateValue(decodeDetStateValue(detStateType(value),encoded));
        CHECK(encoded==again);
    }
    CHECK(encodeDetStateValue(std::int8_t{-1})==std::vector<std::uint64_t>{255});
    CHECK(encodeDetStateValue(std::int32_t{-1})==std::vector<std::uint64_t>{UINT32_MAX});
}
TEST_CASE(codec_rejects_noncanonical_bool_integer_float_width_type_and_nonfinite_lanes) {
    for(auto type:{DetStateType::Bool,DetStateType::UInt8,DetStateType::Int16,DetStateType::UInt32,DetStateType::Float32})
        CHECK(rejects([&]{(void)decodeDetStateValue(type,std::vector<std::uint64_t>{UINT64_MAX});}));
    CHECK(rejects([]{(void)decodeDetStateValue(DetStateType::Vec3,std::vector<std::uint64_t>{0,0});}));
    CHECK(rejects([]{(void)decodeDetStateValue(static_cast<DetStateType>(99),std::vector<std::uint64_t>{0});}));
    for(auto bits:{0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u}) {
        CHECK(rejects([&]{(void)encodeDetStateValue(D::fromBits(bits));}));
        CHECK(rejects([&]{(void)decodeDetStateValue(DetStateType::Quaternion,std::vector<std::uint64_t>{0,0,bits,0});}));
    }
}
TEST_CASE(schema_defaults_and_initializers_reject_bad_ids_versions_types_and_bounds_atomically) {
    auto schema=scalarSchema();auto words=detStateDefaults(schema);const auto initial=words;
    CHECK(words==std::vector<std::uint64_t>({0,0x80000000u,UINT32_MAX}));
    CHECK(rejects([&]{writeDetState(schema,words,10,std::uint8_t{1});}));
    CHECK(rejects([&]{writeDetState(schema,words,20,D::fromBits(0x7f800000u));}));
    CHECK(rejects([&]{writeDetState(schema,words,0,false);}));CHECK(words==initial);
    CHECK(rejects([&]{(void)readDetStateValue(schema,std::span(words).first(2),10);}));
    for(unsigned test=0;test<5;++test){auto bad=schema;
        if(test==0)bad.id=1;if(test==1)bad.version=0;if(test==2)bad.fields[0].id=0;
        if(test==3)bad.fields[1].id=bad.fields[0].id;if(test==4)std::reverse(bad.fields.begin(),bad.fields.end());
        CHECK(rejects([&]{(void)detStateDefaults(bad);}));}
    auto max=schema;max.fields.clear();for(unsigned i=1;i<=128;++i)max.fields.push_back({i,ayt::math::DetQuaternion{}});
    CHECK(detStateDefaults(max).size()==512);max.fields.push_back({129,false});CHECK(rejects([&]{(void)detStateDefaults(max);}));
}
TEST_CASE(typed_registration_defaults_globals_and_duplicate_legacy_namespace) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(s.registerTypedSchema(scalarSchema()));
    CHECK_FALSE(s.registerSchema({2,1,{1}}));CHECK_FALSE(s.registerTypedSchema(scalarSchema()));
    CHECK_FALSE(s.registerGlobalState(2,{2,0,0}));CHECK_TRUE(s.registerGlobalState(2));CHECK_TRUE(s.addEntity(7));
    CHECK_FALSE(s.registerTypedSchema({3,1,{{1,false}}}));CHECK_TRUE(s.seal());
    const auto saved=*s.checkpoint();CHECK(saved.manifest[4]==2);
    CHECK(saved.actors.at(7).blocks.at(2)==detStateDefaults(scalarSchema()));CHECK(saved.globals.at(2)==saved.actors.at(7).blocks.at(2));
}
TEST_CASE(actor_and_global_typed_read_write_survive_restore_with_exact_binary32_bits) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(s.registerTypedSchema(scalarSchema()));CHECK_TRUE(s.registerGlobalState(2));CHECK_TRUE(s.addEntity(7));
    CHECK_TRUE(s.registerSystem(1,0,[](DetTickContext& c){
        c.write(7,2,10,!c.read<bool>(7,2,10));c.writeGlobal(2,30,c.readGlobal<std::int32_t>(2,30)-1);
        const auto x=c.read<D>(7,2,20);c.write(7,2,20,x+D::fromBits(1));return true;}));
    CHECK_TRUE(s.seal());const auto before=*s.checkpoint();CHECK_TRUE(s.advance({0,1,{}}));const auto after=*s.checkpoint();
    CHECK(after.actors.at(7).blocks.at(2)[1]==1);CHECK(after.globals.at(2)[2]==0xfffffffeu);
    CHECK_TRUE(s.restore(before));CHECK_TRUE(s.advance({0,1,{}}));CHECK(!firstDetDifference(after,*s.checkpoint()));
}
TEST_CASE(caught_bad_typed_access_still_faults_tick_and_explicit_restore_recovers) {
    for(unsigned test=0;test<7;++test) {
        Fixture f;auto& s=*f.session;CHECK_TRUE(s.registerTypedSchema(scalarSchema()));CHECK_TRUE(s.addEntity(7));
        CHECK_TRUE(s.registerSystem(1,0,[test](DetTickContext& c){try{
            if(test==0)c.write(7,2,10,std::uint8_t{1});if(test==1)(void)c.read<std::uint64_t>(7,2,20);
            if(test==2)c.write(7,2,20,D::fromBits(0x7fc00000u));if(test==3)(void)c.read<D>(99,2,20);
            if(test==4)(void)c.read<D>(7,99,20);if(test==5)(void)c.read<D>(7,2,99);
            if(test==6)(void)c.words(7,2);
        }catch(const std::exception&){}return true;}));
        CHECK_TRUE(s.seal());const auto saved=*s.checkpoint();CHECK_FALSE(s.advance({0,1,{}}));CHECK_TRUE(s.faulted());
        CHECK_TRUE(s.restore(saved));CHECK(!firstDetDifference(saved,*s.checkpoint()));
    }
}
TEST_CASE(checksummed_invalid_typed_restore_and_wire_decode_leave_existing_state_unchanged) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(dettyped_scenario::configure(s));const auto saved=*s.checkpoint();
    DetSessionCheckpoint decoded=saved;std::string error;
    const auto schema=dettyped_scenario::actorSchema();
    for(unsigned test=0;test<6;++test) {
        auto bad=saved;auto& words=bad.actors.at(2).blocks.at(2);
        if(test==0)words[2]=0x7f800000u;if(test==1)words[4]=0x100000000ull;
        if(test==2)words[10]=2;if(test==3)words[14]=UINT64_MAX;
        if(test==4)words.pop_back();if(test==5)bad.globals.at(3).push_back(0);
        CHECK_FALSE(s.restore(bad));CHECK_FALSE(decodeDetCheckpoint(encodeDetCheckpoint(bad),decoded,error));
        CHECK(!firstDetDifference(saved,*s.checkpoint()));CHECK(!firstDetDifference(saved,decoded));
    }
}
TEST_CASE(typed_checkpoint_roundtrip_reports_stable_field_and_vector_quaternion_lane) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(dettyped_scenario::configure(s));const auto saved=*s.checkpoint();
    auto changed=saved;changed.actors.at(100).blocks.at(2)[4]=1;
    auto diff=firstDetDifference(saved,changed);CHECK(diff && diff->entity==100 && diff->component==2 && diff->field==40 && diff->lane==1);
    changed=saved;changed.actors.at(100).blocks.at(2)[9]=1;
    diff=firstDetDifference(saved,changed);CHECK(diff && diff->field==50 && diff->lane==3);
    DetSessionCheckpoint out;std::string error;CHECK_TRUE(decodeDetCheckpoint(encodeDetCheckpoint(saved),out,error));CHECK(!firstDetDifference(saved,out));
}
TEST_CASE(mixed_word_and_typed_schemas_and_maximum_lane_block_decode) {
    Fixture f;auto& s=*f.session;DetTypedStateSchema schema{2,1,{}};
    for(unsigned i=1;i<=128;++i)schema.fields.push_back({i,ayt::math::DetQuaternion{}});
    CHECK_TRUE(s.registerTypedSchema(schema));CHECK_TRUE(s.registerSchema({3,1,{10}},{UINT64_MAX}));CHECK_TRUE(s.addEntity(7));CHECK_TRUE(s.seal());
    const auto saved=*s.checkpoint();DetSessionCheckpoint decoded;std::string error;
    CHECK_TRUE(decodeDetCheckpoint(encodeDetCheckpoint(saved),decoded,error));CHECK(!firstDetDifference(saved,decoded));
}
TEST_CASE(schema_type_version_and_default_changes_reject_restoration) {
    DetSessionCheckpoint saved;
    {Fixture f;CHECK_TRUE(f.session->registerTypedSchema(scalarSchema()));CHECK_TRUE(f.session->addEntity(7));CHECK_TRUE(f.session->seal());saved=*f.session->checkpoint();}
    for(unsigned test=0;test<3;++test){Fixture f;auto schema=scalarSchema();
        if(test==0)++schema.version;if(test==1)schema.fields[0].initial=std::uint8_t{0};if(test==2)schema.fields[0].initial=true;
        CHECK_TRUE(f.session->registerTypedSchema(schema));CHECK_TRUE(f.session->addEntity(7));CHECK_TRUE(f.session->seal());CHECK_FALSE(f.session->restore(saved));}
}
TEST_CASE(checksummed_manifest_rejects_unknown_type_version_field_and_invalid_defaults) {
    Fixture f;auto& s=*f.session;CHECK_TRUE(s.registerTypedSchema(scalarSchema()));CHECK_TRUE(s.addEntity(7));CHECK_TRUE(s.seal());
    const auto saved=*s.checkpoint();DetSessionCheckpoint out=saved;std::string error;
    for(unsigned test=0;test<5;++test) {
        auto bad=saved;auto& m=bad.manifest;
        if(test==0)m[4]=3; // unknown manifest version
        if(test==1)m[80]=99; // unknown first field type
        if(test==2)m[76]=0; // zero first field ID
        if(test==3)m[84]=2; // noncanonical first bool default
        if(test==4){m[100]=0;m[101]=0;m[102]=0x80;m[103]=0x7f;} // second field infinity
        const auto hash=ayt::replay::fnv1a64(m.data(),m.size()-8);
        for(unsigned i=0;i<8;++i)m[m.size()-8+i]=static_cast<std::uint8_t>(hash>>(8*i));
        CHECK_FALSE(decodeDetCheckpoint(encodeDetCheckpoint(bad),out,error));CHECK(!firstDetDifference(saved,out));
    }
}
TEST_CASE(replay_reports_typed_vector_lane_when_logic_diverges) {
    const auto base=(std::filesystem::temp_directory_path()/std::filesystem::path("ay-typed-lane-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".rpl")).string();
    const auto path=ayt::replay::FileReplayRecorder::rotationPathFor(base,0);
    struct Cleanup {std::string path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    auto configure=[](DeterministicSession& s,bool altered){return s.registerTypedSchema({2,1,{{10,V{}}}})
        && s.registerSystem(1,0,[altered](DetTickContext& c){c.write(7,2,10,V::fromInts(0,altered?2:1,0));return true;})
        && s.addEntity(7) && s.seal();};
    {Fixture f;CHECK_TRUE(configure(*f.session,false));DetReplayWriter writer;CHECK_TRUE(writer.begin(*f.session,base));
        CHECK_TRUE(writer.advance({0,1,{}}));CHECK_TRUE(writer.finish());}
    {Fixture f;CHECK_TRUE(configure(*f.session,true));DetReplayReader reader;CHECK_TRUE(reader.open(path));
        CHECK_TRUE(reader.restoreInitial(*f.session));CHECK_FALSE(reader.advance(*f.session));const auto diff=reader.difference();
        CHECK(diff && diff->tick==1 && diff->entity==7 && diff->component==2 && diff->field==10 && diff->lane==1);}
}
TEST_SUITE_END;
