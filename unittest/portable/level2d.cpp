#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
D q(int numerator,int denominator=4){return D::fromInt(numerator)/D::fromInt(denominator);}
DetLevel2D simple(){DetLevel2D level;level.stepDenominator=4;auto& c=level.character;
    c.gravity=D::fromInt(4);c.acceleration=D::fromInt(32);c.deceleration=D::fromInt(32);
    DetLevelActor2D hero;hero.id=10;hero.position={{},q(2)};hero.character=true;
    hero.body={DetBodyMode2D::Kinematic,{q(2),q(2)},{},{},1,6,false};
    DetLevelActor2D floor;floor.id=20;floor.position={{},D::fromInt(-1)};
    floor.body={DetBodyMode2D::Static,{D::fromInt(8),D::fromInt(1)},{},{},2,1,false};
    DetLevelActor2D trigger;trigger.id=30;trigger.position={D::fromInt(2),q(2)};
    trigger.body={DetBodyMode2D::Static,{q(1),q(2)},{},{},4,1,true};level.actors={hero,floor,trigger};return level;}
std::optional<DetSessionCheckpoint> checkedCp(DeterministicSession& session){auto cp=session.checkpoint();require(bool(cp),session.error());return cp;}
Bytes bytes(DeterministicSession& session){return encodeDetCheckpoint(*checkedCp(session));}
std::vector<SimEntityId> heroes(const DetLevel2D& level){std::vector<SimEntityId> result;for(const auto& actor:level.actors)if(actor.character)result.push_back(actor.id);return result;}
std::vector<std::uint8_t> payload(SimEntityId id,D speed){std::vector<std::uint8_t> result;
    for(unsigned n=0;n<8;++n)result.push_back(static_cast<std::uint8_t>(id>>(8*n)));
    for(unsigned n=0;n<4;++n)result.push_back(static_cast<std::uint8_t>(speed.bits()>>(8*n)));return result;}
DetTickInput frame(std::uint64_t tick,const DetLevel2D& level,D speed){DetTickInput input{tick,level.inputVersion,{}};std::uint32_t sequence=0;
    for(auto id:heroes(level))input.commands.push_back({1,sequence++,0x32001,payload(id,speed)});return input;}
bool configure(DeterministicSession& session,const DetLevel2D& level){std::string error;const auto ids=heroes(level);
    if(!session.registerSystem(10,level.character.collision.priority,[ids,c=level.character](DetTickContext& context){
        for(const auto& command:context.input().commands){if(command.source!=1 || command.type!=0x32001 || command.payload.size()!=12)return false;
            SimEntityId id=0;std::uint32_t bits=0;for(unsigned n=0;n<8;++n)id|=std::uint64_t{command.payload[n]}<<(8*n);
            for(unsigned n=0;n<4;++n)bits|=std::uint32_t{command.payload[8+n]}<<(8*n);
            if(std::find(ids.begin(),ids.end(),id)==ids.end())return false;detCharacterInput2D(context,id,c,{D::fromBits(bits),false,false,false});}return true;}))return false;
    if(!configureDetLevel2D(session,level,error))throw std::runtime_error(error);return session.seal();}
