// design reference: AYEntity/design.md Stage42 complete AYDLEVEL3 progress content.
// Integer timing/value assertions are independent. Complete checkpoint comparisons
// certify these execution paths agree, not an independently generated all-byte golden.
#include <AYEntity/DeterministicLevel2D.h>
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
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
DetLevelActor2D actor(SimEntityId id,DetBodyMode2D mode,V position,V half,std::uint32_t layer,std::uint32_t mask,bool trigger=false){
    DetLevelActor2D a;a.id=id;a.position=position;a.body={mode,half,{},{},layer,mask,trigger};return a;}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=3;l.applicationVersion=42;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;
    c.advanced=true;c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);
    c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(8),{}},{q(2),q(2)},2,1),
        actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true),
        actor(40,DetBodyMode2D::Static,{D::fromInt(2),{}},{q(2),q(2)},2,1,true)};
    l.actionTargets={{20,1,true,0},{30,1,true,0},{40,1,true,0}};
    l.counters={{1,0,2,0}};l.objectives={{1,{{1,1}}},{2,{{1,2}}}};
    l.claims={{1,30,10,1,1},{2,40,10,1,1}};l.flowBindings={{1,0},{2,1}};
    l.objectiveActions={{1,2,{20,DetActionKind2D::CollisionDisable},true}};return l;}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
std::uint32_t mask(const DetSessionCheckpoint& cp,const DetLevel2D& l,SimEntityId id){return readDetState<std::uint32_t>(
    detCollisionBodySchema2D(l.character.collision),cp.actors.at(id).blocks.at(l.character.collision.bodySchema),DetBodyMask);}
DetTickInput frame(std::uint64_t tick,bool reverse=false){DetTickInput i{tick,1,{{1,0,1,{static_cast<std::uint8_t>(tick<4?4:0)}},{1,1,2,{42}}}};
    if(reverse)std::reverse(i.commands.begin(),i.commands.end());return i;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& l){return s.registerSystem(6,l.character.collision.priority,[c=l.character](DetTickContext& ctx){
    for(const auto& command:ctx.input().commands){if(command.source!=1 || command.payload.size()!=1)return false;
        if(command.type==1 && command.sequence==0)detCharacterInput2D(ctx,10,c,{D::fromUInt(command.payload[0]),false,false,false});
        else if(command.type!=2 || command.sequence!=1 || command.payload[0]!=42)return false;}return true;});}
void configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;
    if(!reverse)require(inputSystem(s,l),s.error());
    const bool configured=configureDetLevel2D(s,l,error);require(configured,error);
    if(reverse)require(inputSystem(s,l),s.error());const bool sealed=s.seal();require(sealed,s.error());}
