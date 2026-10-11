// design reference: AYEntity/design.md Stage48 complete AYDLEVEL5 vitality content.
// Selected HP, route and endpoint rows are hand-derived. Whole-CP comparisons
// prove execution-path equality, not an independently generated all-byte golden.
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
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetLevelActor2D actor(SimEntityId id,DetBodyMode2D mode,V position,V half,std::uint32_t layer,std::uint32_t mask,bool trigger=false){
    DetLevelActor2D a;a.id=id;a.position=position;a.body={mode,half,{},{},layer,mask,trigger};return a;}
// Literal prior fixtures: their existing measured semantic pins belong to these
// exact recipes only, not to arbitrary files sharing a format version.
DetLevel2D old3(){DetLevel2D l;l.formatVersion=3;l.applicationVersion=42;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;
    c.advanced=true;c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(8),{}},{q(2),q(2)},2,1),
        actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true),actor(40,DetBodyMode2D::Static,{D::fromInt(2),{}},{q(2),q(2)},2,1,true)};
    l.actionTargets={{20,1,true,0},{30,1,true,0},{40,1,true,0}};l.counters={{1,0,2,0}};l.objectives={{1,{{1,1}}},{2,{{1,2}}}};
    l.claims={{1,30,10,1,1},{2,40,10,1,1}};l.flowBindings={{1,0},{2,1}};l.objectiveActions={{1,2,{20,DetActionKind2D::CollisionDisable},true}};return l;}
DetLevel2D old2(){DetLevel2D l;l.formatVersion=2;l.applicationVersion=36;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;c.advanced=true;
    c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    auto platform=actor(40,DetBodyMode2D::MovingObstacle,{D::fromInt(10),{}},{q(1),q(1)},4,8);platform.path=1;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(4),{}},{q(2),q(2)},2,1),actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true),platform};
    l.paths={{1,DetPathPlayback2D::PingPong,{{{},4,0},{{D::fromInt(1),{}},4,0}}}};l.actionTargets={{20,1,true,0},{40,8,true,1}};
    l.rules={{1,30,10,DetTriggerPhase2D::Enter,{20,DetActionKind2D::CollisionDisable},true,0,2},
        {2,30,10,DetTriggerPhase2D::Exit,{40,DetActionKind2D::PathPause},false,4,0}};return l;}
DetLevel2D old4(){auto l=old3();l.formatVersion=4;l.applicationVersion=46;l.character.collision.characterRespawn=true;
    l.actionTargets.pop_back();l.counters={{1,0,1,0}};l.objectives={{1,{{1,1}}}};l.claims.resize(1);l.flowBindings.resize(1);
    l.objectiveActions={{1,1,{20,DetActionKind2D::CollisionDisable},true}};
    l.respawnSpawns={{1,10,{D::fromInt(-1),{}}},{2,10,{D::fromInt(4),{}}}};l.respawnBindings={{10,1}};
    l.respawnRegions={{1,30,10,DetRespawnFlowRegionKind2D::Checkpoint,2},{2,40,10,DetRespawnFlowRegionKind2D::Death,0}};return l;}
DetLevel2D scene(bool occupied=false){auto l=old4();l.formatVersion=5;l.applicationVersion=48;l.actors[3].position.x=D::fromInt(1);
    l.respawnRegions.resize(1);l.vitalityActors={{10,201,5,5,0,5,true}};
    l.vitalityPermissions={{1,10,DetVitalityKind2D::Hurt,UINT64_MAX},{2,10,DetVitalityKind2D::Heal,UINT64_MAX}};
    l.vitalityRegions={{1,30,10,DetVitalityKind2D::Hurt,2},{2,40,10,DetVitalityKind2D::Hurt,3}};l.vitalityRespawnRecovery=true;
    if(occupied){l.actors.push_back(actor(70,DetBodyMode2D::Static,{D::fromInt(4),{}},{q(2),q(2)},2,1));l.actionTargets.push_back({70,1,true,0});}return l;}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,const DetTickInput& input){const bool ok=s.advance(input);require(ok,"tick "+std::to_string(input.tick)+": "+s.error());}
