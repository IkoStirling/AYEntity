// design reference: AYEntity/design.md Stage50 explicit moving-trigger content.
// Hand expectations cover selected geometry, HP, clocks and controls.
// Whole-checkpoint comparisons establish execution-path equality, not all-byte golden/authentication.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
unsigned cases=0,rejects=0,comparisons=0,handRows=0;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
DetSessionCheckpoint cp(DeterministicSession& session){auto value=session.checkpoint();require(bool(value),"checkpoint: "+session.error());return *value;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
void advance(DeterministicSession& s,const DetTickInput& i){const bool ok=s.advance(i);require(ok,"advance tick "+std::to_string(i.tick)+": "+s.error());}
template<class T>T pathValue(const DetSessionCheckpoint& value,const DetPathMotion2DConfig& cfg,std::uint32_t field){
    return readDetState<T>(detPathMotionStateSchema2D(cfg),value.actors.at(20).blocks.at(cfg.stateSchema),field);}
std::uint32_t mask(const DetSessionCheckpoint& value,const DetCollision2DConfig& cfg){
    return readDetState<std::uint32_t>(detCollisionBodySchema2D(cfg),value.actors.at(20).blocks.at(cfg.bodySchema),DetBodyMask);}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);require(bool(input),"open canonical input");
    std::string result((std::istreambuf_iterator<char>(input)),{});require(result.size()<=kDetLevel2DMaxTextBytes,"bounded canonical input");return result;}
void write(const fs::path& file,const std::string& text){std::ofstream output(file,std::ios::binary);output<<text;output.close();require(bool(output),"fresh output write");}
void write(const fs::path& file,const Bytes& data){std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    output.close();require(bool(output),"fresh binary write");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"existing output parent required");const auto now=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto p=parent/("level-moving-trigger7-"+std::to_string(now)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}throw std::runtime_error("fresh output directory unavailable");}
std::string replaced(std::string text,const std::string& before,const std::string& after){const auto at=text.find(before);require(at!=std::string::npos,"canonical fixture token");text.replace(at,before.size(),after);return text;}
DetLevelActor2D actor(SimEntityId id,DetBodyMode2D mode,V center,V half,bool trigger=false){DetLevelActor2D a;a.id=id;a.position=center;
    a.body={mode,half,{},{},trigger?2u:1u,trigger?1u:2u,trigger};a.character=mode==DetBodyMode2D::Kinematic;return a;}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=7;l.applicationVersion=50;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.advanced=true;c.maxHorizontalSpeed=D::fromInt(4);
    c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    DetLevelPath2D path;path.id=1;path.playback=DetPathPlayback2D::Once;path.movingTrigger=true;
    path.waypoints={{{},8,0},{{D::fromInt(4),{}},8,0}};l.paths={path};
    l.actors={actor(10,DetBodyMode2D::Kinematic,{},{q(1),q(1)}),
        actor(20,DetBodyMode2D::MovingObstacle,{D::fromInt(-2),{}},{q(2),q(2)},true),
        actor(30,DetBodyMode2D::Static,{},{q(1),q(1)})};l.actors[1].path=1;
    // Hero/wall share layer1 mask2, so they do not collide with each other.
    // Trigger20 reciprocally visits both: real trigger crossings never act as solids.
    l.actionTargets={{20,1,true,1}};l.counters={{1,0,1,1}};l.objectives={{1,{{1,1}}}};
    l.objectiveActions={{1,1,{20,DetActionKind2D::CollisionEnable},true}};
    l.vitalityActors={{10,201,10,10,0,0,false}};
    l.vitalityRegions={{1,20,10,DetVitalityKind2D::Hurt,2,2}};l.periodicVitalityRegions=true;return l;}
std::optional<DetActionKind2D> actionAt(unsigned t){switch(t){
    case 1:return DetActionKind2D::PathPause;case 2:return DetActionKind2D::PathResume;
    case 3:return DetActionKind2D::CollisionDisable;case 5:return DetActionKind2D::CollisionEnable;
    case 6:return DetActionKind2D::PathPause;case 7:return DetActionKind2D::PathDisable;
    case 8:return DetActionKind2D::PathEnable;case 9:return DetActionKind2D::PathResume;default:return {};}}
DetTickInput frame(unsigned t,bool reverse=false){DetTickInput value{t,1,{}};if(const auto action=actionAt(t))
    value.commands.push_back({1,0,1,{static_cast<std::uint8_t>(*action)}});value.commands.push_back({1,1,99,{50}});
    if(reverse)std::reverse(value.commands.begin(),value.commands.end());return value;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& l){return s.registerSystem(4,l.character.collision.priority,
    [actions=detLevelActionsConfig2D(l)](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){
        if(cmd.source!=1)return false;if(cmd.type==1 && cmd.payload.size()==1){
            if(cmd.payload[0]<1 || cmd.payload[0]>7)return false;
            const auto result=executeDetAction2D(ctx,actions,{20,static_cast<DetActionKind2D>(cmd.payload[0])});
            if(result.code!=DetActionCode2D::Applied && result.code!=DetActionCode2D::Unchanged)return false;
        }else if(cmd.type!=99 || cmd.payload!=Bytes{50})return false;}return true;});}
void configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;
    if(!reverse)require(inputSystem(s,l),"input registration: "+s.error());const bool setup=configureDetLevel2D(s,l,error);
    require(setup,"complete level pipeline: "+error);if(reverse)require(inputSystem(s,l),"reverse input registration: "+s.error());
    const bool sealed=s.seal();require(sealed,"seal: "+s.error());}
// Exact completed Stage49 source copy; identity read from its actual compressed
// Manifest event (FNV checked), not computed by a second Level implementation.
// Source .dlt SHA f96d921b1db3dc3ea45ca069c793666fd5e35f4726b780bd85a9c759c6b69adf.
void legacy(const fs::path& file){const auto text=read(file);DetLevel2D l;std::string error;const bool parsed=decodeDetLevel2D(text,l,error);require(parsed,error);
    require(l.formatVersion==6 && detLevelSessionConfig(l).contentHash==UINT64_C(15889101324626947613)
        && encodeDetLevel2D(l)==text,"completed old6 canonical source/identity unchanged");
    const auto vitality=detLevelVitalityConfig2D(l);require(detVitalityStateSchemas2D(vitality).front().version==3,"old6 Vitality3 remains3");
    for(const auto& path:l.paths)require(!path.movingTrigger,"old6 role defaults false");
    DeterministicSession s(detLevelSessionConfig(l));const bool setup=configureDetLevel2D(s,l,error);require(setup,error);require(s.seal(),s.error());++cases;}
void authoring(const fs::path& dir){const auto l=scene();const auto text=encodeDetLevel2D(l);DetLevel2D decoded;std::string error;
    const bool parsed=decodeDetLevel2D(text,decoded,error);require(parsed,error);require(encodeDetLevel2D(decoded)==text
        && text.find("path 1 0 1 2\n")!=std::string::npos,"canonical explicit Level7 role token");
    const auto cfg=detLevelPathConfig2D(decoded,1);const auto actions=detLevelActionsConfig2D(decoded);
    const auto progress=detLevelProgressConfig2D(decoded);const auto flow=detLevelProgressFlowConfig2D(decoded);const auto vitality=detLevelVitalityConfig2D(decoded);
    require(cfg.movingTrigger && actions.targets.front().path->movingTrigger
        && detPathMotionStateSchema2D(cfg).version==2 && detPathMotionPolicySchema2D(cfg).version==2
        && detActionsStateSchema2D(actions).version==3 && detProgressStateSchema2D(progress).version==1
        && detProgressFlowObjectiveSchema2D(flow).version==1 && detVitalityStateSchemas2D(vitality).front().version==3,
        "real Path2/Actions3 plus unchanged Progress1/Flow1 and periodic Vitality3 selectors");
    const auto refuse=[&](const std::string& bad){const bool accepted=decodeDetLevel2D(bad,decoded,error);
        require(!accepted && !error.empty() && encodeDetLevel2D(decoded)==text,"atomic parser refusal");++rejects;};
    refuse(replaced(text,"path 1 0 1 2","path 1 0 2 2"));refuse(replaced(text,"path 1 0 1 2","path 1 0 2"));
    refuse(replaced(text,"AYDLEVEL 7","AYDLEVEL 8"));refuse(text+"unknown");
    const auto invalid=[&](const DetLevel2D& bad){require(!validateDetLevel2D(bad,error) && !error.empty(),"semantic role refusal");++rejects;};
    auto bad=l;bad.formatVersion=6;invalid(bad);bad=l;bad.paths[0].movingTrigger=false;invalid(bad);
    bad=l;bad.actors[1].body.trigger=false;invalid(bad);bad=l;bad.actors[1].body.mode=DetBodyMode2D::Static;invalid(bad);
    bad=l;bad.actors[1].body.oneWay=true;invalid(bad);bad=l;bad.actors[1].body.shape=DetCollisionShape2D::SlopeRisesRight;invalid(bad);
    const auto cooked=encodeDetLevelCooked2D(l);DetLevel2D cookedLevel;const bool loaded=decodeDetLevelCooked2D(cooked,cookedLevel,error);
    require(loaded && encodeDetLevel2D(cookedLevel)==text && detLevelPathConfig2D(cookedLevel,1).movingTrigger,"cooked1 envelope preserves selected Level7 role");
    // Path role is full Level/Path identity even without any action grant.
    auto unauthorised=l;unauthorised.actionTargets.clear();unauthorised.counters.clear();unauthorised.objectives.clear();unauthorised.objectiveActions.clear();
    require(validateDetLevel2D(unauthorised,error),error);const auto noActions=detLevelActionsConfig2D(unauthorised);
    require(noActions.targets.empty() && detActionsStateSchema2D(noActions).version==1
        && detLevelPathConfig2D(unauthorised,1).movingTrigger,"ungranted Path2 does not invent Actions3 authority");
    require(detLevelSessionConfig(unauthorised).contentHash!=detLevelSessionConfig(l).contentHash,"complete authored authority affects content identity");
    write(dir/"moving-trigger7.dlt",text);++cases;}
