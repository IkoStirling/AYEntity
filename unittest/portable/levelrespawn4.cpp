// design reference: AYEntity/design.md Stage46 complete AYDLEVEL4 respawn content.
// Dyadic movement/count assertions are independent; whole-checkpoint comparisons
// verify these execution paths agree, not a separately generated all-byte golden.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
unsigned cases=0,parserNegatives=0,semanticNegatives=0,fullComparisons=0;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
D q(int n,int d=4){return D::fromInt(n)/D::fromInt(d);}
DetLevelActor2D actor(SimEntityId id,DetBodyMode2D mode,V position,V half,std::uint32_t layer,std::uint32_t mask,bool trigger=false){
    DetLevelActor2D a;a.id=id;a.position=position;a.body={mode,half,{},{},layer,mask,trigger};return a;}
DetLevel2D old3(){DetLevel2D l;l.formatVersion=3;l.applicationVersion=42;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;
    c.advanced=true;c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(8),{}},{q(2),q(2)},2,1),
        actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true),actor(40,DetBodyMode2D::Static,{D::fromInt(2),{}},{q(2),q(2)},2,1,true)};
    l.actionTargets={{20,1,true,0},{30,1,true,0},{40,1,true,0}};l.counters={{1,0,2,0}};l.objectives={{1,{{1,1}}},{2,{{1,2}}}};
    l.claims={{1,30,10,1,1},{2,40,10,1,1}};l.flowBindings={{1,0},{2,1}};
    l.objectiveActions={{1,2,{20,DetActionKind2D::CollisionDisable},true}};return l;}
DetLevel2D old2(){DetLevel2D l;l.formatVersion=2;l.applicationVersion=36;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;c.advanced=true;
    c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    auto platform=actor(40,DetBodyMode2D::MovingObstacle,{D::fromInt(10),{}},{q(1),q(1)},4,8);platform.path=1;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(4),{}},{q(2),q(2)},2,1),actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true),platform};
    l.paths={{1,DetPathPlayback2D::PingPong,{{{},4,0},{{D::fromInt(1),{}},4,0}}}};l.actionTargets={{20,1,true,0},{40,8,true,1}};
    l.rules={{1,30,10,DetTriggerPhase2D::Enter,{20,DetActionKind2D::CollisionDisable},true,0,2},
        {2,30,10,DetTriggerPhase2D::Exit,{40,DetActionKind2D::PathPause},false,4,0}};return l;}
DetLevel2D scene(){auto l=old3();l.formatVersion=4;l.applicationVersion=46;l.character.collision.characterRespawn=true;
    l.actionTargets.pop_back();l.counters={{1,0,1,0}};l.objectives={{1,{{1,1}}}};l.claims.resize(1);l.flowBindings.resize(1);
    l.objectiveActions={{1,1,{20,DetActionKind2D::CollisionDisable},true}};
    l.respawnSpawns={{1,10,{D::fromInt(-1),{}}},{2,10,{D::fromInt(4),{}}}};
    l.respawnBindings={{10,1}};l.respawnRegions={{1,30,10,DetRespawnFlowRegionKind2D::Checkpoint,2},{2,40,10,DetRespawnFlowRegionKind2D::Death,0}};return l;}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,const DetTickInput& input){if(!s.advance(input))throw std::runtime_error("advance tick "+std::to_string(input.tick)+": "+s.error());}
std::uint32_t mask(const DetSessionCheckpoint& cp,const DetLevel2D& l,SimEntityId id){return readDetState<std::uint32_t>(detCollisionBodySchema2D(l.character.collision),
    cp.actors.at(id).blocks.at(l.character.collision.bodySchema),DetBodyMask);}
DetTickInput frame(std::uint64_t t,bool reverse=false,bool denied=false){const auto speed=static_cast<std::uint8_t>(t<(denied?3u:4u)?4:0);
    const auto manual=static_cast<std::uint8_t>(denied?t==6:t==3 || t==5);DetTickInput i{t,1,{{1,0,1,{speed,manual}},{1,1,2,{46}}}};
    if(reverse)std::reverse(i.commands.begin(),i.commands.end());return i;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& l){const auto routing=detLevelRespawnFlowConfig2D(l);return s.registerSystem(6,l.character.collision.priority,
    [character=l.character,routing](DetTickContext& ctx){for(const auto& c:ctx.input().commands){if(c.source!=1)return false;
        if(c.sequence==0 && c.type==1 && c.payload.size()==2){detCharacterInput2D(ctx,10,character,{D::fromUInt(c.payload[0]),false,false,false});
            detCharacterRespawnFlowInput2D(ctx,10,routing,c.payload[1]!=0);}
        else if(c.sequence!=1 || c.type!=2 || c.payload!=Bytes{46})return false;}return true;});}
void configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;if(!reverse)require(inputSystem(s,l),s.error());
    const bool ok=configureDetLevel2D(s,l,error);require(ok,error);if(reverse)require(inputSystem(s,l),s.error());const bool sealed=s.seal();require(sealed,s.error());}
void write(const fs::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;f.close();require(bool(f),"text evidence write");}
void write(const fs::path& p,const Bytes& data){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));f.close();require(bool(f),"byte evidence write");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output existing parent required");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto path=parent/("level-respawn4-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("fresh directory unavailable");}
std::string replace(std::string text,const std::string& from,const std::string& to){const auto pos=text.find(from);require(pos!=std::string::npos,"fixture token: "+from);text.replace(pos,from.size(),to);return text;}
void legacyChecks(const fs::path& file){DetLevel2D l;std::string error;require(loadDetLevel2D(file,l,error),error);
    require(l.formatVersion==1 && detLevelSessionConfig(l).contentHash==UINT64_C(1216874329789544449),"original format1 semantic pin");
    const auto v2=old2(),v3=old3();require(detLevelSessionConfig(v2).contentHash==UINT64_C(8404432461347634428)
        && detLevelSessionConfig(v3).contentHash==UINT64_C(8120146628727633082),"previous format2/3 semantic pins");
    for(const auto& old:{l,v2,v3}){const auto text=encodeDetLevel2D(old);require(text.find("\nrespawn")==std::string::npos,"legacy canonical text unchanged");
        DetLevel2D decoded;require(decodeDetLevel2D(text,decoded,error) && encodeDetLevel2D(decoded)==text,error);
        DeterministicSession s(detLevelSessionConfig(old));const bool installed=configureDetLevel2D(s,old,error);require(installed,error);require(s.seal(),s.error());
        require(!checkpoint(s).globals.contains(136) && !checkpoint(s).globals.contains(144),"legacy pipeline has no respawn schema");}
    ++cases;std::cout<<"PASS previous format1/2/3 identity pins and absent respawn authority\n";}
void parserChecks(const fs::path& dir){const auto l=scene();const auto text=encodeDetLevel2D(l);DetLevel2D decoded;std::string error;
    require(decodeDetLevel2D(text,decoded,error),error);require(encodeDetLevel2D(decoded)==text,"full format4 text roundtrip");
    const auto refused=[&](const std::string& bad,bool semantic){const bool ok=decodeDetLevel2D(bad,decoded,error);
        require(!ok && encodeDetLevel2D(decoded)==text,"failed decode output atomic");require(error.find(semantic?"semantic preflight":"line ")!=std::string::npos,"bounded syntax/semantic diagnostics");++parserNegatives;};
    refused(replace(text,"AYDLEVEL 4","AYDLEVEL 5"),false);refused(replace(text,"respawn 2 1 2","respawn 9 1 2"),false);
    refused(replace(text,"respawn 2 1 2","respawn 2 9 2"),false);refused(replace(text,"respawn 2 1 2","respawn 2 1 17"),false);
    refused(replace(text,"defaultspawn 10 1","defaultspawn 18446744073709551616 1"),false);refused(text+"trailing",false);
    refused(replace(text,"spawn 2 10 40800000 00000000","spawn 2 10 7f800000 00000000"),false);
    refused(replace(text,"defaultspawn 10 1","defaultspawn 10 9"),true);refused(replace(text,"region 1 30 10 0 2","region 1 20 10 0 2"),true);
    refused(replace(text,"region 2 40 10 1 0","region 2 40 10 1 2"),true);refused(replace(text,"region 2 40 10 1 0","region 2 40 10 2 0"),true);
    refused(replace(text,"spawn 2 10 40800000 00000000","spawn 2 30 40800000 00000000"),true);
    const auto cooked=encodeDetLevelCooked2D(l);DetLevel2D cookedLevel;require(decodeDetLevelCooked2D(cooked,cookedLevel,error) && encodeDetLevel2D(cookedLevel)==text,"existing envelope1 carries complete canonical format4");
    write(dir/"respawn.dlt",text);write(dir/"respawn.dlc",cooked);++cases;std::cout<<"PASS format4 text/cooked roundtrip and bounded atomic parsing\n";}