std::uint32_t mask(const DetSessionCheckpoint& cp,const DetLevel2D& l,SimEntityId id){return readDetState<std::uint32_t>(detCollisionBodySchema2D(l.character.collision),
    cp.actors.at(id).blocks.at(l.character.collision.bodySchema),DetBodyMask);}
void request(DetTickInput& input,std::uint8_t permission,std::uint64_t amount){Bytes payload{permission};
    for(unsigned n=0;n<8;++n)payload.push_back(static_cast<std::uint8_t>(amount>>(8*n)));
    input.commands.push_back({1,static_cast<std::uint32_t>(input.commands.size()),2,std::move(payload)});}
DetTickInput frame(std::uint64_t tick,bool reverse=false,bool occupied=false){const auto speed=static_cast<std::uint8_t>(tick<3?4:0);
    const auto manual=static_cast<std::uint8_t>(tick==5 || (!occupied && tick==3));DetTickInput i{tick,1,{{1,0,1,{speed,manual}},{1,1,99,{48}}}};
    if(tick==3 && !occupied)request(i,1,1);if(tick==4 && occupied)i.commands.push_back({1,2,3,{}});
    if(reverse)std::reverse(i.commands.begin(),i.commands.end());return i;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& l){const auto v=detLevelVitalityConfig2D(l);
    const auto actions=detLevelActionsConfig2D(l);std::optional<DetCharacterRespawnFlow2DConfig> routing;
    if(l.character.collision.characterRespawn)routing=detLevelRespawnFlowConfig2D(l);
    return s.registerSystem(4,l.character.collision.priority,[v,actions,routing,character=l.character](DetTickContext& ctx){
        for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
            if(cmd.type==1 && cmd.payload.size()==2){detCharacterInput2D(ctx,10,character,{D::fromUInt(cmd.payload[0]),false,false,false});
                if(routing)detCharacterRespawnFlowInput2D(ctx,10,*routing,cmd.payload[1]!=0);else if(cmd.payload[1])return false;}
            else if(cmd.type==2 && cmd.payload.size()==9){std::uint64_t amount=0;for(unsigned n=0;n<8;++n)amount|=std::uint64_t{cmd.payload[n+1]}<<(8*n);
                (void)executeDetVitality2D(ctx,v,cmd.payload[0],amount);}
            else if(cmd.type==3 && cmd.payload.empty())(void)executeDetAction2D(ctx,actions,{70,DetActionKind2D::CollisionDisable});
            else if(cmd.type!=99 || cmd.payload!=Bytes{48})return false;}return true;});}
void configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;if(!reverse)require(inputSystem(s,l),s.error());
    const bool installed=configureDetLevel2D(s,l,error);require(installed,error);if(reverse)require(inputSystem(s,l),s.error());const bool sealed=s.seal();require(sealed,s.error());}
