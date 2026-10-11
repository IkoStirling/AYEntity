// Stage35: actual console acquisition outside Sim, canonical commands inside Sim.
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/DeterministicInteraction2D.h>
#include <AYEntity/DeterministicLevel2D.h>
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
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
constexpr std::uint32_t statsSchema=24,inputSystem=6,observeSystem=63;
constexpr SimEntityId player=10,door=110,platform=130,collectible=220;
enum Stats : std::uint32_t {DoorWasClosed=10,PathWasPaused=20,ItemWasPresent=30,
    OpenCount=40,CloseCount=50,PauseCount=60,ResumeCount=70,CollectCount=80,
    OccupiedCount=90,RestartDeniedCount=100};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::uint64_t integer(std::string_view text){std::uint64_t value=0;
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && error==std::errc{} && end==text.data()+text.size(),"Invalid unsigned integer");return value;}
void asciiPath(std::string_view text){require(!text.empty(),"Empty path");
    for(unsigned char byte:text)require(byte>=32 && byte<127,"Example file paths must be printable ASCII");}
std::string quoted(std::string_view text){std::string out="\"";
    for(char byte:text){if(byte=='\\' || byte=='\"')out+='\\';out+=byte;}return out+'\"';}
struct Options {
    DetHostMode mode=DetHostMode::Live;
    std::string level="AYRuntime/AYEntity/examples/detinteraction/interaction.dlt",replay,output;
    std::uint64_t ticks=160,seek=0;
    bool seekSet=false,interactive=false,checkRestore=false,requireFeatures=false;
};
Options options(int argc,char** argv){Options value;bool modeSet=false;std::set<std::string> seen;
    for(int n=1;n<argc;++n){const std::string key=argv[n];require(seen.insert(key).second,"Duplicate option: "+key);
        if(key=="--interactive"){value.interactive=true;continue;}
        if(key=="--check-restore"){value.checkRestore=true;continue;}
        if(key=="--require-features"){value.requireFeatures=true;continue;}
        require(n+1<argc,"Option requires a value: "+key);const std::string argument=argv[++n];
        if(key=="--level"){asciiPath(argument);value.level=argument;}
        else if(key=="--output"){asciiPath(argument);value.output=argument;}
        else if(key=="--ticks")value.ticks=integer(argument);
        else if(key=="--seek"){value.seek=integer(argument);value.seekSet=true;}
        else if(key=="--record" || key=="--replay"){
            require(!modeSet,"Record and replay are mutually exclusive");modeSet=true;asciiPath(argument);value.replay=argument;
            value.mode=key=="--record"?DetHostMode::Record:DetHostMode::Replay;
        }else throw std::runtime_error("Unknown option: "+key);
    }
    require(value.ticks>=1 && value.ticks<=65536,"Ticks must be in1..65536");
    require(!value.seekSet || (value.mode==DetHostMode::Replay && value.seek<=value.ticks),"Seek requires replay and seek<=ticks");
    require(!value.interactive || value.mode!=DetHostMode::Replay,"Interactive input is Live/Record only");
    require(!value.checkRestore || (value.mode==DetHostMode::Live && !value.interactive && value.ticks>=64),
        "Restore check requires scripted Live and at least64ticks");return value;
}
DetTypedStateSchema statistics(){return {statsSchema,1,{{DoorWasClosed,true},{PathWasPaused,false},{ItemWasPresent,true},
    {OpenCount,std::uint64_t{0}},{CloseCount,std::uint64_t{0}},{PauseCount,std::uint64_t{0}},
    {ResumeCount,std::uint64_t{0}},{CollectCount,std::uint64_t{0}},{OccupiedCount,std::uint64_t{0}},
    {RestartDeniedCount,std::uint64_t{0}}}};}
void count(DetTickContext& ctx,std::uint32_t field){const auto before=ctx.readGlobal<std::uint64_t>(statsSchema,field);
    require(before!=UINT64_MAX,"Example feature counter overflow");ctx.writeGlobal(statsSchema,field,before+1);}
DetActions2DConfig actionPolicy(const DetLevel2D& level){DetActions2DConfig c;c.collision=level.character.collision;
    c.targets={{door,1,true,std::nullopt},{platform,16,true,detLevelPathConfig2D(level,1)},
        {collectible,1,true,std::nullopt}};return c;}