void hand(const DetSessionCheckpoint& value,const DetLevel2D& level,const DetPathMotion2DConfig& path,const DetVitality2DConfig& vitality,unsigned t){
    constexpr int xquarters[12]={-6,-6,-4,-2,0,2,2,2,2,4,6,8};const auto& hero=value.actors.at(10).pose;const auto& hazard=value.actors.at(20).pose;
    require(hazard.position[0]==q(xquarters[t]).bits() && hazard.position[1]==D{}.bits()
        && hero.position[0]==D{}.bits() && hero.position[1]==D{}.bits(),"hand dyadic hazard progression/stationary hero");
    const auto hp=t<6?10u:t<8?8u:6u;const auto attempts=t<6?0u:t<8?1u:2u;const auto region=detVitalityRegionState2D(value,vitality,1);
    require(detVitalityState2D(value,vitality,10).hp==hp && region.attemptCount==attempts
        && region.episodeCount==(t<6?0u:1u) && region.active==(t>=6 && t<10),"real moving-trigger Enter/Stay/Exit HP and episode clocks");
    if(t>=6 && t<10)require(region.lastAttemptTick==(t<8?6u:8u) && region.nextEligible==(t<8?8u:10u),"hand processing6/8 only");
    if(t>=10)require(!region.nextEligible,"Exit clears deadline without retry");
    require(mask(value,level.character.collision)==((t==3 || t==4)?0u:1u),"mask toggles only authored reciprocal participation");
    require(pathValue<bool>(value,path,DetPathPaused)==(t==1 || (t>=6 && t<=8)),"pause remains independent of enable");
    require(pathValue<bool>(value,path,DetPathEnabled)==(t!=7),"disable preserved cursor; enable preserved pause");
    require(pathValue<bool>(value,path,DetPathFinished)==(t==11) && !pathValue<bool>(value,path,DetPathBlocked),"trigger never blocked by ordinary wall");
    const auto flow=detProgressFlowObjective2D(value,detLevelProgressFlowConfig2D(level),1);
    require(flow.previousSatisfied && flow.onceConsumed && flow.attemptCount==1 && flow.lastTick==0
        && flow.lastResult==DetActionCode2D::Unchanged,"initial objective executes one genuine fixed action without retry");++handRows;}
void equalCheckpoint(const DetSessionCheckpoint& actual,const DetSessionCheckpoint& expected,const std::string& phase){
    if(const auto diff=firstDetDifference(expected,actual))throw std::runtime_error(phase+" tick "+std::to_string(actual.nextTick)
        +" entity "+std::to_string(diff->entity)+" component "+std::to_string(diff->component)+" field "+std::to_string(diff->field));++comparisons;}
