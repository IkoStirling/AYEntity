// design reference: AYEntity/design.md Stage36 complete AYDLEVEL2 content.
// Small dyadic motion/action timing oracles; complete CP comparisons below are
// execution/restore/rollback/replay equivalence, not independent all-byte goldens.
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
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
bool same(V a,V b){return a.x.bits()==b.x.bits() && a.y.bits()==b.y.bits();}
DetLevelActor2D actor(SimEntityId id,DetBodyMode2D mode,V position,V half,std::uint32_t layer,std::uint32_t mask,bool trigger=false){
    DetLevelActor2D a;a.id=id;a.position=position;a.body={mode,half,{},{},layer,mask,trigger};return a;}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=2;l.applicationVersion=36;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.collision.maxBodies=32;c.collision.maxTriggerPairs=8;
    c.advanced=true;c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);
    c.gravity={};c.jumpSpeed={};
    auto hero=actor(10,DetBodyMode2D::Kinematic,{D::fromInt(-1),{}},{q(1),q(1)},1,2);hero.character=true;
    auto door=actor(20,DetBodyMode2D::Static,{D::fromInt(4),{}},{q(2),q(2)},2,1);
    auto trigger=actor(30,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1,true);
    auto platform=actor(40,DetBodyMode2D::MovingObstacle,{D::fromInt(10),{}},{q(1),q(1)},4,8);platform.path=1;
    l.actors={hero,door,trigger,platform};l.paths={{1,DetPathPlayback2D::PingPong,{{{},4,0},{{D::fromInt(1),{}},4,0}}}};
    l.actionTargets={{20,1,true,0},{40,8,true,1}};
    l.rules={{1,30,10,DetTriggerPhase2D::Enter,{20,DetActionKind2D::CollisionDisable},true,0,2},
        {2,30,10,DetTriggerPhase2D::Exit,{40,DetActionKind2D::PathPause},false,4,0}};return l;}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
V position(const DetSessionCheckpoint& cp,SimEntityId id){const auto& p=cp.actors.at(id).pose.position;return {D::fromBits(p[0]),D::fromBits(p[1])};}
template<class T>T body(const DetSessionCheckpoint& cp,const DetLevel2D& l,SimEntityId id,std::uint32_t field){return readDetState<T>(
    detCollisionBodySchema2D(l.character.collision),cp.actors.at(id).blocks.at(l.character.collision.bodySchema),field);}
template<class T>T result(const DetSessionCheckpoint& cp,const DetActions2DConfig& c,std::uint32_t field){return readDetState<T>(
    detActionsStateSchema2D(c),cp.globals.at(c.stateSchema),field);}
DetTickInput frame(std::uint64_t tick,bool reverse=false){const int speed=tick%4<2?4:-4;
    DetTickInput i{tick,1,{{1,0,1,{static_cast<std::uint8_t>(speed)}}}};
    if(tick==6)i.commands.push_back({1,1,2,{40,static_cast<std::uint8_t>(DetActionKind2D::PathResume)}});
    if(reverse)std::reverse(i.commands.begin(),i.commands.end());return i;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& l){const auto actions=detLevelActionsConfig2D(l);
    return s.registerSystem(10,l.character.collision.priority,[c=l.character,actions](DetTickContext& ctx){
        for(const auto& command:ctx.input().commands){if(command.source!=1)return false;
            if(command.type==1 && command.sequence==0 && command.payload.size()==1){const int speed=command.payload[0]<128?command.payload[0]:int(command.payload[0])-256;
                detCharacterInput2D(ctx,10,c,{D::fromInt(speed),false,false,false});}
            else if(command.type==2 && command.sequence==1 && command.payload.size()==2){const auto r=executeDetAction2D(ctx,actions,
                {command.payload[0],static_cast<DetActionKind2D>(command.payload[1])});require(r.code==DetActionCode2D::Applied,"hand direct resume result");}
            else return false;}return true;});}
bool configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;
    if(!reverse && !inputSystem(s,l))return false;
    if(!configureDetLevel2D(s,l,error))throw std::runtime_error(error);
    if(reverse && !inputSystem(s,l))return false;return s.seal();}