std::string quote(const std::string& text){std::string result="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text){if(c=='"' || c=='\\'){result+='\\';result+=char(c);}else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}else result+=char(c);}return result+'"';}
void write(const fs::path& file,const Bytes& data){std::ofstream stream(file,std::ios::binary);stream.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));stream.close();require(bool(stream),"Cannot write evidence bytes");}
void write(const fs::path& file,const std::string& data){std::ofstream stream(file,std::ios::binary);stream<<data;stream.close();require(bool(stream),"Cannot write evidence text");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must be an existing parent directory");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto directory=parent/("level2d-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(directory))return directory;}throw std::runtime_error("Fresh evidence directory unavailable");}
void parserChecks(const fs::path& directory){const auto original=simple();const auto text=encodeDetLevel2D(original);std::string error;DetLevel2D decoded;
    require(decodeDetLevel2D(text,decoded,error),error);require(encodeDetLevel2D(decoded)==text,"Canonical encode/decode retains all exact bits");
    require(decodeDetLevel2D("\r\n\t"+text+" \n",decoded,error) && detLevelSessionConfig(decoded).contentHash==detLevelSessionConfig(original).contentHash,"Whitespace is not content identity");
    write(directory/"simple.dlt",text);require(loadDetLevel2D(directory/"simple.dlt",decoded,error),error);
    const auto before=encodeDetLevel2D(decoded);
    for(auto malformed:{text+" garbage",std::string("AYDLEVEL 2\n"),std::string(kDetLevel2DMaxTextBytes+1,'x'),std::string("\xef\xbb\xbf")+text}){
        require(!decodeDetLevel2D(malformed,decoded,error) && !error.empty() && encodeDetLevel2D(decoded)==before,"Bad decode preserves existing level");}
    auto badBits=text;const auto position=badBits.find("3f000000");require(position!=std::string::npos,"Fixture half-bit token");badBits.replace(position,8,"7fc00000");
    require(!decodeDetLevel2D(badBits,decoded,error) && encodeDetLevel2D(decoded)==before,"Nonfinite bits rejected atomically");
    badBits=text;badBits.replace(position,8,"3f00000");
    require(!decodeDetLevel2D(badBits,decoded,error) && encodeDetLevel2D(decoded)==before,"Short Float32 token rejected atomically");
    auto badBoolean=text;const auto role=badBoolean.find(" 1 6 0 0 1 0\n");require(role!=std::string::npos,"Fixture role tokens");
    badBoolean.replace(role,13," 1 6 0 0 2 0\n");
    require(!decodeDetLevel2D(badBoolean,decoded,error) && encodeDetLevel2D(decoded)==before,"Boolean2 rejected rather than interpreted truthy");
    require(!loadDetLevel2D(directory/"missing.dlt",decoded,error) && encodeDetLevel2D(decoded)==before,"Failed load preserves output");
    for(unsigned kind=0;kind<8;++kind){auto bad=original;switch(kind){case 0:bad.actors[1].id=bad.actors[0].id;break;
        case 1:bad.actors[0].body.mode=DetBodyMode2D::Static;break;case 2:bad.actors[0].path=99;break;
        case 3:bad.actors[0].position.y={};break;case 4:bad.character.collision.maxBodies=2;break;
        case 5:bad.actors[0].body.ignoredOneWay={20};break;case 6:bad.actors[0].body.half.x={};break;
        case 7:bad.character.collision.continuousTerrain=true;break;}
        require(!validateDetLevel2D(bad,error) && !error.empty(),"Invalid authoring/initial geometry/policy rejected");}
    auto options=DetLevel2DOptions{};options.identitySchemaBase=4;require(!validateDetLevel2D(original,error,options),"Schema namespace conflict detected before destination write");
    options={};options.identityValidatorId=original.character.validatorId;require(!validateDetLevel2D(original,error,options),"Validator namespace conflict");
    options={};options.identityLogicId=original.character.logicProfileId;require(!validateDetLevel2D(original,error,options),"Logic namespace conflict");
    auto swapped=original;std::swap(swapped.actors[0],swapped.actors[1]);require(!validateDetLevel2D(swapped,error),"Unsorted stable IDs rejected");
    std::cout<<"PASS strict ASCII/bit tokens, canonical roundtrip, bounded counts, role/path/policy/initial geometry, output atomicity and namespace preflight\n";
}
void bodyCountChecks(){DetLevel2D level;std::string error;
    for(unsigned n=0;n<64;++n){DetLevelActor2D actor;actor.id=n==63?UINT64_MAX:n+1;
        actor.position={D::fromInt(4*int(n)),{}};actor.body={DetBodyMode2D::Static,{q(2),q(2)},{},{},1,1,false};level.actors.push_back(actor);}
    require(validateDetLevel2D(level,error),error);const auto text=encodeDetLevel2D(level);DetLevel2D decoded;
    require(decodeDetLevel2D(text,decoded,error) && decoded.actors.size()==64 && decoded.actors.back().id==UINT64_MAX,"64 bodies and full-width stable ID endpoint accepted");
    level.actors.push_back(level.actors.back());require(!validateDetLevel2D(level,error),"65th body rejected before registration");
    std::cout<<"PASS 64-body capacity and full-width stable IDs without float conversion\n";
}
void identityChecks(){auto level=simple();DeterministicSession first(detLevelSessionConfig(level));require(configure(first,level),first.error());
    const auto cp=*checkedCp(first);const auto before=bytes(first);auto changed=cp;changed.globals.at(32).at(1)^=1;
    require(!first.restore(changed) && bytes(first)==before,"Content global restore rejects atomically");changed=cp;changed.actors.at(10).blocks.at(32).at(1)^=1;
    require(!first.restore(changed) && bytes(first)==before,"Content actor copy restore rejects atomically");
    auto different=level;different.actors[2].position.x=D::fromInt(3);auto zeroHash=detLevelSessionConfig(level);zeroHash.contentHash=0;
    DeterministicSession a(zeroHash),b(zeroHash);require(configure(a,level) && configure(b,different),"Same-hash identity fixture setup");
    require(a.manifest()!=b.manifest(),"Complete content bytes, not only contentHash, bind manifest");const auto bBefore=bytes(b);
    require(!b.restore(*checkedCp(a)) && bytes(b)==bBefore,"Different content rejects foreign checkpoint even with equal Session hash");
    std::string error;require(!configureDetLevel2D(first,level,error) && bytes(first)==before,"Sealed destination preflight unchanged");
    DetSessionConfig wrong=detLevelSessionConfig(level);wrong.stepDenominator=8;DeterministicSession mismatch(wrong);
    require(!configureDetLevel2D(mismatch,level,error) && !mismatch.sealed(),"Wrong destination fixed step rejected before registration");
    std::cout<<"PASS exact manifest content/global/actor-copy identity, foreign and malformed restore atomicity, sealed and mismatched destination rejection\n";
}
void pathCountChecks(){auto level=simple();level.actors.resize(2);std::string error;
    for(unsigned n=0;n<8;++n){DetLevelPath2D p;p.id=n+1;p.waypoints={{{},4,0},{{D::fromInt(1),{}},4,0}};level.paths.push_back(p);
        DetLevelActor2D actor;actor.id=100+n;actor.position={D::fromInt(20+4*int(n)),D::fromInt(4)};actor.path=n+1;
        actor.body={DetBodyMode2D::MovingObstacle,{q(2),q(2)},{},{},8,8,false};level.actors.push_back(actor);}
    require(validateDetLevel2D(level,error),error);const auto text=encodeDetLevel2D(level);DetLevel2D roundtrip;
    require(decodeDetLevel2D(text,roundtrip,error) && roundtrip.paths.size()==8,"Eight policies/16 unique path schemas accepted");
    auto unused=level;unused.actors.pop_back();require(!validateDetLevel2D(unused,error),"Unused path policy rejected");
    DetLevel2DOptions conflict;conflict.pathPreSystemBase=20;require(!validateDetLevel2D(level,error,conflict),"Path/character ordering conflict");
    level.paths.push_back(level.paths.back());require(!validateDetLevel2D(level,error),"Ninth path is rejected before allocation/registration");
    std::cout<<"PASS real8-path resource/setup boundary and independent registration ordering\n";
}
void closureChecks(const fs::path& directory){const auto level=simple();DeterministicSession baseline(detLevelSessionConfig(level));require(configure(baseline,level),baseline.error());
    std::vector<Bytes> states{bytes(baseline)};std::vector<DetSessionCheckpoint> cps{*checkedCp(baseline)};
    for(unsigned n=1;n<=8;++n){require(baseline.advance(frame(n-1,level,D::fromInt(1))),baseline.error());const auto cp=*checkedCp(baseline);
        require(cp.actors.at(10).pose.position[0]==q(int(n)).bits() && cp.actors.at(10).pose.position[1]==q(2).bits(),"Independent dyadic loaded-level motion");
        states.push_back(encodeDetCheckpoint(cp));cps.push_back(cp);}
    DeterministicSession restored(detLevelSessionConfig(level));require(configure(restored,level) && restored.restore(cps[4]),restored.error());
    for(unsigned n=5;n<=8;++n){require(restored.advance(frame(n-1,level,D::fromInt(1))),restored.error());require(bytes(restored)==states[n],"Loaded-level restore exact CP continuation");}
    DeterministicSession recording(detLevelSessionConfig(level));require(configure(recording,level),recording.error());DetReplayWriter writer;
    require(writer.begin(recording,(directory/"level.rpl").string(),2),writer.error());
    for(unsigned n=1;n<=8;++n){require(writer.advance(frame(n-1,level,D::fromInt(1))),writer.error());require(bytes(recording)==states[n],"Real resource .rpl recording CP");}require(writer.finish(),writer.error());
    DeterministicSession playback(detLevelSessionConfig(level));require(configure(playback,level),playback.error());DetReplayReader reader;
    const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"Real loaded-level .rpl playback full CP");}
    require(reader.seek(playback,4) && bytes(playback)==states[4],"Real loaded-level seek");write(directory/"final.state",states.back());
    std::cout<<"PASS loaded8tick hand motion, complete CP restore continuation, actual sealed .rpl playback and seek\n";
}
void resourceChecks(const DetLevel2D& level,const fs::path& directory){const auto& c=level.character;
    require(c.advanced && c.collision.continuousTerrain && !c.collision.stepUp,"Resource hand oracle requires the declared terrain profile");
    DetTypedStateSchema schema{c.stateSchema,kDetCharacterController2DTerrainProfileVersion,{
        {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},
        {DetCharacterVelocity,V{}},{DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
        {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},{DetCharacterIntentVelocity,V{}},
        {DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{0}},
        {DetCharacterCoyoteRemaining,std::uint32_t{0}},{DetCharacterJumpBufferRemaining,std::uint32_t{0}},
        {DetCharacterJumpHeld,false},{DetCharacterDropHeld,false},{DetCharacterCrouchHeld,false},
        {DetCharacterDropStarted,false},{DetCharacterCrouched,false},{DetCharacterStandingCaptured,false},
        {DetCharacterStandingHalf,V{}},{DetCharacterStandingOffset,V{}},{DetCharacterDropRemaining,std::uint32_t{0}},
        {DetCharacterCoyoteSupportY,D{}},{DetCharacterJumpLatch,false},{DetCharacterDropLatch,false},
        {DetCharacterDropTick,std::uint64_t{0}},{DetCharacterGroundNormal,V{}}}};
    const DetStateLayout characters(schema),bodies(detCollisionBodySchema2D(c.collision));
    DeterministicSession baseline(detLevelSessionConfig(level));require(configure(baseline,level),baseline.error());
    std::vector<Bytes> states{bytes(baseline)};std::vector<DetSessionCheckpoint> cps{*checkedCp(baseline)};
    for(unsigned n=1;n<=64;++n){const auto input=frame(n-1,level,D::fromInt(1));if(!baseline.advance(input)){
        write(directory/"failed-before.state",states.back());write(directory/"failed-input.bin",encodeDetInput(input));
        throw std::runtime_error("Resource inputTick="+std::to_string(n-1)+": "+baseline.error());}
        const auto cp=*checkedCp(baseline);const auto& hero=cp.actors.at(12);const auto& state=hero.blocks.at(c.stateSchema);const auto& mover=cp.actors.at(130);
        // Separate hand calculation: own +1 plus carrier +1 moves X2/64;
        // wedge rise/run1/2 makes own travel raise the foot1/128 per tick.
        require(hero.pose.position[0]==(D::fromInt(21)+q(int(2*n),64)).bits()
            && hero.pose.position[1]==(q(7,8)+q(int(n),128)).bits(),"Moving slope resource exact dyadic XY row="+std::to_string(n));
        require(mover.pose.position[0]==(D::fromInt(22)+q(int(n),64)).bits() && mover.pose.position[1]==D::fromInt(1).bits(),"Real resource path target row="+std::to_string(n));
        const auto own=std::get<V>(characters.read(state,DetCharacterVelocity)),carry=std::get<V>(characters.read(state,DetCharacterSupportVelocity));
        require(own.bits()==V{D::fromInt(1),{}}.bits() && carry.bits()==V{D::fromInt(1),{}}.bits(),"Hand own+carrier decomposition row="+std::to_string(n));
        require(std::get<bool>(characters.read(state,DetCharacterGrounded)) && std::get<DetEntityRef>(characters.read(state,DetCharacterSupport)).value==130
            && std::get<std::uint64_t>(characters.read(state,DetCharacterSupportTick))==n
            && std::get<V>(characters.read(state,DetCharacterGroundNormal)).bits()==V{D::fromInt(-2),D::fromInt(4)}.bits(),"Real moving slope support/normal/stamp row="+std::to_string(n));
        require(std::get<V>(bodies.read(hero.blocks.at(c.collision.bodySchema),DetBodyVelocity)).bits()==V{D::fromInt(2),q(1,2)}.bits(),"Hand moving slope world tangent row="+std::to_string(n));
        states.push_back(encodeDetCheckpoint(cp));cps.push_back(cp);}
    DeterministicSession restored(detLevelSessionConfig(level));require(configure(restored,level) && restored.restore(cps[32]),restored.error());
    for(unsigned n=33;n<=64;++n){require(restored.advance(frame(n-1,level,D::fromInt(1))),restored.error());require(bytes(restored)==states[n],"Real level moving support restore full CP row="+std::to_string(n));}
    std::cout<<"PASS real resource64tick independent moving-slope own/carry/tangent/XY/support and complete CP restore continuation\n";
}
std::string run(const DetLevel2D& level,unsigned ticks,D speed,const fs::path& directory){DeterministicSession session(detLevelSessionConfig(level));require(configure(session,level),session.error());
    std::uint64_t events=0;for(unsigned n=0;n<ticks;++n){const auto before=bytes(session);const auto input=frame(n,level,speed);
        if(!session.advance(input)){if(!directory.empty()){write(directory/"failed-before.state",before);write(directory/"failed-input.bin",encodeDetInput(input));}
            throw std::runtime_error("Level advance failed at inputTick="+std::to_string(n)+": "+session.error());}events+=checkedCp(session)->pendingEvents.size();}
    const auto cp=*checkedCp(session);std::ostringstream out;out.imbue(std::locale::classic());
    out<<"{\"schema\":1,\"status\":\"passed\",\"levelProfile\":1,\"ticks\":"<<ticks<<",\"bodies\":"<<level.actors.size()<<",\"paths\":"<<level.paths.size()
        <<",\"contentHash\":\""<<detLevelSessionConfig(level).contentHash<<"\",\"finalHash\":\""<<detCheckpointHash(cp)<<"\",\"pendingEventRows\":"<<events<<",\"actors\":[";
    bool comma=false;for(const auto& actor:level.actors){if(comma)out<<',';comma=true;const auto& pose=cp.actors.at(actor.id).pose;
        out<<"{\"id\":\""<<actor.id<<"\",\"xBits\":"<<pose.position[0]<<",\"yBits\":"<<pose.position[1]<<'}';}out<<"]}\n";
    if(!directory.empty()){write(directory/"loaded.dlt",encodeDetLevel2D(level));write(directory/"loaded-final.state",encodeDetCheckpoint(cp));write(directory/"summary.json",out.str());}return out.str();}
unsigned number(const char* text){std::string_view value(text);unsigned result=0;const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);
    if(error!=std::errc{} || end!=value.data()+value.size() || result>4096)throw std::invalid_argument("--ticks must be0..4096");return result;}
