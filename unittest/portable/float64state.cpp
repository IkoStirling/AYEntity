#include <AYEntity/DeterministicSession.h>
#include <AYReplay/ReplayHash.h>
#include <iostream>
using namespace ayt::entity;using F=ayt::math::DetFloat64;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    DetSessionConfig c{76,1,1,64,76};DeterministicSession s(c);
    require(s.registerTypedSchema({700,1,{{1,F::fromBits(0x8000000000000000ULL)},{2,F::fromInt(2)}}}),"register64");
    require(s.registerGlobalState(700),"global64");require(s.addEntity(10),"actor64");
    require(s.registerSystem(1,0,[](DetTickContext& ctx){ctx.writeGlobal(700,2,ctx.readGlobal<F>(700,2)+F::fromInt(1));ctx.write(10,700,2,ctx.read<F>(10,700,2)/F::fromInt(2));return true;}),"system64");
    if(!s.seal())throw std::runtime_error("seal64: "+s.error());auto initial=*s.checkpoint();require(initial.manifest[4]==5,"manifest5 optin");
    require(initial.globals.at(700)[0]==0x8000000000000000ULL,"signed64zero bits");
    for(unsigned t=0;t<4;++t)if(!s.advance({t,1,{}}))throw std::runtime_error("advance64: "+s.error());auto final=*s.checkpoint();
    require(final.globals.at(700)[1]==F::fromInt(6).bits()&&final.actors.at(10).blocks.at(700)[1]==F::fromDouble(0.125).bits(),"64 hand arithmetic closure");
    auto bytes=encodeDetCheckpoint(final);DetSessionCheckpoint decoded;std::string error;if(!decodeDetCheckpoint(bytes,decoded,error))throw std::runtime_error("decode64: "+error);require(encodeDetCheckpoint(decoded)==bytes,"manifest5 decode bytes");
    auto repair=[](std::vector<std::uint8_t>& blob){auto hash=ayt::replay::fnv1a64(blob.data(),blob.size()-8);for(unsigned i=0;i<8;++i)blob[blob.size()-8+i]=static_cast<std::uint8_t>(hash>>(8*i));};
    auto badProfile=final;badProfile.manifest[60]=2;repair(badProfile.manifest);require(!decodeDetCheckpoint(encodeDetCheckpoint(badProfile),decoded,error),"unknown binary64 numeric profile rejected");
    auto empty64=final;empty64.manifest[84]=9;empty64.manifest[100]=9;repair(empty64.manifest);require(!decodeDetCheckpoint(encodeDetCheckpoint(empty64),decoded,error),"noncanonical empty manifest5 rejected");
    require(s.restore(initial),"restore64");for(unsigned t=0;t<4;++t)require(s.advance({t,1,{}}),"replay64");require(encodeDetCheckpoint(*s.checkpoint())==bytes,"64 restore exact suffix");
    auto invalid=final;invalid.actors.at(10).blocks.at(700)[1]=0x7ff0000000000000ULL;
    require(!s.restore(invalid)&&encodeDetCheckpoint(*s.checkpoint())==bytes,"nonfinite64 restore atomicity");
    DetTypedStateSchema layoutSchema{701,1,{{1,F{}}}};auto words=detStateDefaults(layoutSchema);bool rejected=false;
    try{writeDetStateValue(layoutSchema,words,1,F::fromBits(0x7ff8000000000000ULL));}catch(...){rejected=true;}
    require(rejected&&words[0]==0,"nonfinite64 write no mutation");
    DeterministicSession old(c);require(old.registerTypedSchema({700,1,{{1,ayt::math::DetFloat32{}}}})&&old.seal(),"old32");require(old.checkpoint()->manifest[4]==2,"old manifest unchanged");
    std::cout<<"PASS binary64 type15, manifest5, signed-zero, hand state arithmetic, codec/restore and nonfinite rejection; old binary32 manifest2\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