void advance(DeterministicSession& s,const DetTickInput& i){
    // Read diagnostics after the operation; function argument evaluation order
    // otherwise permits copying the previous empty error before advance fails.
    if(!s.advance(i))throw std::runtime_error("Session advance tick "+std::to_string(i.tick)+": "+s.error());
}
void write(const fs::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;f.close();require(bool(f),"write text evidence");}
void write(const fs::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));f.close();require(bool(f),"write byte evidence");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires existing parent");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();for(unsigned n=0;n<1024;++n){const auto p=parent/("level-interaction-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}
    throw std::runtime_error("fresh evidence directory unavailable");}
std::string replace(std::string text,const std::string& from,const std::string& to){const auto at=text.find(from);require(at!=std::string::npos,"negative fixture token found: "+from);text.replace(at,from.size(),to);return text;}
void parserChecks(const fs::path& dir){const auto original=scene();const auto text=encodeDetLevel2D(original);DetLevel2D loaded;std::string error;
    require(decodeDetLevel2D(text,loaded,error),error);require(encodeDetLevel2D(loaded)==text,"format2 complete content roundtrip");
    const auto hash=detLevelSessionConfig(loaded).contentHash;
    require(decodeDetLevel2D("\r\n\t"+text+" \n",loaded,error) && detLevelSessionConfig(loaded).contentHash==hash,"whitespace is not rule/target authority");
    write(dir/"content.dlt",text);require(loadDetLevel2D(dir/"content.dlt",loaded,error),error);
    const auto before=encodeDetLevel2D(loaded);const auto refused=[&](const std::string& bad,bool semantic){
        require(!decodeDetLevel2D(bad,loaded,error) && encodeDetLevel2D(loaded)==before,"failed decode leaves complete prior content unchanged");
        require(error.find(semantic?"semantic preflight":"line ")!=std::string::npos,"syntax versus semantic diagnostic scope");++parserNegatives;};
    refused(replace(text,"AYDLEVEL 2","AYDLEVEL 3"),false);
    refused(replace(text,"actions 64 2","actions 64 9"),false);
    refused(replace(text,"rules 2","rules 17"),false);
    refused(replace(text,"target 20 1 1 0","target 20 1 2 0"),false);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 10 1 20 2 2 0 2"),false);
    refused(text+"unknown",false);refused(std::string("\xef\xbb\xbf")+text,false);
    refused(replace(text,"target 20 1 1 0","target 21 1 1 0"),true);
    refused(replace(text,"target 40 8 1 1","target 20 8 1 1"),true);
    refused(replace(text,"target 20 1 1 0","target 20 0 1 0"),true);
    refused(replace(text,"target 40 8 1 1","target 40 8 1 99"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 10 2 20 2 1 0 2"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 10 1 20 8 1 0 2"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 10 1 99 2 1 0 2"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 31 10 1 20 2 1 0 2"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 20 1 20 2 1 0 2"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 10 1 20 2 1 65536 2"),true);
    refused(replace(text,"rule 1 30 10 1 20 2 1 0 2","rule 1 30 10 1 20 2 1 0 65536"),true);
    auto noRules=original;noRules.rules.clear();require(validateDetLevel2D(noRules,error),error);
    DeterministicSession onlyActions(detLevelSessionConfig(noRules));require(configure(onlyActions,noRules),onlyActions.error());
    require(!checkpoint(onlyActions).globals.contains(80) && checkpoint(onlyActions).globals.contains(64),"Actions-only resource installs no Interaction state");
    ++cases;std::cout<<"PASS complete format2 roundtrip, closed references/types/timers and atomic located diagnostics; Actions-only content\n";
}
void semanticChecks(){const auto good=scene();std::string error;
    const auto rejected=[&](const DetLevel2D& bad,const DetLevel2DOptions& options=DetLevel2DOptions{}){
        require(!validateDetLevel2D(bad,error,options) && !error.empty(),"invalid content/options rejected by complete scratch pipeline");++semanticNegatives;};
    auto bad=good;bad.formatVersion=1;rejected(bad);
    bad=good;bad.formatVersion=1;bad.actionTargets.clear();bad.rules.clear();bad.maxActionsPerTick=65;rejected(bad);
    bad=good;bad.actionTargets[0].collisionMask=2;rejected(bad);
    bad=good;bad.actionTargets[0].collisionControl=false;rejected(bad);
    bad=good;bad.rules[0].action.kind=DetActionKind2D::PathPause;rejected(bad);
    bad=good;bad.maxActionsPerTick=1025;rejected(bad);
    auto options=DetLevel2DOptions{};options.actionsStateSchema=good.character.stateSchema;rejected(good,options);
    options={};options.actionsPolicySchemaBase=32;rejected(good,options);
    options={};options.interactionStateSchema=options.actionsStateSchema;rejected(good,options);
    options={};options.interactionSystemId=12;rejected(good,options);
    options={};options.interactionValidatorId=good.character.validatorId;rejected(good,options);
    // Fully populated authored control/rule counts exercise real policy chunks
    // and the actual128-field Interaction schema, not a synthetic byte-only row.
    auto maximum=good;maximum.actors.resize(3);maximum.actionTargets={{20,1,true,0}};
    auto& points=maximum.paths[0].waypoints;points.clear();for(unsigned n=0;n<24;++n)points.push_back({{q(int(n)),{}},4,0});
    for(unsigned n=0;n<7;++n){auto platform=actor(100+n,DetBodyMode2D::MovingObstacle,{D::fromInt(20+int(n)*4),{}},{q(1),q(1)},4,8);platform.path=1;
        maximum.actors.push_back(platform);maximum.actionTargets.push_back({platform.id,8,true,1});}
    maximum.rules.clear();for(unsigned n=0;n<16;++n)maximum.rules.push_back({n+1,30,10,DetTriggerPhase2D::Enter,
        {20,DetActionKind2D::CollisionDisable},false,n,n});
    require(validateDetLevel2D(maximum,error),error);const auto text=encodeDetLevel2D(maximum);DetLevel2D decoded;
    require(decodeDetLevel2D(text,decoded,error) && decoded.actionTargets.size()==8 && decoded.rules.size()==16,"full target/rule counts install and roundtrip");
    require(detActionsPolicySchemas2D(detLevelActionsConfig2D(decoded)).size()>1,"real multi-chunk complete action identity");
    require(detInteractionStateSchema2D(detLevelInteractionConfig2D(decoded)).fields.size()==128,"full bounded rule state is registered");
    ++cases;std::cout<<"PASS unsupported content/namespace preflight and actual8targets/16rules/24points composite closure\n";
}
void closureChecks(const fs::path& dir){const auto authored=scene();DetLevel2D loaded;std::string error;
    require(decodeDetLevel2D(encodeDetLevel2D(authored),loaded,error),error);const auto actions=detLevelActionsConfig2D(loaded);const auto rules=detLevelInteractionConfig2D(loaded);
    DeterministicSession baseline(detLevelSessionConfig(loaded)),ordered(detLevelSessionConfig(authored));
    require(configure(baseline,loaded) && configure(ordered,authored,true),"loaded/authored equivalent recipes");
    std::vector<DetSessionCheckpoint> cps{checkpoint(baseline)};std::vector<Bytes> states{bytes(baseline)};std::vector<DetConfirmedEvent> expectedEvents;
    const int platformQuarter[]{41,42,42,42,42,42,43,44,43,42,42,42};
    for(unsigned t=0;t<12;++t){advance(baseline,frame(t));advance(ordered,frame(t,true));require(bytes(baseline)==bytes(ordered),"loaded/authored opposite registration/input order all CP bytes");++fullComparisons;
        const auto cp=checkpoint(baseline);cps.push_back(cp);states.push_back(encodeDetCheckpoint(cp));
        const int x=t%4==0 || t%4==2?0:(t%4==1?1:-1);
        require(same(position(cp,10),V{D::fromInt(x),{}}) && same(position(cp,40),V{q(platformQuarter[t]),{}}),"hand dyadic character/path motion");
        require(body<std::uint32_t>(cp,loaded,20,DetBodyMask)==(t<3?1u:0u),"delay2 door action executes at processing tick3");
        const auto once=detInteractionState2D(cp,rules,1);if(t==1 || t==2)require(once.pending && once.dueTick==3 && !once.attemptCount,"loaded true pending delay2");
        if(t>=3)require(once.fired && !once.pending && once.scheduleCount==1 && once.attemptCount==1
            && once.lastExecutionTick==3 && once.lastResult==DetActionCode2D::Applied,"loaded once remains consumed after real due execution");
        for(const auto& e:cp.pendingEvents)expectedEvents.push_back({1,t,e});}
    require(result<std::uint64_t>(cps.back(),actions,DetActionsTotalCount)==5
        && detInteractionState2D(cps.back(),rules,2).scheduleCount==3,"hand five attempts, three cooldown4 Exit schedules");
    require(baseline.restore(cps[2]),"pending loaded rule restores");for(unsigned t=2;t<12;++t){advance(baseline,frame(t));require(bytes(baseline)==states[t+1],"pending restore full continuation");++fullComparisons;}
    DeterministicSession corrected(detLevelSessionConfig(loaded));require(configure(corrected,loaded),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};
    DeterministicRollback rollback(corrected,rc);for(unsigned t=0;t<12;++t){const auto input=frame(t);
        if(t==2){require(rollback.advance(),rollback.error());require(rollback.submit(1,input),rollback.error());}
        else require(rollback.submit(1,input) && rollback.advance(),rollback.error());
        require(bytes(corrected)==states[t+1],"real late-input correction includes loaded actions/rules and all CP bytes");++fullComparisons;}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==expectedEvents
        && rollback.takeConfirmedEvents().empty(),"actual rollback and confirmed effects exactly once");
    DeterministicSession recording(detLevelSessionConfig(loaded));require(configure(recording,loaded),recording.error());DetReplayWriter writer;
    const auto path=(dir/"content.rpl").string();require(writer.begin(recording,path,2),writer.error());
    for(unsigned t=0;t<12;++t){require(writer.advance(frame(t)),writer.error());require(bytes(recording)==states[t+1],"actual file record full CP");++fullComparisons;}
    require(writer.finish(),writer.error());DeterministicSession playback(detLevelSessionConfig(loaded));require(configure(playback,loaded),playback.error());DetReplayReader reader;
    // The recorder seals rotation0 (content_000.rpl), not its requested base.
    // Use its returned physical path, as in the existing Level2D closure.
    const bool opened=reader.open(writer.path());require(opened,reader.error());
    const bool restored=reader.restoreInitial(playback);require(restored,reader.error());while(!reader.atEnd()){require(reader.advance(playback),reader.error());
        require(bytes(playback)==states[playback.nextTick()],"actual file replay full CP");++fullComparisons;}
    require(reader.seek(playback,2) && bytes(playback)==states[2],"file seek restores a live loaded pending rule");++fullComparisons;
    write(dir/"final.state",states.back());++cases;
    std::cout<<"PASS12ticks actual loaded once/delay/cooldown/path action, complete state order/restore/late rollback/confirmed drain/rpl/seek\n";
}
void identityChecks(){const auto l=scene();auto recipe=detLevelSessionConfig(l);recipe.contentHash=0;
    DeterministicSession a(recipe);require(configure(a,l),a.error());const auto cp=checkpoint(a);
    for(unsigned n=0;n<3;++n){auto changed=l;if(n==0)changed.rules[0].delayTicks=3;else if(n==1){changed.actionTargets[0].collisionMask=2;changed.actors[1].body.mask=0;}
        else changed.paths[0].waypoints[1].offset.x=D::fromInt(2);
        DeterministicSession b(recipe);require(configure(b,changed),b.error());const auto before=bytes(b);
        require(a.manifest()!=b.manifest() && !b.restore(cp) && !b.faulted() && bytes(b)==before,"changed complete content rejects old CP despite deliberately equal hash");++fullComparisons;}
    auto corrupt=cp;corrupt.globals.at(32).at(1)^=1;const auto before=bytes(a);require(!a.restore(corrupt) && bytes(a)==before,"immutable v2 content corruption rejects atomically");
    auto options=DetLevel2DOptions{};options.actionsStateSchema=100;options.actionsPolicySchemaBase=101;
    options.interactionStateSchema=110;options.interactionPolicySchemaBase=111;
    DeterministicSession remapped(detLevelSessionConfig(l));std::string error;require(configureDetLevel2D(remapped,l,error,options) && remapped.seal(),error);
    require(detLevelActionsConfig2D(l,options).stateSchema==100 && checkpoint(remapped).globals.contains(100)
        && checkpoint(remapped).globals.contains(110) && a.manifest()!=remapped.manifest(),"application registration ranges participate in actual complete manifest");
    ++cases;std::cout<<"PASS complete v2 content identity, equal-hash foreign/corrupt restore and remapped actual registration\n";
}
void registrationHookChecks(){const auto l=scene();std::string error;const auto actions=detLevelActionsConfig2D(l);
    const DetTypedStateSchema application{100,1,{{1,std::uint32_t{17}}}};unsigned calls=0;
    DeterministicSession good(detLevelSessionConfig(l));
    require(configureDetLevel2D(good,l,error,{},[&](DeterministicSession& destination){++calls;
        // Duplicate registration proves Actions is already installed. A new
        // typed schema can succeed only while the destination has no actors.
        require(!destination.registerTypedSchema(detActionsStateSchema2D(actions)),"hook observes already-installed Actions");
        return destination.registerTypedSchema(application) && destination.registerGlobalState(application.id);}),error);
    require(calls==1 && good.seal(),good.error());const auto cp=checkpoint(good);
    require(readDetState<std::uint32_t>(application,cp.globals.at(100),1)==17,"hook application global registered");
    for(const auto& [id,actor]:cp.actors){(void)id;require(readDetState<std::uint32_t>(application,actor.blocks.at(100),1)==17,"hook defaults precede every created actor");}
    for(unsigned throwing=0;throwing<2;++throwing){DeterministicSession failed(detLevelSessionConfig(l));unsigned observed=0;
        require(!configureDetLevel2D(failed,l,error,{},[&](DeterministicSession&)->bool{++observed;if(throwing)throw std::runtime_error("hook deliberate refusal");return false;}),"hook refusal reports configure failure");
        require(observed==1 && !failed.sealed() && failed.registerTypedSchema(application),"failed hook has no created actors; partial configuration must be discarded");}
    const auto before=calls;DetLevel2D decoded;require(validateDetLevel2D(l,error) && decodeDetLevel2D(encodeDetLevel2D(l),decoded,error),error);
    (void)detLevelActionsConfig2D(l);require(calls==before,"scratch validation/decoder/getters do not run destination hook");
    ++cases;std::cout<<"PASS destination-only actor-free registration hook, application defaults, and false/throw failure scope\n";
}
void legacyChecks(const fs::path& file){DetLevel2D l;std::string error;require(loadDetLevel2D(file,l,error),error);
    // Independently serialized the checked-in format1 fixture's1032 semantic
    // bytes (AYDLV001/profile1/LE declared widths) before this implementation.
    require(l.formatVersion==1 && l.actionTargets.empty() && l.rules.empty()
        && detLevelSessionConfig(l).contentHash==UINT64_C(1216874329789544449),"original format1 canonical content hash pin");
    const auto text=encodeDetLevel2D(l);require(text.starts_with("AYDLEVEL 1\n") && text.find("\nactions")==std::string::npos,"format1 text shape unchanged");
    DeterministicSession s(detLevelSessionConfig(l));require(configureDetLevel2D(s,l,error) && s.seal(),error);const auto cp=checkpoint(s);
    require(!cp.globals.contains(64) && !cp.globals.contains(80),"legacy recipe installs no new facade state");
    bool absent=false;try{(void)detLevelActionsConfig2D(l);}catch(const std::invalid_argument&){absent=true;}require(absent,"no implicit new authority on format1");
    ++cases;std::cout<<"PASS original format1 semantic hash/text/profile closure with no new action authority\n";
}
} // namespace
int main(int argc,char** argv){try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string a=argv[n];if((a=="--output" || a=="--legacy") && n+1<argc){
        if(a=="--output")parent=argv[++n];else legacy=argv[++n];}else throw std::invalid_argument("usage: --output existingParent --legacy format1TerrainFile");}
    require(!parent.empty() && !legacy.empty(),"--output and --legacy are required");const auto dir=fresh(parent);
    legacyChecks(legacy);parserChecks(dir);semanticChecks();closureChecks(dir);identityChecks();registrationHookChecks();
    const auto cpHash=detLevelSessionConfig(scene()).contentHash;
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)
        +",\"parserNegatives\":"+std::to_string(parserNegatives)+",\"semanticNegatives\":"+std::to_string(semanticNegatives)
        +",\"completeCheckpointComparisons\":"+std::to_string(fullComparisons)+",\"ticks\":12,\"contentHash\":\""+std::to_string(cpHash)+"\"}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL levelinteraction2d: "<<e.what()<<'\n';return 1;}}
