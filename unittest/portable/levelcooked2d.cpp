// design reference: AYEntity/design.md Stage45 bounded cooked level envelope.
// Header layout/LE values and hand gameplay timing are independent assertions.
// Full CP comparisons below are execution-path equivalence, not all-byte goldens.
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
unsigned cases=0,rejected=0,comparisons=0;
void require(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
void put(Bytes& bytes,std::size_t at,std::uint64_t value,unsigned width){for(unsigned n=0;n<width;++n)bytes.at(at+n)=static_cast<std::uint8_t>(value>>(8*n));}
std::uint64_t fnv(std::string_view text){std::uint64_t h=UINT64_C(0xcbf29ce484222325);for(const unsigned char c:text){h^=c;h*=UINT64_C(0x100000001b3);}return h;}
Bytes handEnvelope(std::string_view payload,std::uint64_t semantic){
    Bytes b{'A','Y','D','L','C','0','0','1',1,0,0,0};b.resize(32);
    put(b,12,payload.size(),4);put(b,16,semantic,8);put(b,24,fnv(payload),8);b.insert(b.end(),payload.begin(),payload.end());return b;}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetLevelActor2D actor(SimEntityId id,DetBodyMode2D mode,V position,V half,bool trigger=false){DetLevelActor2D a;
    a.id=id;a.position=position;a.body={mode,half,{},{},id==10?1u:2u,id==10?2u:1u,trigger};return a;}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=3;l.applicationVersion=45;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxTriggerPairs=8;c.advanced=true;
    c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)});hero.character=true;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(8),{}},{q(2),q(2)}),actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},true)};
    l.actionTargets={{20,1,true,0},{30,1,true,0}};l.counters={{1,0,1,0}};l.objectives={{1,{{1,1}}}};
    l.claims={{1,30,10,1,1}};l.flowBindings={{1,0}};l.objectiveActions={{1,1,{20,DetActionKind2D::CollisionDisable},true}};return l;}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void write(const fs::path& p,const Bytes& bytes){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));out.close();require(bool(out),"evidence write");}
void write(const fs::path& p,const std::string& text){write(p,Bytes(text.begin(),text.end()));}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto p=parent/("level-cooked-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}throw std::runtime_error("fresh evidence directory unavailable");}
void roundtrip(const DetLevel2D& original,const fs::path& dir,const std::string& name){const auto text=encodeDetLevel2D(original);
    const auto b=encodeDetLevelCooked2D(original);require(b==handEnvelope(text,detLevelSessionConfig(original).contentHash),"independent specified LE envelope "+name);
    require(b.size()==32+text.size(),"exact payload/envelope width");DetLevel2D decoded;std::string error;
    const bool ok=decodeDetLevelCooked2D(b,decoded,error);require(ok,error);
    require(encodeDetLevel2D(decoded)==text && decoded.formatVersion==original.formatVersion && encodeDetLevelCooked2D(decoded)==b,"complete three-format canonical roundtrip "+name);
    write(dir/(name+".dlc"),b);++cases;std::cout<<"PASS "<<name<<" complete cooked roundtrip and independent LE/FNV envelope\n";
}
void malformedChecks(){const auto l=scene();const auto valid=encodeDetLevelCooked2D(l);const auto text=encodeDetLevel2D(l);DetLevel2D output=l;output.applicationVersion=99;
    const auto before=encodeDetLevel2D(output);std::string error;
    const auto refuse=[&](const Bytes& b,std::string_view expected){const bool ok=decodeDetLevelCooked2D(b,output,error);
        require(!ok && encodeDetLevel2D(output)==before,"every envelope/source failure is output atomic");
        require(error.find(expected)!=std::string::npos,"specific cooked diagnostic expected "+std::string(expected)+", got "+error);++rejected;};
    refuse({},"size");auto b=valid;b.resize(31);refuse(b,"size");
    b=valid;b[0]='X';refuse(b,"magic");b=valid;put(b,8,2,4);refuse(b,"version");
    b=valid;put(b,12,0,4);refuse(b,"payload size");b=valid;put(b,12,65537,4);refuse(b,"payload size");
    b=valid;put(b,12,UINT32_MAX,4);refuse(b,"payload size");
    b=valid;put(b,12,text.size()+1,4);refuse(b,"length/EOF");b=valid;b.pop_back();refuse(b,"length/EOF");
    b=valid;b.push_back(0);refuse(b,"length/EOF");
    b=valid;b[32]^=1;refuse(b,"checksum");b=valid;b[24]^=1;refuse(b,"checksum");
    b=valid;b[16]^=1;refuse(b,"semantic content hash");
    refuse(handEnvelope(" "+text,detLevelSessionConfig(l).contentHash),"not canonical");
    refuse(handEnvelope(text+"\r\n",detLevelSessionConfig(l).contentHash),"not canonical");
    auto badText=text;badText.replace(0,8,"UNKNOWN!");refuse(handEnvelope(badText,detLevelSessionConfig(l).contentHash),"source preflight");
    const auto at=badText.find("counter 1 0 1 0");require(at!=std::string::npos,"semantic negative counter token");
    badText=text;badText.replace(at,15,"counter 1 0 1 2");refuse(handEnvelope(badText,detLevelSessionConfig(l).contentHash),"source preflight");
    auto maximum=text;maximum.resize(kDetLevel2DMaxTextBytes,' ');const auto maxEnvelope=handEnvelope(maximum,detLevelSessionConfig(l).contentHash);
    require(maxEnvelope.size()==kDetLevelCooked2DMaxBytes,"inclusive advertised whole resource bound");refuse(maxEnvelope,"not canonical");
    b=maxEnvelope;b.push_back(' ');refuse(b,"envelope size");
    auto invalid=l;invalid.formatVersion=99;bool threw=false;try{(void)encodeDetLevelCooked2D(invalid);}catch(const std::invalid_argument&){threw=true;}
    require(threw,"encoder performs actual default Level preflight");++cases;
    std::cout<<"PASS strict envelope/version/length/EOF/checksums/canonical source, actual semantic preflight and failure atomicity\n";
}
void configure(DeterministicSession& s,const DetLevel2D& l){std::string error;
    require(s.registerSystem(6,0,[c=l.character](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
        if(command.source!=1 || command.sequence!=0 || command.type!=1 || command.payload.size()!=1)return false;
        detCharacterInput2D(ctx,10,c,{D::fromUInt(command.payload[0]),false,false,false});}return true;}),s.error());
    const bool configured=configureDetLevel2D(s,l,error);require(configured,error);const bool sealed=s.seal();require(sealed,s.error());}
