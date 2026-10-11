// design reference: AYEntity/design.md Stage39; actual content, progress and persistent saves.
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicProgress2D.h>
#include <AYEntity/DeterministicSave.h>
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
constexpr SimEntityId player=10,door=110,platform=130;
constexpr std::uint32_t appSchema=112,inputSystem=6,progressSystem=8,observeSystem=63,appProfile=70;
enum Stats : std::uint32_t {DoorClosed=10,PathPaused=20,Opened=30,Paused=40,Resumed=50,Carried=60,
    Claims=70,Finishes=80,Denied=90};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::uint64_t integer(std::string_view text){std::uint64_t value=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && error==std::errc{} && end==text.data()+text.size(),"Invalid unsigned integer");return value;}
void asciiPath(std::string_view value){require(!value.empty(),"Empty path");for(unsigned char ch:value)require(ch>=32 && ch<127,"Example paths must be printable ASCII");}
std::string quoted(std::string_view value){std::string result="\"";for(char ch:value){if(ch=='\\' || ch=='\"')result+='\\';result+=ch;}return result+'\"';}
struct Options {DetHostMode mode=DetHostMode::Live;std::string level="AYRuntime/AYEntity/examples/detcontent/content.dlt",output,replay,save,load;
    std::uint64_t ticks=160,seek=0,saveAt=0;bool interactive=false,features=false,seekSet=false,saveAtSet=false;};
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
    require(opts.load.empty() || opts.mode==DetHostMode::Live,"Persistent load requires Live");return opts;
}
DetTypedStateSchema stats(){return {appSchema,1,{{DoorClosed,true},{PathPaused,false},{Opened,std::uint64_t{0}},{Paused,std::uint64_t{0}},
    {Resumed,std::uint64_t{0}},{Carried,std::uint64_t{0}},{Claims,std::uint64_t{0}},{Finishes,std::uint64_t{0}},{Denied,std::uint64_t{0}}}};}
void count(DetTickContext& context,std::uint32_t field){const auto value=context.readGlobal<std::uint64_t>(appSchema,field);
    require(value!=UINT64_MAX,"Application count overflow");context.writeGlobal(appSchema,field,value+1);}
DetProgress2DConfig progressConfig(const DetInteraction2DConfig& interaction){DetProgress2DConfig p;p.actions=interaction.actions;
    p.counters={{1,0,2,0},{2,0,1,0}};p.objectives={{1,{{1,2}}},{2,{{1,2},{2,1}}}};
    p.claims={{1,220,player,1,1},{2,230,player,1,1},{3,240,player,2,1}};return p;}