DetInteraction2DConfig interactionPolicy(const DetActions2DConfig& actions){DetInteraction2DConfig c;c.actions=actions;
    c.rules={{1,200,player,DetTriggerPhase2D::Enter,{door,DetActionKind2D::CollisionDisable},false,8,2},
        {2,200,player,DetTriggerPhase2D::Exit,{platform,DetActionKind2D::PathPause},false,8,0},
        {3,210,player,DetTriggerPhase2D::Enter,{platform,DetActionKind2D::PathResume},false,8,2},
        {4,collectible,player,DetTriggerPhase2D::Enter,{collectible,DetActionKind2D::CollisionDisable},true,0,0},
        {5,collectible,player,DetTriggerPhase2D::Exit,{door,DetActionKind2D::CollisionEnable},false,0,8}};return c;}
bool configure(DeterministicSession& session,const DetLevel2D& level,const DetInteraction2DConfig& rules){
    if(!session.registerTypedSchema(statistics()) || !session.registerGlobalState(statsSchema)
        || !session.registerLogicProfile(observeSystem,1,0x44494e54484f5331ULL)
        || !installDetActions2D(session,rules.actions) || !installDetInteraction2D(session,rules))return false;
    if(!session.registerSystem(inputSystem,level.character.collision.priority,[level,actions=rules.actions](DetTickContext& ctx){
        const auto& commands=ctx.input().commands;
        if(commands.empty() || commands.size()>2)return false;
        const auto& movement=commands[0];
        if(movement.source!=player || movement.sequence!=0 || movement.type!=1 || movement.payload.size()!=2
            || movement.payload[0]>4 || movement.payload[1]>1)return false;
        detCharacterInput2D(ctx,player,level.character,{D::fromInt(static_cast<int>(movement.payload[0])-2),movement.payload[1]!=0});
        if(commands.size()==2){const auto& command=commands[1];
            if(command.source!=100 || command.sequence!=0 || command.type!=2 || command.payload.size()!=9
                || command.payload[8]<1 || command.payload[8]>7)return false;
            SimEntityId target=0;for(unsigned n=0;n<8;++n)target|=SimEntityId{command.payload[n]}<<(8*n);
            const auto result=executeDetAction2D(ctx,actions,{target,static_cast<DetActionKind2D>(command.payload[8])});
            if(result.code==DetActionCode2D::Occupied)count(ctx,OccupiedCount);
            if(result.code==DetActionCode2D::RestartAwayFromOrigin)count(ctx,RestartDeniedCount);
        }return true;
    }))return false;
    std::string error;if(!configureDetLevel2D(session,level,error))throw std::runtime_error("Level configuration: "+error);
    return session.registerSystem(observeSystem,level.character.collision.priority,[actions=rules.actions](DetTickContext& ctx){
        const bool closed=ctx.read<std::uint32_t>(door,actions.collision.bodySchema,DetBodyMask)!=0;
        const bool paused=detPathMotionState2D(ctx,platform,*actions.targets[1].path).paused;
        const bool present=ctx.read<std::uint32_t>(collectible,actions.collision.bodySchema,DetBodyMask)!=0;
        if(closed!=ctx.readGlobal<bool>(statsSchema,DoorWasClosed))count(ctx,closed?CloseCount:OpenCount);
        if(paused!=ctx.readGlobal<bool>(statsSchema,PathWasPaused))count(ctx,paused?PauseCount:ResumeCount);
        if(!present && ctx.readGlobal<bool>(statsSchema,ItemWasPresent))count(ctx,CollectCount);
        ctx.writeGlobal(statsSchema,DoorWasClosed,closed);ctx.writeGlobal(statsSchema,PathWasPaused,paused);
        ctx.writeGlobal(statsSchema,ItemWasPresent,present);return true;
    });
}
struct Sample {int speed=0;bool jump=false;std::optional<DetActionRequest2D> action;};
struct InputBoundary {std::uint64_t tick=0,calls=0;Sample sample;};
Sample scripted(std::uint64_t tick){Sample sample;sample.speed=2;
    if(tick==44)sample.action=DetActionRequest2D{platform,DetActionKind2D::PathRestart};
    if(tick==88)sample.action=DetActionRequest2D{door,DetActionKind2D::CollisionEnable};return sample;}