void closure(const fs::path& dir){const auto l=scene();const auto p=detLevelPathConfig2D(l,1);const auto v=detLevelVitalityConfig2D(l);
    DeterministicSession baseline(detLevelSessionConfig(l)),reverse(detLevelSessionConfig(l));configure(baseline,l);configure(reverse,l,true);
    std::vector<DetSessionCheckpoint> rows{cp(baseline)};std::vector<DetConfirmedEvent> events;std::string trace="0 "+std::to_string(detCheckpointHash(rows.front()))+"\n";
    for(unsigned t=0;t<12;++t){advance(baseline,frame(t));advance(reverse,frame(t,true));const auto row=cp(baseline);equalCheckpoint(cp(reverse),row,"reverse callback/input registration");
        hand(row,l,p,v,t);rows.push_back(row);trace+=std::to_string(t+1)+" "+std::to_string(detCheckpointHash(row))+"\n";
        for(const auto& event:row.pendingEvents)events.push_back({1,t,event});}
    require(!events.empty(),"fixture must emit actual reciprocal trigger effects");const bool restored=baseline.restore(rows[6]);require(restored,baseline.error());
    for(unsigned t=6;t<12;++t){advance(baseline,frame(t));equalCheckpoint(cp(baseline),rows[t+1],"restore pending Enter and interval authority");}
    const auto before=bytes(baseline);auto broken=rows[6];const auto policy=detPathMotionPolicySchema2D(p);
    writeDetState(policy,broken.globals.at(p.policySchema),12,false);const bool bad=baseline.restore(broken);
    require(!bad && bytes(baseline)==before,"role policy restore refusal leaves complete live state intact");++rejects;
    auto foreign=l;foreign.paths[0].waypoints.back().offset.x=D::fromInt(5);auto config=detLevelSessionConfig(l);config.contentHash=0;
    DeterministicSession local(config),other(config);configure(local,l);configure(other,foreign);const auto otherBefore=bytes(other);
    require(local.manifest()!=other.manifest() && !other.restore(cp(local)) && bytes(other)==otherBefore,"full moving path literal identity despite matching contentHash");++rejects;
    DeterministicSession corrected(detLevelSessionConfig(l));configure(corrected,l);DetRollbackConfig rc;rc.maxPredictionTicks=4;
    rc.prediction={{1,DetPredictionMode::Omit},{99,DetPredictionMode::Omit}};DeterministicRollback rollback(corrected,rc);
    for(unsigned t=0;t<12;++t){const auto input=frame(t);if(t==5){const bool guessed=rollback.advance();require(guessed,rollback.error());
            const bool submitted=rollback.submit(1,input);require(submitted,rollback.error());}
        else{const bool submitted=rollback.submit(1,input);require(submitted,rollback.error());const bool advanced=rollback.advance();require(advanced,rollback.error());}
        equalCheckpoint(cp(corrected),rows[t+1],"real late enable recreates geometric events and HP");}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events
        && rollback.takeConfirmedEvents().empty(),"one-shot action Omit correction and actual effects exactly once");
    DeterministicSession recording(detLevelSessionConfig(l));configure(recording,l);DetReplayWriter writer;
    const bool began=writer.begin(recording,(dir/"moving-trigger.rpl").string(),2);require(began,writer.error());
    for(unsigned t=0;t<12;++t){const bool advanced=writer.advance(frame(t));require(advanced,writer.error());equalCheckpoint(cp(recording),rows[t+1],"actual rpl record");}
    const bool finished=writer.finish();require(finished,writer.error());const std::string actualFile=writer.path();
    DeterministicSession playback(detLevelSessionConfig(l));configure(playback,l);DetReplayReader reader;const bool opened=reader.open(actualFile);require(opened,reader.error());
    const bool initial=reader.restoreInitial(playback);require(initial,reader.error());equalCheckpoint(cp(playback),rows[0],"actual initial playback");
    while(!reader.atEnd()){const bool advanced=reader.advance(playback);require(advanced,reader.error());equalCheckpoint(cp(playback),rows.at(static_cast<std::size_t>(playback.nextTick())),"actual rpl playback");}
    for(unsigned tick:{6u,12u}){const bool sought=reader.seek(playback,tick);require(sought,reader.error());equalCheckpoint(cp(playback),rows[tick],"real seek");}
    write(dir/"tick-hashes.txt",trace);write(dir/"initial.state",encodeDetCheckpoint(rows[0]));write(dir/"final.state",encodeDetCheckpoint(rows.back()));
    Bytes inputs;for(unsigned t=0;t<12;++t){const auto data=encodeDetInput(frame(t));const auto size=static_cast<std::uint32_t>(data.size());
        for(unsigned byte=0;byte<4;++byte)inputs.push_back(static_cast<std::uint8_t>(size>>(8*byte)));inputs.insert(inputs.end(),data.begin(),data.end());}
    write(dir/"canonical-inputs.bin",inputs);++cases;}
} // namespace
int main(int argc,char** argv){fs::path dir;try{fs::path parent,old;for(int n=1;n<argc;++n){const std::string arg=argv[n];
        if((arg=="--output" || arg=="--legacy") && n+1<argc){if(arg=="--output")parent=argv[++n];else old=argv[++n];}
        else throw std::invalid_argument("--output existingParent --legacy completedStage49Format6File");}
    require(!parent.empty() && !old.empty(),"actual old6 canonical input and output parent required");dir=fresh(parent);legacy(old);authoring(dir);closure(dir);
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)+",\"rejects\":"+std::to_string(rejects)
        +",\"ticks\":12,\"handRows\":"+std::to_string(handRows)+",\"completeCheckpointComparisons\":"+std::to_string(comparisons)
        +",\"legacy6ContentHash\":\"15889101324626947613\",\"oracleScope\":\"selected hand path/mask/HP clocks; full CP execution equality\"}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;
}catch(const std::exception& error){std::cerr<<"FAIL levelmovingtrigger7: "<<error.what()<<'\n';if(!dir.empty())try{write(dir/"failure.txt",error.what());}catch(...){}return 1;}}