D bits(const char* text){std::string_view value(text);std::uint32_t result=0;const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result,16);
    if(value.size()!=8 || error!=std::errc{} || end!=value.data()+value.size() || !D::fromBits(result).isFinite())throw std::invalid_argument("--speed requires8 finite hex bits");return D::fromBits(result);}
} // namespace
int main(int argc,char** argv){fs::path directory;try{fs::path levelFile,parent;unsigned ticks=128;D speed=D::fromInt(1);bool checks=false,seenTicks=false,seenSpeed=false;
    for(int n=1;n<argc;++n){const std::string flag=argv[n];if(flag=="--checks"){if(checks)throw std::invalid_argument("Duplicate --checks");checks=true;continue;}
        if(n+1==argc)throw std::invalid_argument("Missing CLI argument");if(flag=="--level" && levelFile.empty())levelFile=argv[++n];
        else if(flag=="--output" && parent.empty())parent=argv[++n];else if(flag=="--ticks" && !seenTicks){seenTicks=true;ticks=number(argv[++n]);}
        else if(flag=="--speed" && !seenSpeed){seenSpeed=true;speed=bits(argv[++n]);}else throw std::invalid_argument("Unknown/duplicate CLI option");}
    if(levelFile.empty())throw std::invalid_argument("usage: --level FILE.dlt [--ticks0..4096 --speed HEXBITS --output EXISTING_PARENT --checks]");
    if(!parent.empty())directory=fresh(parent);else if(checks)directory=fresh(fs::temp_directory_path());
    if(checks){parserChecks(directory);identityChecks();bodyCountChecks();pathCountChecks();closureChecks(directory);}
    DetLevel2D level;std::string error;require(loadDetLevel2D(levelFile,level,error),error);if(checks)resourceChecks(level,directory);std::cout<<run(level,ticks,speed,directory);return 0;
}catch(const std::exception& e){if(!directory.empty())try{write(directory/"failure.json","{\"status\":\"failed\",\"error\":"+quote(e.what())+"}\n");}catch(...){}
    std::cerr<<e.what()<<'\n';return 1;}}