void semanticChecks(){const auto l=scene();std::string error;const auto refused=[&](const DetLevel2D& bad,const DetLevel2DOptions& options=DetLevel2DOptions{}){
        require(!validateDetLevel2D(bad,error,options) && !error.empty(),"invalid full composition");++semanticNegatives;};
    auto bad=l;bad.formatVersion=3;refused(bad);bad=l;bad.character.collision.characterRespawn=false;refused(bad);
    bad=l;bad.respawnSpawns.clear();refused(bad);bad=l;bad.respawnBindings.clear();refused(bad);
    bad=l;bad.respawnSpawns[1].id=1;refused(bad);bad=l;bad.respawnBindings[0].actor=30;refused(bad);
    auto o=DetLevel2DOptions{};o.respawnStateSchema=96;refused(l,o);o={};o.respawnFlowPolicySchemaBase=137;refused(l,o);
    o={};o.respawnFlowSystemId=8;refused(l,o);o={};o.respawnFlowValidatorId=72;refused(l,o);
    o={};o.respawnFlowSystemId=12;refused(l,o);o={};o.respawnFlowPolicySchemaBase=UINT32_MAX;refused(l,o);
    auto manual=l;manual.respawnRegions.clear();require(validateDetLevel2D(manual,error),"manual-only zero-region recipe: "+error);
    auto noRespawn=l;noRespawn.character.collision.characterRespawn=false;noRespawn.respawnSpawns.clear();noRespawn.respawnBindings.clear();noRespawn.respawnRegions.clear();
    require(validateDetLevel2D(noRespawn,error),"format4 zero-table flagfalse keeps ordinary pipeline: "+error);
    auto path=l;path.paths={{1,DetPathPlayback2D::PingPong,{{{},4,0},{{D::fromInt(1),{}},4,0}}}};
    auto p=actor(70,DetBodyMode2D::MovingObstacle,{D::fromInt(16),{}},{q(1),q(1)},4,8);p.path=1;path.actors.push_back(p);path.actionTargets.push_back({70,8,true,1});
    require(validateDetLevel2D(path,error),"complete path/respawn/Progress composition: "+error);auto actions=detLevelActionsConfig2D(path);
    require(detActionsStateSchema2D(actions).version==2 && detActionsPolicySchemas2D(actions)[0].version==2,"Actions2 descriptor explicitly selected");
    auto mismatched=actions;mismatched.targets.back().path->collision.characterRespawn=false;bool rejected=false;
    try{(void)encodeDetActionsConfig2D(mismatched);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"nested Path respawn flag identity is exact");
    ++cases;std::cout<<"PASS complete table/role/namespace/order budgets and Actions2 nested Path identity\n";}
