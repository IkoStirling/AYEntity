// design reference: AYEntity/design.md Stage59; actual action-controlled door and pre-input Brain2 sight observation.
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicActions2D.h>
#include <AYEntity/DeterministicSave.h>
#include <AYEntity/DeterministicVitality2D.h>
#include <AYEntity/DeterministicAttack2D.h>
#include <AYEntity/DeterministicBrain2D.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYGameLoop.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
constexpr SimEntityId player=10,patient=20,door=101;
constexpr std::uint32_t inputSystem=4,appProfile=83;
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::uint64_t integer(std::string_view text){std::uint64_t value=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && error==std::errc{} && end==text.data()+text.size(),"Invalid unsigned integer");return value;}
void asciiPath(std::string_view value){require(!value.empty(),"Empty path");for(unsigned char ch:value)require(ch>=32 && ch<127,"Example paths must be printable ASCII");}
std::string quoted(std::string_view value){std::string result="\"";for(char ch:value){if(ch=='\\' || ch=='\"')result+='\\';result+=ch;}return result+'\"';}
struct Options {DetHostMode mode=DetHostMode::Live;std::string level="AYRuntime/AYEntity/examples/detsight/content.dlt",output,replay,save,load;
    std::uint64_t ticks=64,seek=0,saveAt=0;bool interactive=false,features=false,seekSet=false,saveAtSet=false;};
Options options(int argc,char** argv){Options opts;std::set<std::string> seen;bool modeSet=false;
    for(int n=1;n<argc;++n){const std::string key=argv[n];require(seen.insert(key).second,"Duplicate option: "+key);
        if(key=="--interactive"){opts.interactive=true;continue;}if(key=="--require-features"){opts.features=true;continue;}
        require(n+1<argc,"Missing option value: "+key);const std::string value=argv[++n];
        if(key=="--ticks")opts.ticks=integer(value);else if(key=="--seek"){opts.seek=integer(value);opts.seekSet=true;}
        else if(key=="--save-at"){opts.saveAt=integer(value);opts.saveAtSet=true;}
        else if(key=="--level" || key=="--output" || key=="--save" || key=="--load"){
            asciiPath(value);if(key=="--level")opts.level=value;else if(key=="--output")opts.output=value;else if(key=="--save")opts.save=value;else opts.load=value;
        }else if(key=="--record" || key=="--replay"){require(!modeSet,"Record/replay are mutually exclusive");modeSet=true;asciiPath(value);
            opts.replay=value;opts.mode=key=="--record"?DetHostMode::Record:DetHostMode::Replay;
        }else throw std::runtime_error("Unknown option: "+key);
    }
    require(opts.ticks>=1 && opts.ticks<=65536,"Ticks must be in1..65536");
    require(!opts.interactive || opts.mode!=DetHostMode::Replay,"Interactive acquisition cannot run during Replay");
    require(!opts.seekSet || (opts.mode==DetHostMode::Replay && opts.seek<=opts.ticks),"Seek requires Replay and seek<=ticks");
    require(opts.saveAtSet==!opts.save.empty(),"--save-at and --save must be supplied together");
    require(!opts.saveAtSet || (opts.mode==DetHostMode::Live && !opts.interactive && opts.saveAt>=1 && opts.saveAt<=opts.ticks),"Timed save requires scripted Live and a tick in range");
    require(opts.load.empty() || opts.mode==DetHostMode::Live,"Persistent load requires Live");
    return opts;
}
bool configure(DeterministicSession& session,const DetLevel2D& level){
    if(!session.registerLogicProfile(appProfile,1,0x4453494748543031ULL))return false;
    const auto actions=detLevelActionsConfig2D(level);
    if(!session.registerSystem(inputSystem,level.character.collision.priority,[actions](DetTickContext& context){
        const auto& commands=context.input().commands;if(commands.size()!=1)return false;const auto& command=commands.front();
        if(command.source!=player || command.sequence!=0 || command.type!=8 || command.payload.size()!=1 || command.payload[0]>1)return false;
        const auto result=executeDetAction2D(context,actions,{door,command.payload[0]?DetActionKind2D::CollisionDisable:DetActionKind2D::CollisionEnable});
        return result.code==DetActionCode2D::Applied || result.code==DetActionCode2D::Unchanged;
    }))return false;
    std::string error;if(!configureDetLevel2D(session,level,error))throw std::runtime_error("Sight content setup: "+error);return true;
}
struct Sample {bool doorOpen=false;};
struct InputBoundary {std::uint64_t tick=0,calls=0;Sample sample;};
class StandardHost {
public:
    StandardHost(const Options& opts,const DetLevel2D& level,InputBoundary& input):
        host_(ayt::app::defaultEngineHost()),loop_(ayt::game::GameLoop::instance()),runtime_(host_),scope_(host_){
        try {loop_.setRenderThreadEnabled(false);loop_.setParallelEnabled(false);loop_.setTimeScale(1);
            require(loop_.setFixedTimestepRatio(level.stepNumerator,level.stepDenominator),"Host rejected fixed step");
            auto& modules=runtime_.modules();require(modules.emplace<EntityComponentModule>().succeeded(),"Add Core component module");
            require(modules.emplace<EntityRuntimeModule>().succeeded(),"Add Entity runtime module");
            DetHostedSceneRecipe recipe;recipe.config=detLevelSessionConfig(level);recipe.mode=opts.mode;recipe.replayPath=opts.replay;recipe.checkpointInterval=4;
            recipe.configure=[level](DeterministicSession& session){return configure(session,level);};
            // Input is already acquired by the external caller before this callback. Sim sees bytes only.
            recipe.input=[&input](const DetHostInputRequest& request,DetTickInput& packet){
                if(request.tick!=input.tick)return false;
                packet={request.tick,request.version,{{static_cast<std::uint32_t>(player),0,8,{static_cast<std::uint8_t>(input.sample.doorOpen)}}}};
                ++input.calls;return true;
            };
            DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};
            require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"Add deterministic Host integration");
            const auto prepared=runtime_.prepare();require(prepared.succeeded(),prepared.message());runtime_.context().componentRegistry().seal();
            const auto installed=runtime_.install();require(installed.succeeded(),installed.message());
            require(loop_.prepareHostedSession(),"Host preparation failed");scope_.refresh();
            scene_=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-sight-example");host_.scenes()->setCurrent(scene_.get());
            controller_=deterministicHost(host_);require(controller_ && controller_->session(),"Missing standard Host Session");
            require(controller_->state()!=DetHostState::Faulted,controller_->error());
        }catch(...){close();throw;}
    }
    ~StandardHost(){close();}
    DeterministicHostController& controller(){return *controller_;}
    void frame(std::uint64_t number){loop_.tickHostedFrame({1.0f/64.0f,number,number});}