void write(const fs::path& file,const std::string& text){std::ofstream out(file,std::ios::binary);out<<text;out.close();require(bool(out),"text output");}
void write(const fs::path& file,const Bytes& data){std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    out.close();require(bool(out),"byte output");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output existing parent required");const auto now=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto dir=parent/("level-vitality5-"+std::to_string(now)+"-"+std::to_string(n));if(fs::create_directory(dir))return dir;}throw std::runtime_error("fresh directory unavailable");}
std::string replace(std::string text,const std::string& before,const std::string& after){const auto at=text.find(before);require(at!=std::string::npos,"fixture token: "+before);
    text.replace(at,before.size(),after);return text;}
void legacyChecks(const fs::path& file){DetLevel2D first;std::string error;require(loadDetLevel2D(file,first,error),error);
    const std::vector<DetLevel2D> levels{first,old2(),old3(),old4()};const std::uint64_t hashes[]{UINT64_C(1216874329789544449),UINT64_C(8404432461347634428),UINT64_C(8120146628727633082),UINT64_C(5652620791972652914)};
    for(std::size_t n=0;n<levels.size();++n){const auto& l=levels[n];require(l.formatVersion==n+1 && detLevelSessionConfig(l).contentHash==hashes[n],"exact earlier fixture semantic pin");
        const auto text=encodeDetLevel2D(l);require(text.find("\nvitality")==std::string::npos,"earlier text has no appended life policy");DetLevel2D decoded;
        require(decodeDetLevel2D(text,decoded,error) && encodeDetLevel2D(decoded)==text,error);DeterministicSession s(detLevelSessionConfig(l));
        require(configureDetLevel2D(s,l,error),error);require(s.seal(),s.error());const auto cp=checkpoint(s);
        require(!cp.globals.contains(168) && !cp.globals.contains(176) && !cp.globals.contains(192) && !cp.globals.contains(208),"old formats install no new life schemas");}
    ++cases;std::cout<<"PASS exact format1/2/3/4 pins and absent new authority\n";}
void parserChecks(const fs::path& dir){const auto l=scene();const auto text=encodeDetLevel2D(l);DetLevel2D decoded;std::string error;
    require(decodeDetLevel2D(text,decoded,error),error);require(encodeDetLevel2D(decoded)==text,"complete format5 roundtrip");
    const auto refused=[&](const std::string& bad){require(!decodeDetLevel2D(bad,decoded,error) && !error.empty() && encodeDetLevel2D(decoded)==text,"atomic invalid decode");++parserNegatives;};
    refused(replace(text,"AYDLEVEL 5","AYDLEVEL 6"));refused(replace(text,"vitality 32 1 1 2 2","vitality 32 1 9 2 2"));
    refused(replace(text,"vitality 32 1 1 2 2","vitality 32 1 1 17 2"));refused(replace(text,"vitality 32 1 1 2 2","vitality 32 1 1 2 17"));
    refused(replace(text,"health 10 201 5 5 0 5 1","health 10 201 18446744073709551616 5 0 5 1"));
    refused(replace(text,"health 10 201 5 5 0 5 1","health 10 201 5 5 0 5 2"));refused(text+"trailing");
    const auto cooked=encodeDetLevelCooked2D(l);DetLevel2D fromCooked;require(decodeDetLevelCooked2D(cooked,fromCooked,error) && encodeDetLevel2D(fromCooked)==text,"existing cooked envelope1 carries Level5");
    write(dir/"vitality.dlt",text);write(dir/"vitality.dlc",cooked);++cases;std::cout<<"PASS bounded atomic Level5 text/cooked parsing\n";}
void semanticChecks(){const auto l=scene();std::string error;const auto refused=[&](const DetLevel2D& bad,const DetLevel2DOptions& options=DetLevel2DOptions{}){
    require(!validateDetLevel2D(bad,error,options) && !error.empty(),"invalid composite authoring");++semanticNegatives;};
    auto bad=l;bad.formatVersion=4;refused(bad);bad=l;bad.vitalityActors[0].maximumHP=0;refused(bad);bad=l;bad.vitalityActors[0].initialHP=6;refused(bad);
    bad=l;bad.vitalityActors[0].restoredHP=0;refused(bad);bad=l;bad.vitalityActors[0].invulnerabilityTicks=65536;refused(bad);
    bad=l;bad.vitalityActors[0].actor=30;refused(bad);bad=l;bad.vitalityPermissions[0].actor=20;refused(bad);bad=l;bad.vitalityRegions[0].trigger=20;refused(bad);
    bad=l;bad.vitalityRespawnRecovery=false;refused(bad);bad=l;bad.vitalityActors.clear();refused(bad);
    auto o=DetLevel2DOptions{};o.vitalityProgressStateSchema=96;refused(l,o);o={};o.vitalityRecoveryStateSchema=168;refused(l,o);
    o={};o.vitalitySystemId=7;refused(l,o);o={};o.vitalityPostSystemId=40;refused(l,o);o={};o.vitalityPolicySchemaBase=UINT32_MAX;refused(l,o);
    // Exact factory-reserved namespace conflict is a true composition rejection,
    // not an assertion that every individual table maximum composes.
    o={};o.identitySchemaBase=192;refused(l,o);
    auto empty=old4();empty.formatVersion=5;require(validateDetLevel2D(empty,error),error);DeterministicSession s(detLevelSessionConfig(empty));require(configureDetLevel2D(s,empty,error),error);require(s.seal(),s.error());
    const auto cp=checkpoint(s);require(!cp.globals.contains(168) && !cp.globals.contains(176) && !cp.globals.contains(192) && !cp.globals.contains(208),"empty life installs no optional schemas");
    ++cases;std::cout<<"PASS table/reference/options/order composition rejection and empty life\n";}
void healthOnlyChecks(){auto l=scene();l.character.collision.characterRespawn=false;l.respawnSpawns.clear();l.respawnBindings.clear();l.respawnRegions.clear();
    l.actionTargets.clear();l.rules.clear();l.counters.clear();l.objectives.clear();l.claims.clear();l.flowBindings.clear();l.objectiveActions.clear();
    l.vitalityRespawnRecovery=false;l.vitalityActors[0]={10,201,UINT64_MAX,UINT64_MAX-1,0,0,false};l.vitalityRegions.clear();
    const auto c=detLevelVitalityConfig2D(l);require(c.progress.actions.targets.empty() && c.progress.objectives.empty() && c.progress.claims.empty()
        && detActionsStateSchema2D(c.progress.actions).version==1 && detVitalityStateSchemas2D(c).size()==1,"HP-only shares zero-target Actions1 and Progress1");
    DeterministicSession s(detLevelSessionConfig(l));configure(s,l);auto input=frame(0);input.commands[0].payload={0,0};request(input,2,UINT64_MAX);advance(s,input);
    require(detVitalityState2D(checkpoint(s),c,10).hp==UINT64_MAX && detVitalityResult2D(checkpoint(s),c).actual==1,"exact bounded full UInt64 heal");
    input=frame(1);input.commands[0].payload={0,0};request(input,1,UINT64_MAX);advance(s,input);const auto dead=checkpoint(s);
    require(detVitalityState2D(dead,c,10).hp==0 && detVitalityState2D(dead,c,10).deathCount==1 && !dead.globals.contains(208),"health-only lethal never auto-respawns");
    ++cases;std::cout<<"PASS HP-only zero authority, exact UInt64 and no recovery\n";}
void handRow(const DetSessionCheckpoint& cp,const DetLevel2D& l,unsigned t){const auto v=detLevelVitalityConfig2D(l);const auto r=detLevelRespawnFlowConfig2D(l);
    const auto hp=detVitalityState2D(cp,v,10);const auto recovery=detVitalityRecoveryState2D(cp,v,10);const auto flow=detCharacterRespawnFlowState2D(cp,10,r);
    require(hp.hp==(t==1?3u:5u) && hp.deathCount==(t<2?0u:1u),"hand HP5,3,5 and one positive-to-zero death");
    require(recovery.restoreCount==(t<2?0u:t<3?1u:t<5?2u:3u) && recovery.reviveCount==(t<2?0u:1u)
        && recovery.restoreWriteCount==(t<2?0u:t<3?1u:2u),"accepted, manual and equal-HP restore counts are distinct");
    require(flow.activeSpawn==(t<1?1u:2u) && flow.externalDeathCount==(t<2?0u:1u) && flow.deathCount==(t<2?0u:1u)
        && flow.manualCount==(t<3?0u:t<5?1u:2u) && flow.requestCount==(t<2?0u:t<3?1u:t<5?2u:3u),"real checkpoint then producer6 external Death; Manual distinct");
    require(cp.actors.at(10).pose.position[0]==D::fromUInt(t<2?t:4).bits() && cp.actors.at(10).pose.position[1]==0,"hand endpoints0,1,4 then stationary");
    const auto gameplay=detLevelProgressConfig2D(l);require(detProgressCounter2D(cp,gameplay,1)==(t<1?0u:1u)
        && mask(cp,l,20)==(t<1?1u:0u) && mask(cp,l,30)==(t<1?1u:0u),"earlier collectible and objective gate content stays active");
    if(t==2 || t==3 || t==5){const auto bottom=detCharacterRespawnState2D(cp,10,detLevelRespawnConfig2D(l));
        require(bottom.result==DetRespawnResult2D::Accepted && bottom.resultTick==t+1 && bottom.spawn==2
            && flow.lastRequestCode==DetRespawnRequestCode2D::Queued && flow.lastRequestTick==t,"fresh queued route and actual Accepted are separate records");}}
void closureChecks(const fs::path& dir){const auto l=scene();DeterministicSession base(detLevelSessionConfig(l)),other(detLevelSessionConfig(l));configure(base,l);configure(other,l,true);
    std::vector<DetSessionCheckpoint> cps{checkpoint(base)};std::vector<Bytes> states{bytes(base)};std::vector<DetConfirmedEvent> events;
    for(unsigned t=0;t<8;++t){advance(base,frame(t));advance(other,frame(t,true));require(bytes(base)==bytes(other),"full opposite registration/input order CP");++fullComparisons;
        const auto cp=checkpoint(base);handRow(cp,l,t);cps.push_back(cp);states.push_back(encodeDetCheckpoint(cp));for(const auto& e:cp.pendingEvents)events.push_back({1,t,e});}
    const auto v=detLevelVitalityConfig2D(l);require(detVitalityStateSchemas2D(v).size()==2 && detVitalityStateSchemas2D(v)[0].version==2
        && detCharacterRespawnFlowStateSchemas2D(detLevelRespawnFlowConfig2D(l)).size()==2,"explicit Vitals2/Flow2 optional namespaces");
    require(base.restore(cps[3]),base.error());for(unsigned t=3;t<8;++t){advance(base,frame(t));require(bytes(base)==states[t+1],"restore after accepted never repeats recovery");++fullComparisons;}
    DeterministicSession corrected(detLevelSessionConfig(l));configure(corrected,l);DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};DeterministicRollback rollback(corrected,rc);
    for(unsigned t=0;t<8;++t){const auto i=frame(t);if(t==3){require(rollback.advance(),rollback.error());require(rollback.submit(1,i),rollback.error());}
        else require(rollback.submit(1,i) && rollback.advance(),rollback.error());require(bytes(corrected)==states[t+1],"real late manual+Hurt input corrected entire CP");++fullComparisons;}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"real confirmed effects exactly once");
    DeterministicSession recording(detLevelSessionConfig(l));configure(recording,l);DetReplayWriter writer;const bool began=writer.begin(recording,(dir/"vitality.rpl").string(),2);require(began,writer.error());
    for(unsigned t=0;t<8;++t){const bool ok=writer.advance(frame(t));require(ok,writer.error());require(bytes(recording)==states[t+1],"actual recording complete state");++fullComparisons;}
    const bool finished=writer.finish();require(finished,writer.error());DeterministicSession playback(detLevelSessionConfig(l));configure(playback,l);DetReplayReader reader;
    const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"actual playback complete state");++fullComparisons;}
    require(reader.seek(playback,3) && bytes(playback)==states[3],"seek fresh accepted boundary");++fullComparisons;
    require(reader.seek(playback,8) && bytes(playback)==states[8],"seek final equal-HP restoration");++fullComparisons;write(dir/"final.state",states.back());
    ++cases;std::cout<<"PASS8tick selected hand HP/route/position plus full-state restore/rollback/effects/rpl/2seek\n";}