class StandardHost {
public:
    StandardHost(const Options& opts,const DetLevel2D& level,const DetInteraction2DConfig& rules,InputBoundary& boundary):
        host_(ayt::app::defaultEngineHost()),loop_(ayt::game::GameLoop::instance()),runtime_(host_),scope_(host_){
        try{loop_.setRenderThreadEnabled(false);loop_.setParallelEnabled(false);loop_.setTimeScale(1);
            require(loop_.setFixedTimestepRatio(level.stepNumerator,level.stepDenominator),"Host rejected fixed step");
            auto& modules=runtime_.modules();require(modules.emplace<EntityComponentModule>().succeeded(),"Add Entity components");
            require(modules.emplace<EntityRuntimeModule>().succeeded(),"Add Entity runtime");
            DetHostedSceneRecipe recipe;recipe.config=detLevelSessionConfig(level);recipe.mode=opts.mode;
            recipe.replayPath=opts.replay;recipe.checkpointInterval=32;
            recipe.configure=[level,rules](DeterministicSession& session){return configure(session,level,rules);};
            recipe.input=[&boundary](const DetHostInputRequest& request,DetTickInput& packet){
                if(request.tick!=boundary.tick || boundary.sample.speed<-2 || boundary.sample.speed>2)return false;
                packet={request.tick,request.version,{{static_cast<std::uint32_t>(player),0,1,
                    {static_cast<std::uint8_t>(boundary.sample.speed+2),static_cast<std::uint8_t>(boundary.sample.jump)}}}};
                if(boundary.sample.action){const auto action=*boundary.sample.action;std::vector<std::uint8_t> bytes;
                    for(unsigned n=0;n<8;++n)bytes.push_back(static_cast<std::uint8_t>(action.target>>(8*n)));
                    bytes.push_back(static_cast<std::uint8_t>(action.kind));packet.commands.push_back({100,0,2,std::move(bytes)});}
                ++boundary.calls;return true;
            };
            DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};
            require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"Add deterministic Host");
            const auto prepared=runtime_.prepare();require(prepared.succeeded(),prepared.message());runtime_.context().componentRegistry().seal();
            const auto installed=runtime_.install();require(installed.succeeded(),installed.message());
            require(loop_.prepareHostedSession(),"Host preparation failed");scope_.refresh();
            scene_=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-interaction-example");
            host_.scenes()->setCurrent(scene_.get());controller_=deterministicHost(host_);
            require(controller_ && controller_->session(),"Missing module-owned Session");
            require(controller_->state()!=DetHostState::Faulted,controller_->error());
        }catch(...){close();throw;}
    }
    ~StandardHost(){close();}
    DeterministicHostController& controller(){return *controller_;}
    void frame(std::uint64_t number){loop_.tickHostedFrame({1.0f/64.0f,number,number});}
private:
    void close()noexcept{loop_.endHostedSession();runtime_.shutdown();host_.scenes()->setCurrent(nullptr);scene_.reset();scope_.reset();}
    ayt::app::IEngineHost& host_;ayt::game::GameLoop& loop_;
    ayt::app::EngineModuleRuntime runtime_;ayt::app::EngineRuntimeScope scope_;
    std::unique_ptr<ayt::scene::Scene> scene_;DeterministicHostController* controller_=nullptr;
};
std::ofstream freshOutput(const std::string& path){require(!std::filesystem::exists(path),"Output already exists: "+path);
    std::ofstream file(path,std::ios::out|std::ios::binary|std::ios::noreplace);require(static_cast<bool>(file),"Cannot create: "+path);return file;}
void status(const DetSessionCheckpoint& cp,const DetActions2DConfig& actions){const auto& xy=cp.actors.at(player).pose.position;
    const auto get=[&](std::uint32_t field){return readDetState<bool>(statistics(),cp.globals.at(statsSchema),field);};
    const auto schema=detActionsStateSchema2D(actions);const auto& result=cp.globals.at(actions.stateSchema);
    std::cout<<"tick="<<cp.nextTick<<" player="<<D::fromBits(xy[0]).toFloat()<<','<<D::fromBits(xy[1]).toFloat()
        <<" door="<<(get(DoorWasClosed)?"closed":"open")<<" platform="<<(get(PathWasPaused)?"paused":"running")
        <<" collectible="<<(get(ItemWasPresent)?"present":"collected")
        <<" lastActionCode="<<readDetState<std::uint32_t>(schema,result,DetActionsLastCode)<<'\n';}
void help(){std::cout<<"Commands: right [ticks], left [ticks], idle [ticks], jump; open, close; pause, resume, enable, disable, restart;"
    " status, save, load, help, quit. Movement persists across mechanism commands. save/load are Live-only."
    " Action codes:1 applied,2 unchanged,6 occupied,7 restart away from origin.\n";}