void advance(DeterministicSession& s,const DetTickInput& i){if(!s.advance(i))throw std::runtime_error("advance tick "+std::to_string(i.tick)+": "+s.error());}
void write(const fs::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;f.close();require(bool(f),"text evidence write");}
void write(const fs::path& p,const Bytes& data){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));f.close();require(bool(f),"byte evidence write");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto p=parent/("level-progress3-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}throw std::runtime_error("fresh directory unavailable");}
std::string replace(std::string text,const std::string& from,const std::string& to){const auto at=text.find(from);require(at!=std::string::npos,"fixture token present: "+from);text.replace(at,from.size(),to);return text;}
void parserChecks(const fs::path& dir){const auto l=scene();const auto text=encodeDetLevel2D(l);DetLevel2D decoded;std::string error;
    const bool decodedOk=decodeDetLevel2D(text,decoded,error);require(decodedOk,error);require(encodeDetLevel2D(decoded)==text,"all five tables roundtrip");
    const auto before=text;const auto refused=[&](const std::string& bad,bool semantic){const bool ok=decodeDetLevel2D(bad,decoded,error);
        require(!ok && encodeDetLevel2D(decoded)==before,"failed decode is output atomic");
        require(error.find(semantic?"semantic preflight":"line ")!=std::string::npos,"located syntax/semantic diagnostics");++parserNegatives;};
    refused(replace(text,"AYDLEVEL 3","AYDLEVEL 4"),false);
    refused(replace(text,"progress 1 2 2","progress 17 2 2"),false);
    refused(replace(text,"objective 1 1","objective 1 9"),false);
    refused(replace(text,"flow 2 1","flow 17 1"),false);
    refused(replace(text,"flow 2 1","flow 2 9"),false);
    refused(replace(text,"counter 1 0 2 0","counter 1 0 18446744073709551616 0"),false);
    refused(replace(text,"counter 1 0 2 0","counter 1 -1 2 0"),false);
    refused(replace(text,"objectiveaction 1 2 20 2 1","objectiveaction 1 2 20 2 2"),false);
    refused(text+"unknown",false);
    refused(replace(text,"counter 1 0 2 0","counter 1 0 2 3"),true);
    refused(replace(text,"condition 1 1","condition 9 1"),true);
    refused(replace(text,"claim 1 30 10 1 1","claim 1 31 10 1 1"),true);
    refused(replace(text,"claim 1 30 10 1 1","claim 1 30 20 1 1"),true);
    refused(replace(text,"claim 1 30 10 1 1","claim 1 30 10 1 0"),true);
    refused(replace(text,"binding 2 1","binding 2 9"),true);
    refused(replace(text,"binding 2 1","binding 9 1"),true);
    refused(replace(text,"objectiveaction 1 2 20 2 1","objectiveaction 1 2 20 5 1"),true);
    refused(replace(text,"objectiveaction 1 2 20 2 1","objectiveaction 1 2 99 2 1"),true);
    write(dir/"progress.dlt",text);require(loadDetLevel2D(dir/"progress.dlt",decoded,error),error);
    ++cases;std::cout<<"PASS format3 complete text/identity and bounded atomic parser diagnostics\n";
}
void semanticChecks(){const auto l=scene();std::string error;const auto refused=[&](const DetLevel2D& bad,const DetLevel2DOptions& options=DetLevel2DOptions{}){
    const bool ok=validateDetLevel2D(bad,error,options);require(!ok && !error.empty(),"invalid composite preflight");++semanticNegatives;};
    auto bad=l;bad.formatVersion=2;refused(bad);bad=l;bad.counters.clear();refused(bad);
    bad=l;bad.counters.clear();bad.objectives.clear();bad.claims.clear();refused(bad);
    bad=l;bad.character.collision.characterRespawn=true;refused(bad);
    bad=l;bad.objectives[0].conditions[0].atLeast=3;refused(bad);
    bad=l;bad.counters.push_back(bad.counters[0]);refused(bad);
    bad=l;bad.flowBindings[1].claim=1;refused(bad);
    bad=l;bad.actionTargets[1].collisionControl=false;refused(bad);
    auto options=DetLevel2DOptions{};options.progressStateSchema=64;refused(l,options);
    options={};options.progressPolicySchemaBase=32;refused(l,options);
    options={};options.flowCollectStateSchema=96;refused(l,options);
    options={};options.flowPolicySchemaBase=97;refused(l,options);
    options={};options.flowSystemId=20;refused(l,options);
    options={};options.flowValidatorId=64;refused(l,options);
    options={};options.flowPolicySchemaBase=UINT32_MAX;refused(l,options);
    bad=l;bad.rules={{1,30,10,DetTriggerPhase2D::Enter,{20,DetActionKind2D::CollisionDisable},false,0,0}};
    options={};options.flowSystemId=10;refused(bad,options);
    // The table maxima may form a semantic description over8KiB even though
    // every individual count is legal; do not advertise joint maximum capacity.
    bad=l;bad.character.collision.maxBodies=64;
    for(unsigned n=0;n<8;++n){DetLevelPath2D path;path.id=n+1;
        for(unsigned k=0;k<24;++k)path.waypoints.push_back({{q(int(k)),{}},4,0});bad.paths.push_back(path);
        auto platform=actor(100+n,DetBodyMode2D::MovingObstacle,{D::fromInt(20+int(n)*8),{}},{q(1),q(1)},4,8);platform.path=n+1;
        bad.actors.push_back(platform);bad.actionTargets.push_back({platform.id,8,true,n+1});}
    // Eight authorities is the table maximum: retain three collision targets
    // and five full path authorities; all eight named paths remain bound.
    bad.actionTargets.resize(8);for(unsigned n=0;n<16;++n)bad.rules.push_back({n+1,30,10,DetTriggerPhase2D::Enter,{20,DetActionKind2D::CollisionDisable},false,n,n});
    for(unsigned n=1;n<16;++n)bad.counters.push_back({n+1,0,UINT64_MAX,0});
    bad.objectives.clear();for(unsigned n=0;n<8;++n){DetProgressObjective2D o;o.id=n+1;for(unsigned k=0;k<8;++k)o.conditions.push_back({k+1,k+1});bad.objectives.push_back(o);}
    // Threshold1 on counter1 must remain within its maximum2.
    bad.objectives[0].conditions[0].atLeast=1;
    for(auto& o:bad.objectives)o.conditions[0].atLeast=1;
    bad.objectiveActions.clear();for(unsigned n=0;n<8;++n)bad.objectiveActions.push_back({n+1,n+1,{20,DetActionKind2D::CollisionDisable},false});
    for(unsigned n=0;bad.actors.size()<64;++n)bad.actors.push_back(actor(1000+n,DetBodyMode2D::Static,{D::fromUInt(1000+4*n),{}},{q(1),q(1)},4,8));
    refused(bad);require(error.find("canonical content limit")!=std::string::npos,"legal individual counts still obey whole semantic8KiB budget");
    // Additional application descriptors are admitted by the real destination
    // Session, not predicted from Level's own fresh scratch namespace count.
    DeterministicSession full(detLevelSessionConfig(l));unsigned admitted=0;
    const bool configured=configureDetLevel2D(full,l,error,{},[&](DeterministicSession& destination){
        for(unsigned n=0;n<65;++n){const DetTypedStateSchema schema{500+n,1,{{1,std::uint32_t{}}}};
            if(!destination.registerTypedSchema(schema))return false;++admitted;}return true;});
    require(!configured && admitted>0 && admitted<65 && !full.sealed(),"actual Session capacity refusal leaves partial destination for discard");
    ++cases;std::cout<<"PASS complete author references/order/namespaces and populated finite composite preflight\n";
}
void closureChecks(const fs::path& dir){const auto l=scene();const auto text=encodeDetLevel2D(l);DetLevel2D decoded;std::string error;
    require(decodeDetLevel2D(text,decoded,error),error);const auto progress=detLevelProgressConfig2D(decoded);const auto flow=detLevelProgressFlowConfig2D(decoded);
    DeterministicSession baseline(detLevelSessionConfig(decoded)),other(detLevelSessionConfig(l));configure(baseline,decoded);configure(other,l,true);
    std::vector<DetSessionCheckpoint> cps{checkpoint(baseline)};std::vector<Bytes> states{bytes(baseline)};std::vector<DetConfirmedEvent> events;
    for(unsigned t=0;t<8;++t){advance(baseline,frame(t));advance(other,frame(t,true));require(bytes(baseline)==bytes(other),"complete opposite registration/input order state");++fullComparisons;
        const auto cp=checkpoint(baseline);cps.push_back(cp);states.push_back(encodeDetCheckpoint(cp));
        require(detProgressCounter2D(cp,progress,1)==(t==0?0u:t<3?1u:2u),"real Enter delivered next tick: hand counter0/1/2");
        require(detCollectibleState2D(cp,progress,1).claimed==(t>=1) && detCollectibleState2D(cp,progress,2).claimed==(t>=3),"gate1 unlocks second actual Enter");
        require(mask(cp,l,30)==(t<1?1u:0u) && mask(cp,l,40)==(t<3?1u:0u) && mask(cp,l,20)==(t<3?1u:0u),"two atomic collection masks and rising objective action");
        const auto r=detProgressFlowObjective2D(cp,flow,1);require(r.attemptCount==(t<3?0u:1u) && r.onceConsumed==(t>=3),"once only on actual rising action attempt");
        if(t>=3)require(r.lastTick==3 && r.lastResult==DetActionCode2D::Applied,"hand objective action tick3");
        for(const auto& event:cp.pendingEvents)events.push_back({1,t,event});}
    require(baseline.restore(cps[2]),"restore after first collection before gate2");for(unsigned t=2;t<8;++t){advance(baseline,frame(t));require(bytes(baseline)==states[t+1],"Flow edge/claim restore continuation");++fullComparisons;}
    DeterministicSession corrected(detLevelSessionConfig(decoded));configure(corrected,decoded);DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};
    DeterministicRollback rollback(corrected,rc);for(unsigned t=0;t<8;++t){const auto i=frame(t);
        if(t==4){require(rollback.advance(),rollback.error());require(rollback.submit(1,i),rollback.error());}
        else require(rollback.submit(1,i) && rollback.advance(),rollback.error());
        require(bytes(corrected)==states[t+1],"real late Hold correction full state");++fullComparisons;}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events
        && rollback.takeConfirmedEvents().empty(),"actual corrected effects confirmed exactly once");
    DeterministicSession recording(detLevelSessionConfig(decoded));configure(recording,decoded);DetReplayWriter writer;
    require(writer.begin(recording,(dir/"progress.rpl").string(),2),writer.error());for(unsigned t=0;t<8;++t){require(writer.advance(frame(t)),writer.error());
        require(bytes(recording)==states[t+1],"actual recording full state");++fullComparisons;}require(writer.finish(),writer.error());
    DeterministicSession playback(detLevelSessionConfig(decoded));configure(playback,decoded);DetReplayReader reader;const bool opened=reader.open(writer.path());require(opened,reader.error());
    require(reader.restoreInitial(playback),reader.error());while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"actual replay full state");++fullComparisons;}
    require(reader.seek(playback,2) && bytes(playback)==states[2],"actual seek retains pending second collection");++fullComparisons;
    write(dir/"final.state",states.back());++cases;std::cout<<"PASS8ticks actual gated collection/objective action and full state order/restore/late rollback/effects/rpl/seek\n";
}
void pureCounterChecks(){auto l=scene();l.actionTargets.clear();l.claims.clear();l.flowBindings.clear();l.objectiveActions.clear();l.counters={{1,0,UINT64_MAX,UINT64_MAX-1}};
    l.objectives={{1,{{1,UINT64_MAX}}}};const auto progress=detLevelProgressConfig2D(l);require(progress.actions.targets.empty(),"pure counters resolve matching zero-target Actions");
    DeterministicSession s(detLevelSessionConfig(l));std::string error;require(configureDetLevel2D(s,l,error),error);
    require(s.registerSystem(6,0,[progress](DetTickContext& ctx){const auto p=executeDetProgress2D(ctx,progress,{DetProgressOperation2D::Add,1,1});
        require(p.code==DetProgressCode2D::Applied,"UInt64 final increment");const auto denied=executeDetAction2D(ctx,progress.actions,{20,DetActionKind2D::CollisionDisable});
        require(denied.code==DetActionCode2D::UnknownTarget,"zero-target Actions grants no unknown authority");return true;}),s.error());require(s.seal(),s.error());
    const auto before=checkpoint(s);advance(s,{0,1,{}});const auto after=bytes(s);
    require(detProgressCounter2D(checkpoint(s),progress,1)==UINT64_MAX && detProgressObjective2D(checkpoint(s),progress,1)
        && !checkpoint(s).globals.contains(112),"exact UInt64 boundary and no implicit Flow");
    require(s.restore(before),"pure counter restore");advance(s,{0,1,{}});require(bytes(s)==after,"zero-target Progress restore complete state");++fullComparisons;
    const auto text=encodeDetLevel2D(l);DetLevel2D decoded;require(decodeDetLevel2D(text,decoded,error)
        && decoded.counters[0].maximum==UINT64_MAX && decoded.counters[0].initial==UINT64_MAX-1,"UInt64 text never uses binary64");
    ++cases;std::cout<<"PASS pure counter zero-target Actions, UInt64 boundary, ordinary UnknownTarget and complete restore\n";
}
void denialChecks(){auto l=scene();l.actors[1].position.x=D::fromInt(2);l.actors[1].body.mask=0;l.objectiveActions[0].action.kind=DetActionKind2D::CollisionEnable;
    const auto flow=detLevelProgressFlowConfig2D(l);DeterministicSession s(detLevelSessionConfig(l));configure(s,l);
    for(unsigned t=0;t<8;++t)advance(s,frame(t));const auto cp=checkpoint(s);const auto r=detProgressFlowObjective2D(cp,flow,1);
    require(r.onceConsumed && r.attemptCount==1 && r.lastTick==3 && r.lastResult==DetActionCode2D::Occupied
        && mask(cp,l,20)==0 && detProgressCounter2D(cp,flow.progress,1)==2,"Occupied keeps body safe, consumes once, no retry while true");
    ++cases;std::cout<<"PASS authored objective normal Occupied denial without pending retry or partial enable\n";
}
void identityChecks(){const auto l=scene();auto recipe=detLevelSessionConfig(l);recipe.contentHash=0;DeterministicSession a(recipe);configure(a,l);const auto cp=checkpoint(a);
    for(unsigned n=0;n<3;++n){auto changed=l;if(n==0)changed.objectives[0].conditions[0].atLeast=0;else if(n==1)changed.claims[0].amount=2;
        else changed.objectiveActions[0].action.kind=DetActionKind2D::CollisionEnable;
        DeterministicSession b(recipe);configure(b,changed);const auto before=bytes(b);
        require(a.manifest()!=b.manifest() && !b.restore(cp) && !b.faulted() && bytes(b)==before,"full authored threshold/amount/action identity rejects equal-hash foreign CP");++fullComparisons;}
    auto corrupt=cp;corrupt.globals.at(32).at(1)^=1;const auto before=bytes(a);require(!a.restore(corrupt) && bytes(a)==before,"immutable version3 content corruption atomic");
    unsigned hooks=0;DeterministicSession hook(detLevelSessionConfig(l));std::string error;
    require(configureDetLevel2D(hook,l,error,{},[&](DeterministicSession& destination){++hooks;
        require(!destination.registerTypedSchema(detProgressStateSchema2D(detLevelProgressConfig2D(l))),"hook sees Progress already installed");
        return destination.registerTypedSchema({128,1,{{1,std::uint32_t{42}}}}) && destination.registerGlobalState(128);}),error);
    require(hooks==1 && hook.seal() && checkpoint(hook).actors.at(10).blocks.contains(128),"destination-only hook precedes actor copies after Flow");
    ++cases;std::cout<<"PASS complete version3 immutable identities and unchanged actor-free destination hook\n";
}
DetLevel2D oldFormat2(){DetLevel2D l;l.formatVersion=2;l.applicationVersion=36;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;c.advanced=true;
    c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    auto platform=actor(40,DetBodyMode2D::MovingObstacle,{D::fromInt(10),{}},{q(1),q(1)},4,8);platform.path=1;
    l.actors={hero,actor(20,DetBodyMode2D::Static,{D::fromInt(4),{}},{q(2),q(2)},2,1),actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true),platform};
    l.paths={{1,DetPathPlayback2D::PingPong,{{{},4,0},{{D::fromInt(1),{}},4,0}}}};l.actionTargets={{20,1,true,0},{40,8,true,1}};
    l.rules={{1,30,10,DetTriggerPhase2D::Enter,{20,DetActionKind2D::CollisionDisable},true,0,2},
        {2,30,10,DetTriggerPhase2D::Exit,{40,DetActionKind2D::PathPause},false,4,0}};return l;}