void denialChecks(){const auto l=scene(true);const auto v=detLevelVitalityConfig2D(l);const auto route=detLevelRespawnFlowConfig2D(l);const auto respawn=detLevelRespawnConfig2D(l);
    DeterministicSession s(detLevelSessionConfig(l));configure(s,l);for(unsigned t=0;t<8;++t){advance(s,frame(t,false,true));const auto cp=checkpoint(s);
        if(t>=2){const auto hp=detVitalityState2D(cp,v,10);const auto recovered=detVitalityRecoveryState2D(cp,v,10);const auto r=detCharacterRespawnFlowState2D(cp,10,route);
            require(hp.hp==(t<5?0u:5u) && r.requestCount==(t<5?1u:2u) && recovered.restoreCount==(t<5?0u:1u)
                && recovered.reviveCount==(t<5?0u:1u),"Occupied stays HP0; new explicit opted manual after clearing obstruction restores once");
            const auto b=detCharacterRespawnState2D(cp,10,respawn);require(b.result==(t<5?DetRespawnResult2D::Occupied:DetRespawnResult2D::Accepted),"actual normal deny/accept result");}}
    auto notOpted=scene();notOpted.vitalityActors[0].recoverManual=false;DeterministicSession other(detLevelSessionConfig(notOpted));configure(other,notOpted);
    for(unsigned t=0;t<4;++t)advance(other,frame(t));const auto cp=checkpoint(other);const auto cfg=detLevelVitalityConfig2D(notOpted);
    require(detVitalityState2D(cp,cfg,10).hp==4 && detVitalityRecoveryState2D(cp,cfg,10).restoreCount==1,"alive Manual Accepted without opt-in never restores HP");
    ++cases;std::cout<<"PASS occupied/no retry/fresh request and per-actor Manual opt-in\n";}