void closureChecks(const fs::path& dir){const auto l=scene();const auto routing=detLevelRespawnFlowConfig2D(l);const auto respawn=detLevelRespawnConfig2D(l);
    const auto progress=detLevelProgressConfig2D(l);DeterministicSession base(detLevelSessionConfig(l)),other(detLevelSessionConfig(l));configure(base,l);configure(other,l,true);
    std::vector<DetSessionCheckpoint> cps{checkpoint(base)};std::vector<Bytes> states{bytes(base)};std::vector<DetConfirmedEvent> events;
    for(unsigned t=0;t<12;++t){advance(base,frame(t));advance(other,frame(t,true));require(bytes(base)==bytes(other),"complete opposite registration/input order bytes");++fullComparisons;
        auto cp=checkpoint(base);cps.push_back(cp);states.push_back(encodeDetCheckpoint(cp));const auto x=t<3?t:4u;
        require(cp.actors.at(10).pose.position[0]==D::fromUInt(x).bits() && cp.actors.at(10).pose.position[1]==0,"hand endpoint0,1,2,4 then stationary");
        const auto r=detCharacterRespawnFlowState2D(cp,10,routing);require(r.activeSpawn==(t<1?1u:2u) && r.checkpointCount==(t<1?0u:1u)
            && r.deathCount==(t<3?0u:1u) && r.manualCount==(t<5?0u:1u) && r.requestCount==(t<3?0u:t<5?1u:2u),"actual Enter checkpoint/death and one fresh manual attempt");
        require(detProgressCounter2D(cp,progress,1)==(t<1?0u:1u) && mask(cp,l,30)==(t<1?1u:0u) && mask(cp,l,20)==(t<1?1u:0u),"same recipe real collection and objective gate action");
        if(t==3 || t==5){const auto result=detCharacterRespawnState2D(cp,10,respawn);require(result.result==DetRespawnResult2D::Accepted && result.spawn==2
            && result.resultTick==t+1 && r.lastRequestTick==t && r.lastRequestCode==DetRespawnRequestCode2D::Queued,"queued routing distinguished from accepted collision result");
            DetCharacterState2D cleared;cleared.enabled=true;cleared.standingCaptured=true;cleared.standingHalf={q(1),q(1)};
            require(cp.actors.at(10).blocks.at(l.character.stateSchema)==detCharacterState2D(l.character,cleared)
                && readDetState<V>(detCollisionBodySchema2D(l.character.collision),cp.actors.at(10).blocks.at(l.character.collision.bodySchema),DetBodyVelocity)==V{},
                "accepted respawn clears complete controller motion/input state and body velocity");}
        for(const auto& event:cp.pendingEvents)events.push_back({1,t,event});}
    require(base.restore(cps[2]),"restore selected checkpoint before death Enter");for(unsigned t=2;t<12;++t){advance(base,frame(t));require(bytes(base)==states[t+1],"restored full route/action state continuation");++fullComparisons;}
    DeterministicSession corrected(detLevelSessionConfig(l));configure(corrected,l);DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};DeterministicRollback rollback(corrected,rc);
    for(unsigned t=0;t<12;++t){const auto i=frame(t);if(t==5){require(rollback.advance(),rollback.error());require(rollback.submit(1,i),rollback.error());}
        else require(rollback.submit(1,i) && rollback.advance(),rollback.error());require(bytes(corrected)==states[t+1],"real late manual pulse correction full bytes");++fullComparisons;}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"corrected effects confirmed exactly once");
    DeterministicSession recording(detLevelSessionConfig(l));configure(recording,l);DetReplayWriter writer;require(writer.begin(recording,(dir/"respawn.rpl").string(),2),writer.error());
    for(unsigned t=0;t<12;++t){require(writer.advance(frame(t)),writer.error());require(bytes(recording)==states[t+1],"real recording complete route state");++fullComparisons;}require(writer.finish(),writer.error());
    DeterministicSession playback(detLevelSessionConfig(l));configure(playback,l);DetReplayReader reader;require(reader.open(writer.path()),reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"real playback complete route state");++fullComparisons;}
    require(reader.seek(playback,2) && bytes(playback)==states[2],"seek selected checkpoint before death");++fullComparisons;write(dir/"final.state",states.back());
    ++cases;std::cout<<"PASS12ticks checkpoint/death/manual+collect/gate whole-state order/restore/late rollback/effects/rpl/seek\n";}
void arbitrationChecks(){auto l=scene();l.respawnRegions[1].trigger=30;const auto flow=detLevelRespawnFlowConfig2D(l);const auto respawn=detLevelRespawnConfig2D(l);
    DeterministicSession s(detLevelSessionConfig(l));configure(s,l);advance(s,frame(0));advance(s,frame(1));const auto cp=checkpoint(s);
    const auto state=detCharacterRespawnFlowState2D(cp,10,flow);require(state.activeSpawn==2 && state.checkpointCount==1 && state.deathCount==1 && state.requestCount==1
        && detCharacterRespawnState2D(cp,10,respawn).spawn==2 && cp.actors.at(10).pose.position[0]==D::fromInt(4).bits(),"same delivered Enter updates checkpoint before death arbitration");
    ++cases;std::cout<<"PASS checkpoint/death same-tick order uses newly selected spawn\n";}