void legacyChecks(const fs::path& file){DetLevel2D l;std::string error;require(loadDetLevel2D(file,l,error),error);
    require(l.formatVersion==1 && detLevelSessionConfig(l).contentHash==UINT64_C(1216874329789544449),"recorded format1 original semantic hash");
    const auto v2=oldFormat2();require(detLevelSessionConfig(v2).contentHash==UINT64_C(8404432461347634428),"recorded Stage36 format2 complete semantic hash");
    for(const auto& old:{l,v2}){const auto text=encodeDetLevel2D(old);require(text.find("\nprogress")==std::string::npos && text.find("\nflow")==std::string::npos,"old text gains no third-format fields");
        DeterministicSession s(detLevelSessionConfig(old));require(configureDetLevel2D(s,old,error) && s.seal(),error);require(!checkpoint(s).globals.contains(96) && !checkpoint(s).globals.contains(112),"old profiles install no progress/flow");}
    ++cases;std::cout<<"PASS independently pinned old format1/2 semantic hashes and no implicit new authority\n";
}
}
int main(int argc,char** argv){try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string a=argv[n];if((a=="--output" || a=="--legacy") && n+1<argc){
    if(a=="--output")parent=argv[++n];else legacy=argv[++n];}else throw std::invalid_argument("usage: --output existingParent --legacy format1TerrainFile");}
    require(!parent.empty() && !legacy.empty(),"--output and --legacy required");const auto dir=fresh(parent);
    legacyChecks(legacy);parserChecks(dir);semanticChecks();closureChecks(dir);pureCounterChecks();denialChecks();identityChecks();
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)+",\"parserNegatives\":"+std::to_string(parserNegatives)
        +",\"semanticNegatives\":"+std::to_string(semanticNegatives)+",\"completeCheckpointComparisons\":"+std::to_string(fullComparisons)
        +",\"ticks\":8,\"contentHash\":\""+std::to_string(detLevelSessionConfig(scene()).contentHash)+"\"}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL levelprogress3: "<<e.what()<<'\n';return 1;}}