bool exactEnter(const DetTickCommand& command,const DetCollision2DConfig& collision,SimEntityId trigger){
    if(command.source!=collision.systemId || command.type!=collision.eventType)return false;
    DetTriggerEvent2D event;if(!decodeDetTriggerEvent2D(command.payload,event))return false;
    return event.phase==DetTriggerPhase2D::Enter && ((event.pair.first==player && event.pair.second==trigger) || (event.pair.second==player && event.pair.first==trigger));
}
bool configure(DeterministicSession& session,const DetLevel2D& level,const DetProgress2DConfig& progress){
    const auto schema=stats();const DetStateLayout layout(schema);
    if(!session.registerTypedSchema(schema) || !session.registerGlobalState(appSchema)
        || !session.registerLogicProfile(appProfile,1,0x44434f4e54333931ULL))return false;
    if(!session.registerValidator(appProfile,1,[layout](const DetSessionCheckpoint& cp,std::string& error){
        try {const auto& words=cp.globals.at(appSchema);
            for(auto field:{Opened,Paused,Resumed,Carried,Denied})if(std::get<std::uint64_t>(layout.read(words,field))>cp.nextTick){error="App count exceeds tick frontier";return false;}
            if(std::get<std::uint64_t>(layout.read(words,Claims))>3 || std::get<std::uint64_t>(layout.read(words,Finishes))>1){error="App claim count bound";return false;}
            for(const auto& [id,actor]:cp.actors)if(actor.blocks.at(appSchema)!=layout.defaults()){error="App actor copies must remain defaults";return false;}
            return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }))return false;
    if(!session.registerSystem(inputSystem,level.character.collision.priority,[character=level.character](DetTickContext& context){
        const auto& commands=context.input().commands;if(commands.size()!=1)return false;const auto& command=commands.front();
        if(command.source!=player || command.sequence!=0 || command.type!=1 || command.payload.size()!=4 || command.payload[0]>4
            || command.payload[1]>1 || command.payload[2]>1 || command.payload[3]>1)return false;
        detCharacterInput2D(context,player,character,{D::fromInt(int(command.payload[0])-2),command.payload[1]!=0,command.payload[2]!=0,command.payload[3]!=0});return true;
    }))return false;
    if(!session.registerSystem(progressSystem,level.character.collision.priority,[progress](DetTickContext& context){
        // Only real delivered Enter invokes a claim. A normal denial does not enqueue a retry.
        for(const auto& event:context.events())for(const auto& claim:progress.claims){
            if(!exactEnter(event,progress.actions.collision,claim.trigger))continue;
            if(claim.id==3 && !detProgressObjective2D(context,progress,1))continue;
            const auto result=claimDetCollectible2D(context,progress,claim.id);
            if(result.code==DetProgressCode2D::Applied){count(context,Claims);if(claim.id==3)count(context,Finishes);
                if(claim.id!=3 && detProgressObjective2D(context,progress,1)){
                    const auto action=executeDetAction2D(context,progress.actions,{door,DetActionKind2D::CollisionDisable});
                    if(action.code!=DetActionCode2D::Applied && action.code!=DetActionCode2D::Unchanged)count(context,Denied);
                }
            }else if(result.code!=DetProgressCode2D::AlreadyClaimed)count(context,Denied);
        }return true;
    }))return false;
    std::string error;if(!configureDetLevel2D(session,level,error,{},[progress](DeterministicSession& destination){return installDetProgress2D(destination,progress);}))
        throw std::runtime_error("Content setup: "+error);
    const auto path=detLevelPathConfig2D(level,1);
    return session.registerSystem(observeSystem,level.character.collision.priority,[progress,path,character=level.character](DetTickContext& context){
        const bool closed=context.read<std::uint32_t>(door,progress.actions.collision.bodySchema,DetBodyMask)!=0;
        const auto motion=detPathMotionState2D(context,platform,path);
        if(!closed && context.readGlobal<bool>(appSchema,DoorClosed))count(context,Opened);
        if(motion.paused!=context.readGlobal<bool>(appSchema,PathPaused))count(context,motion.paused?Paused:Resumed);
        const auto state=detCharacterState2D(context,player,character);const auto& hero=context.pose(player);const auto& carrier=context.pose(platform);
        const auto delta=hero.position.x-hero.previousPosition.x;
        if(state.grounded && state.support.value==platform && state.supportVelocity.x!=D{}
            && hero.hasPreviousPosition && carrier.hasPreviousPosition && carrier.position.x!=carrier.previousPosition.x
            && delta-state.velocity.x*context.dt()==state.supportVelocity.x*context.dt())count(context,Carried);
        context.writeGlobal(appSchema,DoorClosed,closed);context.writeGlobal(appSchema,PathPaused,motion.paused);return true;
    });
}
struct Sample {int speed=0;bool jump=false,crouch=false,drop=false;};
struct InputBoundary {std::uint64_t tick=0,calls=0;Sample sample;};
class StandardHost {
public:
    StandardHost(const Options& opts,const DetLevel2D& level,const DetProgress2DConfig& progress,InputBoundary& input):
        host_(ayt::app::defaultEngineHost()),loop_(ayt::game::GameLoop::instance()),runtime_(host_),scope_(host_){
        try {loop_.setRenderThreadEnabled(false);loop_.setParallelEnabled(false);loop_.setTimeScale(1);
            require(loop_.setFixedTimestepRatio(level.stepNumerator,level.stepDenominator),"Host rejected fixed step");
            auto& modules=runtime_.modules();require(modules.emplace<EntityComponentModule>().succeeded(),"Add Core component module");
            require(modules.emplace<EntityRuntimeModule>().succeeded(),"Add Entity runtime module");
            DetHostedSceneRecipe recipe;recipe.config=detLevelSessionConfig(level);recipe.mode=opts.mode;recipe.replayPath=opts.replay;recipe.checkpointInterval=32;
            recipe.configure=[level,progress](DeterministicSession& session){return configure(session,level,progress);};
            // Input is already acquired by the external caller before this callback. Sim sees bytes only.
            recipe.input=[&input](const DetHostInputRequest& request,DetTickInput& packet){
                if(request.tick!=input.tick || input.sample.speed<-2 || input.sample.speed>2)return false;
                packet={request.tick,request.version,{{static_cast<std::uint32_t>(player),0,1,{static_cast<std::uint8_t>(input.sample.speed+2),
                    static_cast<std::uint8_t>(input.sample.jump),static_cast<std::uint8_t>(input.sample.crouch),static_cast<std::uint8_t>(input.sample.drop)}}}};
                ++input.calls;return true;
            };
            DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};
            require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"Add deterministic Host integration");
            const auto prepared=runtime_.prepare();require(prepared.succeeded(),prepared.message());runtime_.context().componentRegistry().seal();
            const auto installed=runtime_.install();require(installed.succeeded(),installed.message());
            require(loop_.prepareHostedSession(),"Host preparation failed");scope_.refresh();
            scene_=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-content-example");host_.scenes()->setCurrent(scene_.get());
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
void status(const DetSessionCheckpoint& cp,const DetProgress2DConfig& progress){const auto& position=cp.actors.at(player).pose.position;
    std::cout<<"tick="<<cp.nextTick<<" hero="<<D::fromBits(position[0]).toFloat()<<','<<D::fromBits(position[1]).toFloat()
        <<" keys="<<detProgressCounter2D(cp,progress,1)<<" finish="<<detProgressCounter2D(cp,progress,2)
        <<" exitUnlocked="<<detProgressObjective2D(cp,progress,1)<<" complete="<<detProgressObjective2D(cp,progress,2)<<'\n';}
void help(){std::cout<<"Commands: right [ticks], left [ticks], idle [ticks], jump, drop, crouch [ticks], stand [ticks], status,"
    " save FRESH.dsv, load FILE.dsv, help, quit. Movement remains until changed. Save/load are Live only.\n";}
int run(const Options& opts){require(std::filesystem::is_regular_file(opts.level) && !std::filesystem::is_symlink(opts.level),"Level must be an ordinary file");
    DetLevel2D level;std::string error;require(loadDetLevel2D(opts.level,level,error),error);
    require(level.applicationVersion==39 && level.inputVersion==1 && level.stepNumerator==1 && level.stepDenominator==64,"Content requires app39/input1/step1/64");
    const auto progress=progressConfig(detLevelInteractionConfig2D(level));
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
        const auto cp=checkpoint();if(trace.is_open())trace<<cp.nextTick<<' '<<detCheckpointHash(cp)<<'\n';++traceRows;
        if(controller.state()==DetHostState::Completed)ownerResumed=false;
        if(opts.saveAtSet && cp.nextTick==opts.saveAt && !timedSaved){save(opts.save);timedSaved=true;}
    };
    if(opts.interactive){help();status(checkpoint(),progress);int speed=0;bool crouch=false;std::string line;
        while(controller.session()->nextTick()<opts.ticks && std::cout<<"> " && std::getline(std::cin,line)){
            std::istringstream in(line);std::string command,arg,extra;in>>command>>arg>>extra;if(command.empty())continue;
            try {require(extra.empty(),"Too many command arguments");
                if(command=="quit" || command=="help" || command=="status"){require(arg.empty(),"Command takes no argument");
                    if(command=="quit")break;if(command=="help")help();else status(checkpoint(),progress);continue;}
                if(command=="save" || command=="load"){asciiPath(arg);if(command=="save")save(arg);else load(arg);status(checkpoint(),progress);continue;}
                Sample sample{speed,false,crouch,false};std::uint64_t ticks=1;
                if(command=="right" || command=="left" || command=="idle" || command=="crouch" || command=="stand"){
                    if(command=="right")sample.speed=2;else if(command=="left")sample.speed=-2;else if(command=="idle")sample.speed=0;
                    else sample.crouch=command=="crouch";ticks=arg.empty()?1:integer(arg);
                }else {require(arg.empty(),"Jump/drop take no argument");if(command=="jump")sample.jump=true;else if(command=="drop")sample.drop=true;else throw std::runtime_error("Unknown command");}
                require(ticks>=1 && ticks<=opts.ticks-controller.session()->nextTick(),"Command exceeds tick limit");speed=sample.speed;crouch=sample.crouch;
                for(std::uint64_t n=0;n<ticks;++n){advance(sample);sample.jump=false;sample.drop=false;}status(checkpoint(),progress);
            }catch(const std::exception& e){if(controller.state()==DetHostState::Faulted)throw;std::cout<<"Command rejected: "<<e.what()<<'\n';}
        }
    }else while(controller.session()->nextTick()<opts.ticks)advance(opts.mode==DetHostMode::Replay?Sample{}:Sample{2,false,false,false});
    const auto cp=checkpoint();if(!opts.interactive)require(cp.nextTick==opts.ticks,"Final tick mismatch");
    require(!opts.saveAtSet || timedSaved,"Timed save did not occur");
    if(opts.mode==DetHostMode::Replay){require(controller.state()==DetHostState::Completed,"Replay not completed");require(boundary.calls==0,"Replay acquired live input");}
    if(opts.mode==DetHostMode::Record)require(controller.stop(),controller.error());
    const auto get=[&](std::uint32_t field){return readDetState<std::uint64_t>(stats(),cp.globals.at(appSchema),field);};
    const auto keys=detProgressCounter2D(cp,progress,1),finish=detProgressCounter2D(cp,progress,2);
    const auto unlocked=detProgressObjective2D(cp,progress,1),complete=detProgressObjective2D(cp,progress,2);
    if(opts.features){require(keys==2 && finish==1 && unlocked && complete,"Actual content progress is incomplete");
        for(auto field:{Opened,Paused,Resumed,Carried})require(get(field)>0,"Missing actual content feature: "+std::to_string(field));
        require(get(Claims)==3 && get(Finishes)==1 && get(Denied)==0,"Unexpected application claim/denial counts");}
    std::ostringstream summary;summary<<"{\"schema\":1,\"status\":\"passed\",\"scenario\":\"content\",\"mode\":"
        <<quoted(opts.mode==DetHostMode::Live?"live":opts.mode==DetHostMode::Record?"record":"replay")<<",\"interactive\":"<<(opts.interactive?"true":"false")
        <<",\"initialTick\":"<<initial<<",\"nextTick\":"<<cp.nextTick<<",\"traceRows\":"<<traceRows<<",\"inputCallbacks\":"<<boundary.calls
        <<",\"saves\":"<<saves<<",\"loads\":"<<loads<<",\"saveAt\":"<<opts.saveAt<<",\"savePath\":"<<quoted(opts.save)
        <<",\"featureChecks\":"<<(opts.features?"true":"false")<<",\"finalHash\":"<<quoted(std::to_string(detCheckpointHash(cp)))
        <<",\"recordingPath\":"<<quoted(recording)<<",\"contentHash\":"<<quoted(std::to_string(detLevelSessionConfig(level).contentHash))
        <<",\"keys\":"<<keys<<",\"finish\":"<<finish<<",\"exitUnlocked\":"<<(unlocked?"true":"false")<<",\"complete\":"<<(complete?"true":"false")
        <<",\"features\":{\"opened\":"<<get(Opened)<<",\"paused\":"<<get(Paused)<<",\"resumed\":"<<get(Resumed)<<",\"carriedTicks\":"<<get(Carried)
        <<",\"claims\":"<<get(Claims)<<",\"finishes\":"<<get(Finishes)<<",\"denied\":"<<get(Denied)<<"}}\n";
    if(!opts.output.empty()){auto state=freshOutput(opts.output+".state");const auto data=encodeDetCheckpoint(cp);
        state.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));state.close();trace.close();
        require(bool(state) && bool(trace),"State/trace write failed");auto json=freshOutput(opts.output+".summary.json");json<<summary.str();json.close();require(bool(json),"Summary write failed");}
    status(cp,progress);std::cout<<summary.str();return 0;
}
} // namespace
int main(int argc,char** argv){if(argc==2 && std::string_view(argv[1])=="--help"){
    std::cout<<"AYEntity_DetContentExample --level content.dlt [--ticks 160] [--interactive] [--require-features] [--output freshPrefix]"
        " [--save-at 64 --save fresh.dsv] [--load file.dsv] [--record fresh.rpl | --replay actual_000.rpl] [--seek 64]\n";return 0;}
    Options opts;try{opts=options(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    try{return run(opts);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