int run(const Options& opts){require(std::filesystem::is_regular_file(opts.level) && !std::filesystem::is_symlink(opts.level),"Level must be an ordinary file");
    DetLevel2D level;std::string error;require(loadDetLevel2D(opts.level,level,error),error);
    require(level.stepNumerator==1 && level.stepDenominator==64,"Example requires1/64fixed step");
    const auto rules=interactionPolicy(actionPolicy(level));
    if(opts.mode==DetHostMode::Record){auto path=opts.replay;if(!std::filesystem::path(path).has_extension())path+=".rpl";
        require(!std::filesystem::exists(path) && !std::filesystem::exists(ayt::replay::FileReplayRecorder::rotationPathFor(path,0)),"Record destination exists");}
    if(!opts.output.empty())for(const auto* suffix:{".state",".trace",".summary.json"})
        require(!std::filesystem::exists(opts.output+suffix),"Output already exists: "+opts.output+suffix);
    std::ofstream trace;if(!opts.output.empty())trace=freshOutput(opts.output+".trace");
    InputBoundary boundary;StandardHost host(opts,level,rules,boundary);auto& controller=host.controller();const auto recording=controller.recordingPath();
    if(opts.seekSet)require(controller.seek(opts.seek),controller.error());
    if(controller.state()!=DetHostState::Completed)require(controller.resume(),controller.error());
    const auto initial=controller.session()->nextTick();std::uint64_t frames=0,traceRows=0,restores=0;
    const auto checkpoint=[&]{auto cp=controller.checkpoint();require(cp.has_value(),controller.error());return *cp;};
    const auto advance=[&](Sample sample){const auto before=controller.session()->nextTick();
        boundary.tick=before;boundary.sample=sample;require(++frames<=4*(opts.ticks+1),"Bounded input/frame budget exceeded");
        host.frame(frames);require(controller.state()!=DetHostState::Faulted,controller.error());
        require(controller.session()->nextTick()==before+1,"Host did not advance exactly one tick");
        const auto cp=checkpoint();if(trace.is_open())trace<<cp.nextTick<<' '<<detCheckpointHash(cp)<<'\n';++traceRows;
    };
    if(opts.interactive){help();status(checkpoint(),rules.actions);std::optional<DetSessionCheckpoint> saved;int speed=0;std::string line;
        while(controller.session()->nextTick()<opts.ticks && std::cout<<"> " && std::getline(std::cin,line)){
            std::istringstream input(line);std::string command,number,extra;input>>command;if(command.empty())continue;
            input>>number>>extra;
            try{require(extra.empty(),"Too many command arguments");
                if(command=="quit"){require(number.empty(),"quit takes no argument");break;}
                if(command=="help" || command=="status" || command=="save" || command=="load"){
                    require(number.empty(),"This command takes no argument");
                    if(command=="help")help();else if(command=="status")status(checkpoint(),rules.actions);
                    else if(command=="save"){require(opts.mode==DetHostMode::Live,"save/load require Live");saved=checkpoint();std::cout<<"Saved tick="<<saved->nextTick<<'\n';}
                    else {require(opts.mode==DetHostMode::Live && saved.has_value(),"No Live checkpoint saved");
                        require(controller.restore(*saved),controller.error());require(controller.resume(),controller.error());++restores;
                        status(checkpoint(),rules.actions);}continue;
                }
                Sample sample;sample.speed=speed;std::uint64_t ticks=1;
                if(command=="right" || command=="left" || command=="idle"){
                    sample.speed=command=="right"?2:command=="left"?-2:0;ticks=number.empty()?1:integer(number);
                }else {require(number.empty(),"Mechanism/jump commands take no argument");
                    if(command=="jump")sample.jump=true;
                    else if(command=="open")sample.action=DetActionRequest2D{door,DetActionKind2D::CollisionDisable};
                    else if(command=="close")sample.action=DetActionRequest2D{door,DetActionKind2D::CollisionEnable};
                    else if(command=="pause")sample.action=DetActionRequest2D{platform,DetActionKind2D::PathPause};
                    else if(command=="resume")sample.action=DetActionRequest2D{platform,DetActionKind2D::PathResume};
                    else if(command=="enable")sample.action=DetActionRequest2D{platform,DetActionKind2D::PathEnable};
                    else if(command=="disable")sample.action=DetActionRequest2D{platform,DetActionKind2D::PathDisable};
                    else if(command=="restart")sample.action=DetActionRequest2D{platform,DetActionKind2D::PathRestart};
                    else throw std::runtime_error("Unknown command");}
                require(ticks>=1 && ticks<=opts.ticks-controller.session()->nextTick(),"Command exceeds tick limit");
                speed=sample.speed;
                for(std::uint64_t n=0;n<ticks;++n){advance(sample);sample.jump=false;sample.action.reset();}
                status(checkpoint(),rules.actions);
            }catch(const std::exception& e){if(controller.state()==DetHostState::Faulted)throw;std::cout<<"Command rejected: "<<e.what()<<'\n';}
        }
    }else {std::optional<DetSessionCheckpoint> saved,expected;bool rewound=false;
        while(controller.session()->nextTick()<opts.ticks){const auto before=controller.session()->nextTick();
            advance(opts.mode==DetHostMode::Replay?Sample{}:scripted(before));
            if(opts.checkRestore && controller.session()->nextTick()==32 && !rewound)saved=checkpoint();
            if(opts.checkRestore && controller.session()->nextTick()==64){
                if(!rewound){expected=checkpoint();require(saved.has_value(),"Restore checkpoint missing");
                    require(controller.restore(*saved),controller.error());require(controller.resume(),controller.error());rewound=true;++restores;
                }else require(encodeDetCheckpoint(checkpoint())==encodeDetCheckpoint(*expected),"Restored branch differs in complete state");}
        }
    }
    const auto cp=checkpoint();if(!opts.interactive)require(cp.nextTick==opts.ticks,"Final tick mismatch");
    if(opts.mode==DetHostMode::Replay){require(controller.state()==DetHostState::Completed,"Replay has not completed");require(boundary.calls==0,"Replay acquired live input");}
    if(opts.mode==DetHostMode::Record)require(controller.stop(),controller.error());
    const auto get=[&](std::uint32_t field){return readDetState<std::uint64_t>(statistics(),cp.globals.at(statsSchema),field);};
    if(opts.requireFeatures)for(auto field:{OpenCount,CloseCount,PauseCount,ResumeCount,CollectCount,OccupiedCount,RestartDeniedCount})
        require(get(field)>0,"Actual feature missing: "+std::to_string(field));
    std::ostringstream summary;summary<<"{\"schema\":1,\"status\":\"passed\",\"scenario\":\"interaction\",\"mode\":"
        <<quoted(opts.mode==DetHostMode::Live?"live":opts.mode==DetHostMode::Record?"record":"replay")
        <<",\"interactive\":"<<(opts.interactive?"true":"false")<<",\"initialTick\":"<<initial<<",\"nextTick\":"<<cp.nextTick
        <<",\"traceRows\":"<<traceRows<<",\"inputCallbacks\":"<<boundary.calls<<",\"restores\":"<<restores
        <<",\"featureChecks\":"<<(opts.requireFeatures?"true":"false")<<",\"finalHash\":"<<quoted(std::to_string(detCheckpointHash(cp)))
        <<",\"recordingPath\":"<<quoted(recording)<<",\"features\":{\"opened\":"<<get(OpenCount)<<",\"closed\":"<<get(CloseCount)
        <<",\"paused\":"<<get(PauseCount)<<",\"resumed\":"<<get(ResumeCount)<<",\"collected\":"<<get(CollectCount)
        <<",\"occupiedDenied\":"<<get(OccupiedCount)<<",\"restartDenied\":"<<get(RestartDeniedCount)<<"}}\n";
    if(!opts.output.empty()){auto state=freshOutput(opts.output+".state");const auto bytes=encodeDetCheckpoint(cp);
        state.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));state.close();trace.close();
        require(static_cast<bool>(state) && static_cast<bool>(trace),"State/trace output failed");
        auto json=freshOutput(opts.output+".summary.json");json<<summary.str();json.close();require(static_cast<bool>(json),"Summary output failed");}
    status(cp,rules.actions);std::cout<<summary.str();return 0;
}
} // namespace
int main(int argc,char** argv){if(argc==2 && std::string_view(argv[1])=="--help"){
    std::cout<<"AYEntity_DetInteractionExample --level interaction.dlt [--ticks 160] [--interactive] [--output freshPrefix]"
        " [--require-features] [--check-restore] [--record fresh.rpl | --replay actual_000.rpl] [--seek tick]\n";return 0;}
    Options opts;try{opts=options(argc,argv);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    try{return run(opts);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