private:
    void close()noexcept{loop_.endHostedSession();runtime_.shutdown();host_.scenes()->setCurrent(nullptr);scene_.reset();scope_.reset();}
    ayt::app::IEngineHost& host_;ayt::game::GameLoop& loop_;ayt::app::EngineModuleRuntime runtime_;ayt::app::EngineRuntimeScope scope_;
    std::unique_ptr<ayt::scene::Scene> scene_;DeterministicHostController* controller_=nullptr;
};
std::ofstream freshOutput(const std::string& path){std::ofstream stream(path,std::ios::binary|std::ios::out|std::ios::noreplace);
    require(bool(stream),"Cannot create fresh output: "+path);return stream;}
std::uint32_t doorMask(const DetSessionCheckpoint& cp,const DetActions2DConfig& actions){
    const auto schema=detCollisionBodySchema2D(actions.collision);
    return readDetState<std::uint32_t>(schema,cp.actors.at(door).blocks.at(schema.id),DetBodyMask);
}
DetAttackPhase2D enemyPhase(std::uint64_t tick){
    if(tick<9)return DetAttackPhase2D::Idle;const auto phase=(tick-9)%9;
    if(phase<3)return DetAttackPhase2D::Warmup;if(phase<5)return DetAttackPhase2D::Active;
    if(phase<8)return DetAttackPhase2D::Recovery;return DetAttackPhase2D::Idle;
}
bool historicalDoorBoundary(const DetSessionCheckpoint& cp,const DetActions2DConfig& actions,const DetBrain2DConfig& brain){
    if(cp.nextTick!=9)return false;const auto observed=detBrainState2D(cp,brain,patient);
    require(doorMask(cp,actions)==0 && observed.mode==DetBrainMode2D::SightBlocked && !observed.desiredAttackHeld
        && observed.lastDecisionTick==8 && observed.observedAttackProcessedTick==8,
        "CP9 retains real pre-input SightBlocked while the current registered door mask is open");return true;
}
void handFrame(const DetSessionCheckpoint& cp,const DetActions2DConfig& actions,const DetAttack2DConfig& attack,const DetBrain2DConfig& brain){
    require(cp.nextTick>=1 && cp.nextTick<=64,"Hand frame range");const auto tick=cp.nextTick-1;
    const auto decision=detBrainState2D(cp,brain,patient);const auto foeHP=detVitalityState2D(cp,attack.vitality,patient);
    const auto heroHP=detVitalityState2D(cp,attack.vitality,player);const auto foe=detAttackState2D(cp,attack,patient);
    const auto hit=detAttackResult2D(cp,attack,patient);const bool pulse=tick>=9 && (tick-9)%9==0;
    const auto mode=tick<9?DetBrainMode2D::SightBlocked:pulse?DetBrainMode2D::AttackRequested:DetBrainMode2D::Busy;
    const auto pulses=tick<9?0:(tick-9)/9+1,hurts=tick<12?0:(tick-12)/9+1;
    require(decision.mode==mode && decision.decisionCount==cp.nextTick && decision.attackPulseCount==pulses
        && decision.desiredSpeed.isZero() && decision.desiredAttackHeld==pulse,
        "Hand Brain3 captured before door Input4: blocked0..8, first request9, then finite ready opportunities");
    require(decision.observedAttackProcessedTick==tick && decision.observedAttackPhase==(tick?enemyPhase(tick-1):DetAttackPhase2D::Idle)
        && foe.phase==enemyPhase(tick) && foe.attackCount==pulses && !foe.cancelCount && hit.attemptCount==hurts,
        "W3/A2/R3 hand window includes the separate previous observed callback");
    require(heroHP.hp==100-2*hurts && heroHP.hurtCount==hurts && foeHP.hp==100 && !foeHP.dead,
        "Only first real melee candidate at12+9k hurts2; no Hero attack or synthetic HP writes");
    require(doorMask(cp,actions)==(tick<8?1u:0u) && cp.actors.at(player).pose.position[0]==D{}.bits()
        && cp.actors.at(patient).pose.position[0]==D::fromInt(4).bits()
        && cp.actors.at(player).pose.position[1]==(D::fromInt(1)/D::fromInt(2)).bits()
        && cp.actors.at(patient).pose.position[1]==(D::fromInt(1)/D::fromInt(2)).bits(),"Registered door and stationary supported character geometry");
    const auto state=detActionsStateSchema2D(actions);const auto& words=cp.globals.at(state.id);
    require(readDetState<std::uint64_t>(state,words,DetActionsTotalCount)==cp.nextTick
        && readDetState<std::uint32_t>(state,words,DetActionsLastCode)==static_cast<std::uint32_t>(tick==8?DetActionCode2D::Applied:DetActionCode2D::Unchanged),
        "Every full canonical door sample uses actual registered public action results");
}
void status(const DetSessionCheckpoint& cp,const DetActions2DConfig& actions,const DetAttack2DConfig& attack,const DetBrain2DConfig& brain){
    const auto hero=detVitalityState2D(cp,attack.vitality,player),enemy=detVitalityState2D(cp,attack.vitality,patient);
    const auto state=detBrainState2D(cp,brain,patient);
    std::cout<<"tick="<<cp.nextTick<<" doorMask="<<doorMask(cp,actions)<<" heroHP="<<hero.hp<<" enemyHP="<<enemy.hp
        <<" brain="<<static_cast<std::uint32_t>(state.mode)<<" pulses="<<state.attackPulseCount<<'\n';
}
void help(){std::cout<<"Commands: closed [ticks], open [ticks], idle [ticks], status, save FRESH.dsv, load FILE.dsv, help, quit. Door state persists as a full canonical sample; enemy movement/attack inputs remain Brain-owned.\n";}
int run(const Options& opts){require(std::filesystem::is_regular_file(opts.level) && !std::filesystem::is_symlink(opts.level),"Level must be an ordinary file");
    DetLevel2D level;std::string error;const bool loaded=loadDetLevel2D(opts.level,level,error);require(loaded,error);
    require(level.formatVersion==11 && level.brainLineOfSight && level.applicationVersion==59 && level.inputVersion==8
        && level.stepNumerator==1 && level.stepDenominator==64,"Sight content requires format11/app59/input8/Brain2/step1/64");
    const auto actions=detLevelActionsConfig2D(level);const auto vitality=detLevelVitalityConfig2D(level);
    const auto attack=detLevelAttackConfig2D(level);const auto brain=detLevelBrainConfig2D(level);
    require(actions.targets.size()==1 && actions.targets[0].actor==door && actions.targets[0].collisionMask==1
        && attack.owners.size()==1 && attack.owners[0].actor==patient && brain.lineOfSight,"Expected full fixed door and enemy-only authority");
    if(opts.mode==DetHostMode::Record){auto path=opts.replay;if(!std::filesystem::path(path).has_extension())path+=".rpl";
        require(!std::filesystem::exists(path) && !std::filesystem::exists(ayt::replay::FileReplayRecorder::rotationPathFor(path,0)),"Record destination exists");}
    if(!opts.output.empty())for(const auto* suffix:{".state",".trace",".summary.json"})require(!std::filesystem::exists(opts.output+suffix),"Output already exists");
    if(opts.saveAtSet)require(!std::filesystem::exists(opts.save),"Timed save destination already exists");
    std::ofstream trace;if(!opts.output.empty())trace=freshOutput(opts.output+".trace");
    InputBoundary boundary;StandardHost host(opts,level,boundary);auto& controller=host.controller();const auto recording=controller.recordingPath();
    std::uint64_t saves=0,loads=0,frames=0,traceRows=0;bool timedSaved=false,ownerResumed=false,historicalSeen=false;
    const auto checkpoint=[&]{auto result=controller.checkpoint();require(result.has_value(),controller.error());return *result;};
    const auto historical=[&]{if(opts.features)historicalSeen=historicalDoorBoundary(checkpoint(),actions,brain)||historicalSeen;};
    const auto load=[&](const std::string& path){require(opts.mode==DetHostMode::Live,"Load requires Live");DetSessionCheckpoint saved;
        const bool read=readDetSave(path,saved,error);require(read,error);require(saved.nextTick<=opts.ticks,"Save tick exceeds requested end");
        const bool wasRunning=ownerResumed;ownerResumed=false;
        if(!controller.restore(saved)){const auto diagnostic=controller.error();if(opts.interactive && wasRunning){
                require(controller.resume(),diagnostic+"; failed to restore prior Running lifecycle: "+controller.error());ownerResumed=true;}
            throw std::runtime_error(diagnostic);}
        if(wasRunning){require(controller.resume(),controller.error());ownerResumed=true;}++loads;historical();
        // Reacquire stable IDs on copied reads after every restore; no Entity handle cache.
    };
    if(!opts.load.empty())load(opts.load);
    if(opts.seekSet){require(controller.seek(opts.seek),controller.error());historical();}
    if(controller.state()!=DetHostState::Completed){require(controller.resume(),controller.error());ownerResumed=true;}
    const auto initial=controller.session()->nextTick();require(!opts.saveAtSet || opts.saveAt>initial,"Timed save must lie after starting checkpoint");
    const auto save=[&](const std::string& path){require(opts.mode==DetHostMode::Live,"Save requires Live");const bool saved=writeDetSave(path,checkpoint(),error);require(saved,error);++saves;};
    const auto advance=[&](Sample sample){const auto before=controller.session()->nextTick();boundary.tick=before;boundary.sample=sample;
        require(++frames<=4*(opts.ticks+1),"External frame budget exceeded");host.frame(frames);
        require(controller.state()!=DetHostState::Faulted,controller.error());require(controller.session()->nextTick()==before+1,"Host did not advance one tick");
        const auto current=checkpoint();if(opts.features){handFrame(current,actions,attack,brain);historicalSeen=historicalDoorBoundary(current,actions,brain)||historicalSeen;}
        if(trace.is_open())trace<<current.nextTick<<' '<<detCheckpointHash(current)<<'\n';++traceRows;
        if(controller.state()==DetHostState::Completed)ownerResumed=false;
        if(opts.saveAtSet && current.nextTick==opts.saveAt && !timedSaved){save(opts.save);timedSaved=true;}
    };
    if(opts.interactive){help();status(checkpoint(),actions,attack,brain);bool doorOpen=false;std::string line;
        while(controller.session()->nextTick()<opts.ticks && std::cout<<"> " && std::getline(std::cin,line)){
            std::istringstream in(line);std::string command,arg,extra;in>>command>>arg>>extra;if(command.empty())continue;
            try{require(extra.empty(),"Too many command arguments");
                if(command=="quit" || command=="help" || command=="status"){require(arg.empty(),"Command takes no argument");if(command=="quit")break;
                    if(command=="help")help();else status(checkpoint(),actions,attack,brain);continue;}
                if(command=="save" || command=="load"){asciiPath(arg);if(command=="save")save(arg);else load(arg);status(checkpoint(),actions,attack,brain);continue;}
                require(command=="open" || command=="closed" || command=="idle","Unknown command");
                const auto count=arg.empty()?1:integer(arg);require(count>=1 && count<=opts.ticks-controller.session()->nextTick(),"Command exceeds tick limit");
                if(command!="idle")doorOpen=command=="open";for(std::uint64_t n=0;n<count;++n)advance({doorOpen});status(checkpoint(),actions,attack,brain);
            }catch(const std::exception& e){if(controller.state()==DetHostState::Faulted)throw;std::cout<<"Command rejected: "<<e.what()<<'\n';}
        }
    }else while(controller.session()->nextTick()<opts.ticks){const auto tick=controller.session()->nextTick();advance(opts.mode==DetHostMode::Replay?Sample{}:Sample{tick>=8});}
    const auto final=checkpoint();if(!opts.interactive)require(final.nextTick==opts.ticks,"Final tick mismatch");require(!opts.saveAtSet || timedSaved,"Timed save did not occur");
    if(opts.mode==DetHostMode::Replay){require(controller.state()==DetHostState::Completed,"Replay not completed");require(boundary.calls==0,"Replay acquired live input");}
    if(opts.mode==DetHostMode::Record)require(controller.stop(),controller.error());
    const auto hero=detVitalityState2D(final,vitality,player),enemy=detVitalityState2D(final,vitality,patient);
    const auto foe=detAttackState2D(final,attack,patient);const auto hit=detAttackResult2D(final,attack,patient);const auto decision=detBrainState2D(final,brain,patient);
    if(opts.features)require(final.nextTick==64 && historicalSeen && doorMask(final,actions)==0 && hero.hp==88 && hero.hurtCount==6 && enemy.hp==100
        && foe.attackCount==7 && !foe.cancelCount && hit.attemptCount==6 && foe.phase==DetAttackPhase2D::Warmup
        && decision.mode==DetBrainMode2D::AttackRequested && decision.decisionCount==64 && decision.attackPulseCount==7,"Final finite SightHost content checks");
    std::ostringstream summary;summary<<"{\"schema\":1,\"status\":\"passed\",\"scenario\":\"sight\",\"mode\":"
        <<quoted(opts.mode==DetHostMode::Live?"live":opts.mode==DetHostMode::Record?"record":"replay")<<",\"interactive\":"<<(opts.interactive?"true":"false")
        <<",\"initialTick\":"<<initial<<",\"nextTick\":"<<final.nextTick<<",\"traceRows\":"<<traceRows<<",\"inputCallbacks\":"<<boundary.calls
        <<",\"saves\":"<<saves<<",\"loads\":"<<loads<<",\"featureChecks\":"<<(opts.features?"true":"false")
        <<",\"historicalBlockedMaskOpenSeen\":"<<(historicalSeen?"true":"false")<<",\"inputPayloadBytes\":1,\"levelProfile\":11,\"brainProfile\":2"
        <<",\"finalHash\":"<<quoted(std::to_string(detCheckpointHash(final)))<<",\"recordingPath\":"<<quoted(recording)
        <<",\"contentHash\":"<<quoted(std::to_string(detLevelSessionConfig(level).contentHash))<<",\"doorMask\":"<<doorMask(final,actions)
        <<",\"hero\":{\"hp\":"<<hero.hp<<",\"hurts\":"<<hero.hurtCount<<"},\"enemy\":{\"hp\":"<<enemy.hp<<",\"attacks\":"<<foe.attackCount
        <<",\"attempts\":"<<hit.attemptCount<<",\"cancels\":"<<foe.cancelCount<<",\"decisions\":"<<decision.decisionCount
        <<",\"pulses\":"<<decision.attackPulseCount<<",\"mode\":"<<static_cast<std::uint32_t>(decision.mode)<<"}}\n";
    if(!opts.output.empty()){auto state=freshOutput(opts.output+".state");const auto data=encodeDetCheckpoint(final);state.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
        state.close();trace.close();require(bool(state) && bool(trace),"State/trace write failed");auto json=freshOutput(opts.output+".summary.json");json<<summary.str();json.close();require(bool(json),"Summary write failed");}
    status(final,actions,attack,brain);std::cout<<summary.str();return 0;
}
} // namespace
int main(int argc,char** argv){if(argc==2 && std::string_view(argv[1])=="--help"){
    std::cout<<"AYEntity_DetSightExample --level content.dlt [--ticks 64] [--interactive] [--require-features] [--output freshPrefix]"
        " [--save-at 9 --save fresh.dsv] [--load file.dsv] [--record fresh.rpl | --replay actual_000.rpl] [--seek 9]\n";return 0;}
    Options opts;try{opts=options(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    try{return run(opts);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