void identityChecks(){const auto l=scene();auto recipe=detLevelSessionConfig(l);recipe.contentHash=0;DeterministicSession s(recipe);configure(s,l);const auto initial=checkpoint(s);
    for(unsigned n=0;n<4;++n){auto changed=l;if(n==0)changed.vitalityActors[0].maximumHP=6;else if(n==1)changed.vitalityActors[0].recoverManual=false;
        else if(n==2)changed.vitalityRegions[1].amount=4;else changed.vitalityActors[0].restoredHP=4;DeterministicSession foreign(recipe);configure(foreign,changed);const auto before=bytes(foreign);
        require(s.manifest()!=foreign.manifest() && !foreign.restore(initial) && bytes(foreign)==before,"complete HP/Manual/region/restoration policy identity despite equalcontentHash");++fullComparisons;}
    advance(s,frame(0));advance(s,frame(1));advance(s,frame(2));const auto accepted=checkpoint(s);const auto before=bytes(s);const auto c=detLevelVitalityConfig2D(l);
    const auto schemas=detVitalityStateSchemas2D(c);for(unsigned n=0;n<3;++n){auto bad=accepted;if(n==0)writeDetState(schemas[0],bad.globals.at(c.stateSchema),detVitalityActorField2D(c,10,DetVitalityHP),std::uint64_t{4});
        else if(n==1)writeDetState(schemas[1],bad.globals.at(c.recoveryStateSchema),detVitalityRecoveryField2D(c,10,DetVitalityLethalPending),true);
        else bad.globals.at(c.policySchemaBase).at(3)^=1;require(!s.restore(bad) && bytes(s)==before,"atomic forged HP/pending/literal policy rejection");}
    ++cases;std::cout<<"PASS full literal life identity and malformed committed restore\n";}
} // namespace
int main(int argc,char** argv){try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string a=argv[n];if((a=="--output" || a=="--legacy") && n+1<argc){
    if(a=="--output")parent=argv[++n];else legacy=argv[++n];}else throw std::invalid_argument("usage: --output existingParent --legacy format1TerrainFile");}
    require(!parent.empty() && !legacy.empty(),"output and legacy required");const auto dir=fresh(parent);legacyChecks(legacy);parserChecks(dir);semanticChecks();healthOnlyChecks();closureChecks(dir);denialChecks();identityChecks();
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)+",\"parserNegatives\":"+std::to_string(parserNegatives)
        +",\"semanticNegatives\":"+std::to_string(semanticNegatives)+",\"completeCheckpointComparisons\":"+std::to_string(fullComparisons)
        +",\"ticks\":8,\"oracleScope\":\"selected hand HP/route/position; full CP execution equivalence\",\"contentHash\":\""+std::to_string(detLevelSessionConfig(scene()).contentHash)+"\"}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;}catch(const std::exception& e){std::cerr<<"FAIL levelvitality5: "<<e.what()<<'\n';return 1;}}