void closureChecks(const fs::path& dir){const auto l=scene();DetLevel2D decoded;std::string error;require(decodeDetLevelCooked2D(encodeDetLevelCooked2D(l),decoded,error),error);
    DeterministicSession direct(detLevelSessionConfig(l)),cooked(detLevelSessionConfig(decoded));configure(direct,l);configure(cooked,decoded);
    const auto progress=detLevelProgressConfig2D(l);require(bytes(direct)==bytes(cooked),"initial complete manifest/checkpoint equality");++comparisons;
    for(unsigned t=0;t<8;++t){const DetTickInput input{t,1,{{1,0,1,{static_cast<std::uint8_t>(t<2?4:0)}}}};
        const bool a=direct.advance(input),b=cooked.advance(input);require(a && b,"direct/cooked actual advance: "+direct.error()+" / "+cooked.error());
        const auto cp=checkpoint(cooked);require(bytes(direct)==encodeDetCheckpoint(cp),"all actual direct/cooked checkpoint bytes");++comparisons;
        require(detProgressCounter2D(cp,progress,1)==(t==0?0u:1u),"hand actual Enter tick0 then claim tick1");
        const auto flow=detProgressFlowObjective2D(cp,detLevelProgressFlowConfig2D(l),1);
        require(flow.attemptCount==(t==0?0u:1u) && flow.onceConsumed==(t>=1),"cooked objective action consumes exactly one attempt");}
    write(dir/"final.state",bytes(cooked));++cases;std::cout<<"PASS8ticks actual decoded/direct complete state and routed collection/action timing\n";
}
}
int main(int argc,char** argv){try{fs::path output,legacy,interaction;for(int n=1;n<argc;++n){const std::string arg=argv[n];
    if((arg=="--output" || arg=="--legacy" || arg=="--interaction") && n+1<argc){if(arg=="--output")output=argv[++n];else if(arg=="--legacy")legacy=argv[++n];else interaction=argv[++n];}
    else throw std::invalid_argument("usage: --output existingParent --legacy format1File --interaction format2File");}
    require(!output.empty() && !legacy.empty() && !interaction.empty(),"three input arguments required");const auto dir=fresh(output);
    DetLevel2D first,second;std::string error;const bool firstOk=loadDetLevel2D(legacy,first,error);require(firstOk,error);
    const bool secondOk=loadDetLevel2D(interaction,second,error);require(secondOk,error);
    require(first.formatVersion==1 && second.formatVersion==2,"actual old resource versions");
    const auto legacyBytes=encodeDetLevelCooked2D(first);const Bytes pinnedSemantic{1,184,101,115,193,52,227,16};
    // Independently recorded original format1 semantic value1216874329789544449.
    require(std::equal(pinnedSemantic.begin(),pinnedSemantic.end(),legacyBytes.begin()+16),"original UInt64 semantic hash fixed LE bytes");
    roundtrip(first,dir,"format1");roundtrip(second,dir,"format2");roundtrip(scene(),dir,"format3");malformedChecks();closureChecks(dir);
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)
        +",\"rejectedBuffers\":"+std::to_string(rejected)+",\"completeCheckpointComparisons\":"+std::to_string(comparisons)
        +",\"ticks\":8,\"cookedFormat\":1,\"authentication\":false}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;
}catch(const std::exception& error){std::cerr<<"FAIL levelcooked2d: "<<error.what()<<'\n';return 1;}}