void denialChecks(){auto l=scene();l.respawnSpawns[1].position.x=D::fromInt(6);l.actors.push_back(actor(50,DetBodyMode2D::Static,{D::fromInt(6),{}},{q(2),q(2)},2,1));
    const auto flow=detLevelRespawnFlowConfig2D(l);const auto respawn=detLevelRespawnConfig2D(l);DeterministicSession s(detLevelSessionConfig(l));configure(s,l);
    for(unsigned t=0;t<9;++t){advance(s,frame(t,false,true));const auto cp=checkpoint(s);const auto r=detCharacterRespawnFlowState2D(cp,10,flow);
        if(t>=3){require(cp.actors.at(10).pose.position[0]==D::fromInt(2).bits() && r.activeSpawn==2 && r.requestCount==(t<6?1u:2u)
            && r.manualCount==(t<6?0u:1u),"Occupied baseline remains inside death trigger; Stay no retry, fresh manual retries once");
            const auto result=detCharacterRespawnState2D(cp,10,respawn);require(result.result==DetRespawnResult2D::Occupied
                && result.resultTick==(t<6?4u:7u),"normal occupied denial has no pending deferred request");}}
    ++cases;std::cout<<"PASS occupied spawn keeps baseline/selected checkpoint and no Stay retry\n";}
void identityChecks(){const auto l=scene();auto recipe=detLevelSessionConfig(l);recipe.contentHash=0;DeterministicSession s(recipe);configure(s,l);const auto cp=checkpoint(s);const auto before=bytes(s);
    for(unsigned n=0;n<3;++n){auto changed=l;if(n==0)changed.respawnSpawns[1].position.x=D::fromInt(5);else if(n==1)changed.respawnBindings[0].spawn=2;else changed.respawnRegions[0].spawn=1;
        DeterministicSession foreign(recipe);configure(foreign,changed);const auto actual=bytes(foreign);require(s.manifest()!=foreign.manifest() && !foreign.restore(cp)
            && !foreign.faulted() && bytes(foreign)==actual,"complete spawn/default/region policy rejects equal-contentHash foreign state atomically");++fullComparisons;}
    auto corrupt=cp;const auto cfg=detLevelRespawnFlowConfig2D(l);const auto schema=detCharacterRespawnFlowStateSchema2D(cfg);
    auto defaults=corrupt.globals.at(cfg.stateSchema);const auto field=detCharacterRespawnFlowField2D(cfg,10,DetRespawnFlowActiveSpawn);
    const DetStateLayout layout(schema);layout.write(defaults,field,std::uint32_t{9});corrupt.globals.at(cfg.stateSchema)=defaults;
    require(!s.restore(corrupt) && bytes(s)==before,"unknown active spawn rejects atomic restore");
    unsigned hooks=0;DeterministicSession hooked(detLevelSessionConfig(l));std::string error;require(configureDetLevel2D(hooked,l,error,{},[&](DeterministicSession& destination){++hooks;
        require(!destination.registerTypedSchema(detCharacterRespawnFlowStateSchema2D(cfg)),"hook sees router already installed");
        return destination.registerTypedSchema({128,1,{{1,std::uint32_t{46}}}}) && destination.registerGlobalState(128);}),error);
    require(hooks==1 && hooked.seal() && checkpoint(hooked).actors.at(10).blocks.contains(128),"unchanged configuration hook before immutable actor copies");
    ++cases;std::cout<<"PASS full Level4 routing identity/malformed restore and destination-only hook\n";}
}
int main(int argc,char** argv){try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string a=argv[n];if((a=="--output" || a=="--legacy") && n+1<argc){
    if(a=="--output")parent=argv[++n];else legacy=argv[++n];}else throw std::invalid_argument("usage: --output existingParent --legacy format1TerrainFile");}
    require(!parent.empty() && !legacy.empty(),"output and legacy required");const auto dir=fresh(parent);legacyChecks(legacy);parserChecks(dir);semanticChecks();closureChecks(dir);arbitrationChecks();denialChecks();identityChecks();
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)+",\"parserNegatives\":"+std::to_string(parserNegatives)
        +",\"semanticNegatives\":"+std::to_string(semanticNegatives)+",\"completeCheckpointComparisons\":"+std::to_string(fullComparisons)
        +",\"ticks\":12,\"contentHash\":\""+std::to_string(detLevelSessionConfig(scene()).contentHash)+"\"}\n";write(dir/"summary.json",summary);std::cout<<summary;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL levelrespawn4: "<<e.what()<<'\n';return 1;}}
