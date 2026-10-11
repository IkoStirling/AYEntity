// design reference: AYEntity/design.md Stage56; finite dash input ownership, actual wall/jump and disjoint enemy Brain.
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicProgress2D.h>
#include <AYEntity/DeterministicSave.h>
#include <AYEntity/DeterministicCharacterRespawnFlow2D.h>
#include <AYEntity/DeterministicVitality2D.h>
#include <AYEntity/DeterministicAttack2D.h>
#include <AYEntity/DeterministicBrain2D.h>
#include <AYEntity/DeterministicCharacterControl2D.h>
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
constexpr SimEntityId player=10,patient=20;
constexpr std::uint32_t inputSystem=4,appProfile=81;
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::uint64_t integer(std::string_view text){std::uint64_t value=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && error==std::errc{} && end==text.data()+text.size(),"Invalid unsigned integer");return value;}
void asciiPath(std::string_view value){require(!value.empty(),"Empty path");for(unsigned char ch:value)require(ch>=32 && ch<127,"Example paths must be printable ASCII");}
std::string quoted(std::string_view value){std::string result="\"";for(char ch:value){if(ch=='\\' || ch=='\"')result+='\\';result+=ch;}return result+'\"';}
struct Options {DetHostMode mode=DetHostMode::Live;std::string level="AYRuntime/AYEntity/examples/detcontrol/content.dlt",output,replay,save,load;
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
bool configure(DeterministicSession& session,const DetLevel2D& level,const DetProgress2DConfig&){
    if(!session.registerLogicProfile(appProfile,1,0x444353484f535431ULL))return false;
    if(!session.registerSystem(inputSystem,level.character.collision.priority,[character=level.character,attack=detLevelAttackConfig2D(level),control=detLevelCharacterControlConfig2D(level)](DetTickContext& context){
        const auto& commands=context.input().commands;if(commands.size()!=1)return false;const auto& command=commands.front();
        if(command.source!=player || command.sequence!=0 || command.type!=7 || command.payload.size()!=8 || command.payload[0]>4)return false;
        for(unsigned n=1;n<8;++n)if(command.payload[n]>1)return false;
        detCharacterControlInput2D(context,control,player,{{D::fromInt(int(command.payload[0])-2),command.payload[1]!=0,command.payload[2]!=0,command.payload[3]!=0},command.payload[6]!=0,command.payload[5]!=0,command.payload[7]!=0});
        detSetAttackInput2D(context,attack,player,{command.payload[4]!=0,command.payload[5]!=0});return true;
    }))return false;
    std::string error;if(!configureDetLevel2D(session,level,error))throw std::runtime_error("Content setup: "+error);return true;
}
struct Sample {int speed=0;bool jump=false,crouch=false,drop=false,held=false,facing=true,dash=false,cancel=false;};
struct InputBoundary {std::uint64_t tick=0,calls=0;Sample sample;};
class StandardHost {
public:
    StandardHost(const Options& opts,const DetLevel2D& level,const DetProgress2DConfig& progress,InputBoundary& input):
        host_(ayt::app::defaultEngineHost()),loop_(ayt::game::GameLoop::instance()),runtime_(host_),scope_(host_){
        try {loop_.setRenderThreadEnabled(false);loop_.setParallelEnabled(false);loop_.setTimeScale(1);
            require(loop_.setFixedTimestepRatio(level.stepNumerator,level.stepDenominator),"Host rejected fixed step");
            auto& modules=runtime_.modules();require(modules.emplace<EntityComponentModule>().succeeded(),"Add Core component module");
            require(modules.emplace<EntityRuntimeModule>().succeeded(),"Add Entity runtime module");
            DetHostedSceneRecipe recipe;recipe.config=detLevelSessionConfig(level);recipe.mode=opts.mode;recipe.replayPath=opts.replay;recipe.checkpointInterval=4;
            recipe.configure=[level,progress](DeterministicSession& session){return configure(session,level,progress);};
            // Input is already acquired by the external caller before this callback. Sim sees bytes only.
            recipe.input=[&input](const DetHostInputRequest& request,DetTickInput& packet){
                if(request.tick!=input.tick || input.sample.speed<-2 || input.sample.speed>2)return false;
                packet={request.tick,request.version,{{static_cast<std::uint32_t>(player),0,7,{static_cast<std::uint8_t>(input.sample.speed+2),
                    static_cast<std::uint8_t>(input.sample.jump),static_cast<std::uint8_t>(input.sample.crouch),static_cast<std::uint8_t>(input.sample.drop),
                    static_cast<std::uint8_t>(input.sample.held),static_cast<std::uint8_t>(input.sample.facing),
                    static_cast<std::uint8_t>(input.sample.dash),static_cast<std::uint8_t>(input.sample.cancel)}}}};
                ++input.calls;return true;
            };
            DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};
            require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"Add deterministic Host integration");
            const auto prepared=runtime_.prepare();require(prepared.succeeded(),prepared.message());runtime_.context().componentRegistry().seal();
            const auto installed=runtime_.install();require(installed.succeeded(),installed.message());
            require(loop_.prepareHostedSession(),"Host preparation failed");scope_.refresh();
            scene_=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-control-example");host_.scenes()->setCurrent(scene_.get());
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
void handFrame(const DetSessionCheckpoint& cp,const DetCharacterControl2DConfig& control){
    require(cp.nextTick>=1 && cp.nextTick<=64,"Hand frame range");const auto t=cp.nextTick-1;
    const auto state=detCharacterControlState2D(cp,control,player);
    const bool active=(t>=1 && t<=8) || (t>=17 && t<=19) || (t>=41 && t<=48) || t>=57;
    const bool cooldown=(t>=9 && t<=16) || (t>=20 && t<=32) || (t>=49 && t<=56);
    const auto phase=active?DetCharacterControlPhase2D::Active:cooldown?DetCharacterControlPhase2D::Cooldown:DetCharacterControlPhase2D::Ready;
    const std::uint64_t starts=t<1?0:t<17?1:t<41?2:t<57?3:4;
    std::uint64_t requests=0,denied=0;for(auto at:{1u,4u,10u,17u,21u,26u,33u,41u,44u,50u,57u})if(at<=t)++requests;
    for(auto at:{4u,10u,21u,26u,33u,44u,50u})if(at<=t)++denied;
    require(state.decisionCount==cp.nextTick && state.phase==phase && state.startCount==starts
        && state.requestCount==requests && state.deniedCount==denied && state.cancelCount==(t<20?0u:1u),"Hand dash window/edge/count mismatch");
    if(t==20)require(state.outcome==DetCharacterControlOutcome2D::Canceled && state.readyTick==33,"Cancel must retain original cooldown");
    if(t==33)require(state.outcome==DetCharacterControlOutcome2D::DeniedGround && !state.observedGrounded,"Airborne press consumes edge");
    D x{};if(t>=1 && t<=8)x=D::fromInt(static_cast<int>(t))/D::fromInt(8);
    else if(t>=9 && t<=16)x=D::fromInt(1);
    else if(t>=17 && t<=19)x=D::fromInt(static_cast<int>(24-t))/D::fromInt(8);
    else if(t>=20 && t<=40)x=D::fromInt(5)/D::fromInt(8);
    else if(t==41)x=D::fromInt(3)/D::fromInt(4);else if(t==42)x=D::fromInt(7)/D::fromInt(8);
    else if(t>=43 && t<=56)x=D::fromInt(1);else if(t>=57)x=D::fromInt(static_cast<int>(64-t))/D::fromInt(8);
    require(cp.actors.at(player).pose.position[0]==x.bits(),"Hand actual dash acceleration/wall/cancel/left return");
    D y=D::fromInt(1)/D::fromInt(2);if(t>=32 && t<=39){static constexpr int numerator[]{6,9,11,12,12,11,9,6};y=D::fromInt(numerator[t-32])/D::fromInt(4);}
    require(cp.actors.at(player).pose.position[1]==y.bits(),"Normal jump survives control override");
    if(active)require(state.emitted.targetSpeed.bits()==D::fromInt(t>=17 && t<=19 || t>=57?-8:8).bits(),"Locked dash facing differs");
    if(t>=44 && t<=48){const auto schema=detCharacterStateSchema2D(control.character);
        require(readDetState<ayt::math::DetVec2>(schema,cp.actors.at(player).blocks.at(schema.id),DetCharacterVelocity).x.bits()==0,"Wall limits actual velocity despite active request");}
}
void status(const DetSessionCheckpoint& cp,const DetAttack2DConfig& attack,const DetBrain2DConfig& brain,const DetCharacterControl2DConfig& control){
    const auto hero=detVitalityState2D(cp,attack.vitality,player),enemy=detVitalityState2D(cp,attack.vitality,patient);
    const auto state=detBrainState2D(cp,brain,patient);const auto movement=detCharacterControlState2D(cp,control,player);
    std::cout<<"tick="<<cp.nextTick<<" heroHP="<<hero.hp<<" enemyHP="<<enemy.hp<<" brain="<<static_cast<std::uint32_t>(state.mode)
        <<" dash="<<static_cast<std::uint32_t>(movement.phase)<<" starts="<<movement.startCount<<" ready="<<movement.readyTick<<'\n';
}
void help(){std::cout<<"Commands: right [ticks], left [ticks], idle [ticks], jump, drop, attackright, attackleft, dashright, dashleft, canceldash, faceleft, faceright, crouch [ticks], stand [ticks], status, save FRESH.dsv, load FILE.dsv, help, quit. Movement/facing persist; attack commands hold for one tick. Enemy input is authored Brain-owned; Hero movement is Control-owned.\n";}
int run(const Options& opts){require(std::filesystem::is_regular_file(opts.level) && !std::filesystem::is_symlink(opts.level),"Level must be an ordinary file");
    DetLevel2D level;std::string error;require(loadDetLevel2D(opts.level,level,error),error);
    require(level.formatVersion==10 && level.applicationVersion==56 && level.inputVersion==7 && level.stepNumerator==1 && level.stepDenominator==64,"Control content requires format10/app56/input7/step1/64");
    const auto progress=detLevelVitalityProgressConfig2D(level);
    const auto vitality=detLevelVitalityConfig2D(level);const auto attack=detLevelAttackConfig2D(level);const auto brain=detLevelBrainConfig2D(level);const auto control=detLevelCharacterControlConfig2D(level);
    if(opts.mode==DetHostMode::Record){auto path=opts.replay;if(!std::filesystem::path(path).has_extension())path+=".rpl";
        require(!std::filesystem::exists(path) && !std::filesystem::exists(ayt::replay::FileReplayRecorder::rotationPathFor(path,0)),"Record destination exists");}
    if(!opts.output.empty())for(const auto* suffix:{".state",".trace",".summary.json"})require(!std::filesystem::exists(opts.output+suffix),"Output already exists");
    if(opts.saveAtSet)require(!std::filesystem::exists(opts.save),"Timed save destination already exists");
    std::ofstream trace;if(!opts.output.empty())trace=freshOutput(opts.output+".trace");
    InputBoundary boundary;StandardHost host(opts,level,progress,boundary);auto& controller=host.controller();const auto recording=controller.recordingPath();
    std::uint64_t saves=0,loads=0,frames=0,traceRows=0;bool timedSaved=false,ownerResumed=false;
    const auto checkpoint=[&]{auto cp=controller.checkpoint();require(cp.has_value(),controller.error());return *cp;};
    const auto load=[&](const std::string& path){require(opts.mode==DetHostMode::Live,"Load requires Live");DetSessionCheckpoint cp;
        require(readDetSave(path,cp,error),error);require(cp.nextTick<=opts.ticks,"Save tick exceeds requested end");
        // The example tracks its explicit lifecycle calls; Running alone does not mean unpaused.
        const bool wasRunning=ownerResumed;ownerResumed=false;
        if(!controller.restore(cp)){
            const auto diagnostic=controller.error();
            if(opts.interactive && wasRunning){
                require(controller.resume(),diagnostic+"; failed to restore prior Running lifecycle: "+controller.error());ownerResumed=true;
            }
            throw std::runtime_error(diagnostic);
        }
        if(wasRunning){require(controller.resume(),controller.error());ownerResumed=true;}++loads;
        // Do not retain Entity/presentation handles across restore; reacquire stable IDs on each read.
    };
    if(!opts.load.empty())load(opts.load);
    if(opts.seekSet)require(controller.seek(opts.seek),controller.error());
    if(controller.state()!=DetHostState::Completed){require(controller.resume(),controller.error());ownerResumed=true;}
    const auto initial=controller.session()->nextTick();
    require(!opts.saveAtSet || opts.saveAt>initial,"Timed save must lie after starting checkpoint");
    const auto save=[&](const std::string& path){require(opts.mode==DetHostMode::Live,"Save requires Live");require(writeDetSave(path,checkpoint(),error),error);++saves;};
    const auto advance=[&](Sample sample){const auto before=controller.session()->nextTick();boundary.tick=before;boundary.sample=sample;
        require(++frames<=4*(opts.ticks+1),"External frame budget exceeded");host.frame(frames);
        require(controller.state()!=DetHostState::Faulted,controller.error());require(controller.session()->nextTick()==before+1,"Host did not advance one tick");
        const auto cp=checkpoint();if(opts.features)handFrame(cp,control);if(trace.is_open())trace<<cp.nextTick<<' '<<detCheckpointHash(cp)<<'\n';++traceRows;
        if(controller.state()==DetHostState::Completed)ownerResumed=false;
        if(opts.saveAtSet && cp.nextTick==opts.saveAt && !timedSaved){save(opts.save);timedSaved=true;}
    };
    if(opts.interactive){help();status(checkpoint(),attack,brain,control);int speed=0;bool crouch=false,facing=true;std::string line;
        while(controller.session()->nextTick()<opts.ticks && std::cout<<"> " && std::getline(std::cin,line)){
            std::istringstream in(line);std::string command,arg,extra;in>>command>>arg>>extra;if(command.empty())continue;
            try {require(extra.empty(),"Too many command arguments");
                if(command=="quit" || command=="help" || command=="status"){require(arg.empty(),"Command takes no argument");
                    if(command=="quit")break;if(command=="help")help();else status(checkpoint(),attack,brain,control);continue;}
                if(command=="save" || command=="load"){asciiPath(arg);if(command=="save")save(arg);else load(arg);status(checkpoint(),attack,brain,control);continue;}
                Sample sample{speed,false,crouch,false};sample.facing=facing;std::uint64_t ticks=1;
                if(command=="right" || command=="left" || command=="idle" || command=="crouch" || command=="stand"){
                    if(command=="right"){sample.speed=2;sample.facing=true;}else if(command=="left"){sample.speed=-2;sample.facing=false;}else if(command=="idle")sample.speed=0;
                    else sample.crouch=command=="crouch";ticks=arg.empty()?1:integer(arg);
                }else {require(arg.empty(),"Single-tick commands take no argument");
                    if(command=="attackright" || command=="attackleft"){sample.held=true;sample.facing=command=="attackright";}
                    else if(command=="dashright" || command=="dashleft"){sample.dash=true;sample.facing=command=="dashright";}
                    else if(command=="canceldash")sample.cancel=true;
                    else if(command=="faceleft" || command=="faceright")sample.facing=command=="faceright";
                    else if(command=="jump")sample.jump=true;else if(command=="drop")sample.drop=true;
                    else throw std::runtime_error("Unknown command");}
                require(ticks>=1 && ticks<=opts.ticks-controller.session()->nextTick(),"Command exceeds tick limit");speed=sample.speed;crouch=sample.crouch;facing=sample.facing;
                for(std::uint64_t n=0;n<ticks;++n){advance(sample);sample.jump=false;sample.drop=false;sample.held=false;sample.dash=false;sample.cancel=false;}status(checkpoint(),attack,brain,control);
            }catch(const std::exception& e){if(controller.state()==DetHostState::Faulted)throw;std::cout<<"Command rejected: "<<e.what()<<'\n';}
        }
    }else while(controller.session()->nextTick()<opts.ticks){
        const auto tick=controller.session()->nextTick();
        Sample sample{};sample.facing=tick<10 || (tick>=21 && tick<57);sample.held=tick==46 || tick==53;sample.jump=tick==32;
        sample.cancel=tick==20;for(auto at:{1u,4u,10u,17u,21u,26u,33u,41u,44u,50u,57u})if(tick==at)sample.dash=true;
        advance(opts.mode==DetHostMode::Replay?Sample{}:sample);
    }
    const auto cp=checkpoint();if(!opts.interactive)require(cp.nextTick==opts.ticks,"Final tick mismatch");
    require(!opts.saveAtSet || timedSaved,"Timed save did not occur");
    if(opts.mode==DetHostMode::Replay){require(controller.state()==DetHostState::Completed,"Replay not completed");require(boundary.calls==0,"Replay acquired live input");}
    if(opts.mode==DetHostMode::Record)require(controller.stop(),controller.error());
    const auto hero=detVitalityState2D(cp,vitality,player),enemy=detVitalityState2D(cp,vitality,patient);
    const auto state=detAttackState2D(cp,attack,player),foe=detAttackState2D(cp,attack,patient);
    const auto hit=detAttackResult2D(cp,attack,player),foeHit=detAttackResult2D(cp,attack,patient);
    const auto decision=detBrainState2D(cp,brain,patient);
    const auto movement=detCharacterControlState2D(cp,control,player);
    if(opts.features)require(cp.nextTick==64 && !hero.dead && !enemy.dead && movement.startCount==4 && movement.requestCount==11
        && movement.deniedCount==7 && movement.cancelCount==1 && movement.phase==DetCharacterControlPhase2D::Active
        && movement.readyTick==73 && decision.decisionCount==64 && decision.attackPulseCount>0,"Finite dash content features missing");
    std::ostringstream summary;summary<<"{\"schema\":1,\"status\":\"passed\",\"scenario\":\"control\",\"mode\":"
        <<quoted(opts.mode==DetHostMode::Live?"live":opts.mode==DetHostMode::Record?"record":"replay")<<",\"interactive\":"<<(opts.interactive?"true":"false")
        <<",\"initialTick\":"<<initial<<",\"nextTick\":"<<cp.nextTick<<",\"traceRows\":"<<traceRows<<",\"inputCallbacks\":"<<boundary.calls
        <<",\"saves\":"<<saves<<",\"loads\":"<<loads<<",\"featureChecks\":"<<(opts.features?"true":"false")
        <<",\"finalHash\":"<<quoted(std::to_string(detCheckpointHash(cp)))<<",\"recordingPath\":"<<quoted(recording)
        <<",\"contentHash\":"<<quoted(std::to_string(detLevelSessionConfig(level).contentHash))
        <<",\"hero\":{\"hp\":"<<hero.hp<<",\"hurts\":"<<hero.hurtCount<<",\"attacks\":"<<state.attackCount<<",\"attempts\":"<<hit.attemptCount
        <<"},\"enemy\":{\"hp\":"<<enemy.hp<<",\"dead\":"<<(enemy.dead?"true":"false")<<",\"hurts\":"<<enemy.hurtCount<<",\"attacks\":"<<foe.attackCount
        <<",\"attempts\":"<<foeHit.attemptCount<<",\"cancels\":"<<foe.cancelCount<<",\"decisions\":"<<decision.decisionCount
        <<",\"pulses\":"<<decision.attackPulseCount<<",\"mode\":"<<static_cast<std::uint32_t>(decision.mode)<<"},\"control\":{\"starts\":"<<movement.startCount
        <<",\"requests\":"<<movement.requestCount<<",\"denied\":"<<movement.deniedCount<<",\"cancels\":"<<movement.cancelCount<<",\"phase\":"<<static_cast<std::uint32_t>(movement.phase)<<",\"readyTick\":"<<movement.readyTick<<"}}\n";
    if(!opts.output.empty()){auto state=freshOutput(opts.output+".state");const auto data=encodeDetCheckpoint(cp);
        state.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));state.close();trace.close();
        require(bool(state) && bool(trace),"State/trace write failed");auto json=freshOutput(opts.output+".summary.json");json<<summary.str();json.close();require(bool(json),"Summary write failed");}
    status(cp,attack,brain,control);std::cout<<summary.str();return 0;
}
} // namespace
int main(int argc,char** argv){if(argc==2 && std::string_view(argv[1])=="--help"){
    std::cout<<"AYEntity_DetControlExample --level content.dlt [--ticks 64] [--interactive] [--require-features] [--output freshPrefix]"
        " [--save-at 4 --save fresh.dsv] [--load file.dsv] [--record fresh.rpl | --replay actual_000.rpl] [--seek 4]\n";return 0;}
    Options opts;try{opts=options(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    try{return run(opts);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
