#include <AYEntity/DeterministicSession.h>
#include <AYReplay/ReplayHash.h>
#include <iostream>
using namespace ayt::entity;using namespace ayt::math;
using D=DetFloat64;using V=DetVec3d;using Q=DetQuaterniond;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void repair(std::vector<std::uint8_t>& blob){auto h=ayt::replay::fnv1a64(blob.data(),blob.size()-8);for(unsigned i=0;i<8;++i)blob[blob.size()-8+i]=static_cast<std::uint8_t>(h>>(i*8));}
int main(){try{
    DetTypedStateSchema schema{790,1,{{1,DetVec2d{D::fromBits(1ULL<<63),D::fromBits(1)}},{2,V::fromInts(1,2,3)},{3,Q{}}}};
    require(detStateType(schema.fields[0].initial)==DetStateType::Vec2d&&detStateType(schema.fields[1].initial)==DetStateType::Vec3d&&detStateType(schema.fields[2].initial)==DetStateType::Quaterniond,"stable types16..18");
    auto words=detStateDefaults(schema);require(words.size()==9&&words[0]==1ULL<<63&&words[1]==1&&words[8]==D::fromInt(1).bits(),"full64 lane order hand result");
    auto original=words;bool rejected=false;try{writeDetState(schema,words,2,V{D::fromBits(0x7ff0000000000000ULL),D{},D{}});}catch(...){rejected=true;}require(rejected&&words==original,"nonfinite geometry write atomicity");
    for(auto type:{DetStateType::Vec2d,DetStateType::Vec3d,DetStateType::Quaterniond}){bool bad=false;try{(void)decodeDetStateValue(type,std::array<std::uint64_t,1>{0});}catch(...){bad=true;}require(bad,"incorrect64 lane count rejected");}
    DeterministicSession s({79,1,1,64,790});require(s.registerTypedSchema(schema)&&s.registerGlobalState(790)&&s.addEntity(10),"register64 geometry");
    require(s.registerSystem(790,0,[](DetTickContext& c){auto p=c.readGlobal<V>(790,2);auto dt=D::fromFloat32(c.dt());c.writeGlobal(790,2,p+V::fromInts(4,8,16)*dt);
        auto q=c.read<Q>(10,790,3).integratedAngularVelocityWorld(V{D{},D{},kDetHalfPi64},dt);if(!q)return false;c.write(10,790,3,*q);return true;}),"64 geometry system");
    if(!s.seal())throw std::runtime_error(s.error());auto initial=*s.checkpoint();require(initial.manifest[4]==6,"manifest6 selected");
    for(unsigned i=0;i<16;++i)if(!s.advance({i,1,{}}))throw std::runtime_error(s.error());auto final=*s.checkpoint();auto bytes=encodeDetCheckpoint(final);
    require(std::get<V>(readDetStateValue(schema,final.globals.at(790),2))==V::fromInts(2,4,7),"binary64 vector tick hand result");
    auto q=std::get<Q>(readDetStateValue(schema,final.actors.at(10).blocks.at(790),3));auto vector=q.rotated(V::fromInts(1,0,0));require(bool(vector),"registered quaternion rotation");
    auto expected=detSinCos(kDetHalfPi64/D::fromInt(4));require((vector->y-expected.sin).bits()==0||D::fromBits((vector->y-expected.sin).bits()&0x7fffffffffffffffULL)<D::fromBits(0x3d00000000000000ULL),"angular integration reference");
    DetSessionCheckpoint decoded;std::string error;if(!decodeDetCheckpoint(bytes,decoded,error))throw std::runtime_error(error);require(encodeDetCheckpoint(decoded)==bytes,"manifest6 codec");
    auto badProfile=final;badProfile.manifest[64]=2;repair(badProfile.manifest);require(!decodeDetCheckpoint(encodeDetCheckpoint(badProfile),decoded,error)&&encodeDetCheckpoint(decoded)==bytes,"unknown geometry function profile rejected atomically");
    require(s.restore(initial),"restore64 geometry");for(unsigned i=0;i<16;++i)require(s.advance({i,1,{}}),"64 geometry suffix");require(encodeDetCheckpoint(*s.checkpoint())==bytes,"complete64 restore suffix");
    auto nonfinite=final;nonfinite.actors.at(10).blocks.at(790)[5]=0x7ff0000000000000ULL;require(!s.restore(nonfinite)&&encodeDetCheckpoint(*s.checkpoint())==bytes,"nonfinite quaternion restore atomicity");
    DeterministicSession empty({79,1,1,64,790});require(empty.registerTypedSchema(schema)&&empty.seal(),"manifest6 empty callback tables");require(decodeDetCheckpoint(encodeDetCheckpoint(*empty.checkpoint()),decoded,error),"empty-table6 decode");
    DeterministicSession scalar({79,1,1,64,790});require(scalar.registerTypedSchema({790,1,{{1,D{}}}})&&scalar.seal()&&scalar.checkpoint()->manifest[4]==5,"scalar manifest5 retained");
    std::cout<<"PASS binary64 geometry types16..18, manifest6, registered angular integration and exact restore closure\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
