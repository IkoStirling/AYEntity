// Standard Host level-resource example. Authoritative callbacks capture immutable
// level policy; input acquisition and native-float presentation stay outside Sim.
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYGameLoop.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec2;
constexpr std::uint32_t statsSchema=24,viewSchema=25,inputSystem=10,observeSystem=61,inputType=1;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::uint64_t integer(std::string_view text){std::uint64_t result=0;
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result);
    require(!text.empty() && error==std::errc{} && end==text.data()+text.size(),"Invalid unsigned integer");return result;}
void asciiPath(std::string_view path){require(!path.empty(),"Empty path");
    for(unsigned char byte:path)require(byte>=32 && byte<127,"This file-adapter example requires printable ASCII paths");}
std::string quoted(std::string_view text){std::string result="\"";
    for(char byte:text){if(byte=='\\' || byte=='\"')result+='\\';result+=byte;}return result+'\"';}
struct Options {
    DetHostMode mode=DetHostMode::Live;
    std::string level="AYRuntime/AYEntity/examples/detlevel/terrain.dlt",replay,output;
    std::uint64_t ticks=128,seek=0;unsigned frameDivisor=128;
    bool seekSet=false,requireFeatures=false;
};
Options options(int argc,char** argv){Options value;bool modeSet=false;std::set<std::string> seen;
    for(int n=1;n<argc;++n){const std::string key=argv[n];
        require(seen.insert(key).second,"Duplicate option: "+key);
        if(key=="--require-features"){value.requireFeatures=true;continue;}
        require(n+1<argc,"Option requires a value: "+key);const std::string argument=argv[++n];
        if(key=="--level"){asciiPath(argument);value.level=argument;}
        else if(key=="--output"){asciiPath(argument);value.output=argument;}
        else if(key=="--record" || key=="--replay"){
            require(!modeSet,"Record and replay are mutually exclusive");modeSet=true;asciiPath(argument);value.replay=argument;
            value.mode=key=="--record"?DetHostMode::Record:DetHostMode::Replay;
        }else if(key=="--ticks")value.ticks=integer(argument);
        else if(key=="--seek"){value.seek=integer(argument);value.seekSet=true;}
        else if(key=="--frame-divisor"){const auto divisor=integer(argument);
            require(divisor==64 || divisor==128,"Frame divisor must be64 or128");value.frameDivisor=static_cast<unsigned>(divisor);}
        else throw std::runtime_error("Unknown option: "+key);
    }
    require(value.ticks>=1 && value.ticks<=65536,"Ticks must be in1..65536");
    require(!value.seekSet || (value.mode==DetHostMode::Replay && value.seek<=value.ticks),"Seek requires replay and seek<=ticks");
    return value;
}
DetTypedStateSchema observations(){return {viewSchema,1,{{10,V{}},{20,false},{30,DetEntityRef{}},
    {40,V{}},{50,V{}},{60,V{}}}};}
std::vector<SimEntityId> characters(const DetLevel2D& level){std::vector<SimEntityId> ids;
    for(const auto& actor:level.actors)if(actor.character){
        require(actor.id<=std::numeric_limits<std::uint32_t>::max(),"Example command source requires a32-bit character ID");ids.push_back(actor.id);}
    require(!ids.empty(),"Example needs at least one level character");return ids;
}
const DetLevelActor2D* actor(const DetLevel2D& level,SimEntityId id){
    const auto found=std::lower_bound(level.actors.begin(),level.actors.end(),id,
        [](const auto& value,SimEntityId key){return value.id<key;});
    return found!=level.actors.end() && found->id==id?&*found:nullptr;
}
bool configure(DeterministicSession& session,const DetLevel2D& level,const std::vector<SimEntityId>& ids){
    // Schemas must be registered before configureDetLevel2D adds the actors.
    if(!session.registerTypedSchema({statsSchema,1,{{10,std::uint64_t{0}},{20,std::uint64_t{0}}}})
        || !session.registerGlobalState(statsSchema) || !session.registerTypedSchema(observations())
        || !session.registerLogicProfile(observeSystem,1,0x444c54484f535431ULL))return false;
    const auto policy=level.character;std::vector<SimEntityId> bodyIds;
    for(const auto& body:level.actors)bodyIds.push_back(body.id);
    if(!session.registerSystem(inputSystem,0,[policy,ids,bodyIds](DetTickContext& context){
        if(context.input().commands.size()!=ids.size())return false;
        for(std::size_t n=0;n<ids.size();++n){const auto id=ids[n];const auto& command=context.input().commands[n];
            if(command.source!=id || command.sequence!=0 || command.type!=inputType || command.payload.size()!=4
                || command.payload[0]>4 || command.payload[1]>1 || command.payload[2]>1 || command.payload[3]>1)return false;
            detCharacterInput2D(context,id,policy,{D::fromInt(static_cast<int>(command.payload[0])-2),
                command.payload[1]!=0,command.payload[2]!=0,command.payload[3]!=0});
        }
        for(const auto id:bodyIds){const auto position=context.pose(id).position;
            context.write(id,viewSchema,10,V{position.x,position.y});}
        return true;
    }))return false;
    std::string error;if(!configureDetLevel2D(session,level,error))throw std::runtime_error("Level configuration: "+error);
    return session.registerSystem(observeSystem,0,[level,ids](DetTickContext& context){
        const auto count=[&](std::uint32_t field){const auto before=context.readGlobal<std::uint64_t>(statsSchema,field);
            if(before==UINT64_MAX)throw std::overflow_error("Application feature count overflow");
            context.writeGlobal(statsSchema,field,before+1);};
        for(const auto id:ids){const auto movement=detCharacterState2D(context,id,level.character);
            const auto position=context.pose(id).position;const auto previous=context.read<V>(id,viewSchema,10);
            const bool movedX=position.x.bits()!=previous.x.bits(),moved=movedX || position.y.bits()!=previous.y.bits();
            const auto* support=actor(level,movement.support.value);
            if(movement.grounded && support && support->body.shape!=DetCollisionShape2D::Aabb && !movement.groundNormal.x.isZero()){
                if(support->body.mode==DetBodyMode2D::Static && movedX && !movement.velocity.x.isZero())count(10);
                if(support->body.mode==DetBodyMode2D::MovingObstacle && moved && movement.velocity.x.isZero()
                    && (!movement.supportVelocity.x.isZero() || !movement.supportVelocity.y.isZero())){
                    const auto platform=context.pose(support->id).position;
                    const auto platformBefore=context.read<V>(support->id,viewSchema,10);
                    if(platform.x.bits()!=platformBefore.x.bits() || platform.y.bits()!=platformBefore.y.bits())count(20);
                }
            }
            context.write(id,viewSchema,20,movement.grounded);context.write(id,viewSchema,30,movement.support);
            context.write(id,viewSchema,40,movement.groundNormal);context.write(id,viewSchema,50,movement.velocity);
            context.write(id,viewSchema,60,movement.supportVelocity);
        }return true;
    });
}
struct Sample {int speed=0;bool jump=false,crouch=false,drop=false;};
struct InputBoundary {std::uint64_t tick=0,calls=0;std::vector<Sample> samples;};
// A real application replaces this callback with its already-polled devices.
// It runs before tickHostedFrame; no callback polls native input from inside Sim.
using PrePoll=std::function<std::vector<Sample>(std::uint64_t)>;
PrePoll defaultInput(const std::vector<SimEntityId>& ids){return [ids](std::uint64_t tick){
    std::vector<Sample> result(ids.size());for(std::size_t n=0;n<ids.size();++n){
        if(ids[n]==10)result[n].speed=1;
        else if(ids[n]==11 && tick<64)result[n].speed=1;
        else if(ids[n]==13 && tick<64)result[n].speed=-1;
    }return result; // Neutral rows explicitly release jump, crouch and drop each tick.
};}
class StandardHost {
public:
    StandardHost(const Options& opts,const DetLevel2D& level,const std::vector<SimEntityId>& ids,InputBoundary& boundary):
        host_(ayt::app::defaultEngineHost()),loop_(ayt::game::GameLoop::instance()),runtime_(host_),scope_(host_){
        try{
            loop_.setRenderThreadEnabled(false);loop_.setParallelEnabled(false);loop_.setTimeScale(1);
            require(loop_.setFixedTimestepRatio(level.stepNumerator,level.stepDenominator),"Host rejected the level's fixed step");
            auto& modules=runtime_.modules();
            require(modules.emplace<EntityComponentModule>().succeeded(),"Add Entity component module");
            require(modules.emplace<EntityRuntimeModule>().succeeded(),"Add Entity runtime module");
            DetHostedSceneRecipe recipe;recipe.config=detLevelSessionConfig(level);recipe.mode=opts.mode;
            recipe.replayPath=opts.replay;recipe.checkpointInterval=32;
            recipe.configure=[level,ids](DeterministicSession& session){return configure(session,level,ids);};
            recipe.input=[&boundary,ids](const DetHostInputRequest& request,DetTickInput& packet){
                if(request.tick!=boundary.tick || boundary.samples.size()!=ids.size())return false;
                packet={request.tick,request.version,{}};
                for(std::size_t n=0;n<ids.size();++n){const auto sample=boundary.samples[n];
                    if(sample.speed<-2 || sample.speed>2)return false;
                    packet.commands.push_back({static_cast<std::uint32_t>(ids[n]),0,inputType,
                        {static_cast<std::uint8_t>(sample.speed+2),static_cast<std::uint8_t>(sample.jump),
                         static_cast<std::uint8_t>(sample.crouch),static_cast<std::uint8_t>(sample.drop)}});
                }++boundary.calls;return true;
            };
            DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};
            require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"Add deterministic Host module");
            const auto prepared=runtime_.prepare();require(prepared.succeeded(),prepared.message());
            runtime_.context().componentRegistry().seal();
            const auto installed=runtime_.install();require(installed.succeeded(),installed.message());
            require(loop_.prepareHostedSession(),"Standard Host preparation failed");scope_.refresh();
            scene_=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-terrain-level-example");
            host_.scenes()->setCurrent(scene_.get());controller_=deterministicHost(host_);
            require(controller_ && controller_->session(),"Missing module-owned deterministic Host Session");
            require(controller_->state()!=DetHostState::Faulted,controller_->error());
        }catch(...){close();throw;}
    }
    ~StandardHost(){close();}
    DeterministicHostController& controller(){return *controller_;}
    void frame(unsigned divisor,std::uint64_t number){loop_.tickHostedFrame({1.0f/static_cast<float>(divisor),number,number});}
private:
    void close()noexcept{loop_.endHostedSession();runtime_.shutdown();host_.scenes()->setCurrent(nullptr);scene_.reset();scope_.reset();}
    ayt::app::IEngineHost& host_;ayt::game::GameLoop& loop_;
    ayt::app::EngineModuleRuntime runtime_;ayt::app::EngineRuntimeScope scope_;
    std::unique_ptr<ayt::scene::Scene> scene_;DeterministicHostController* controller_=nullptr;
};
void trace(std::ostream& out,const DetSessionCheckpoint& cp,const std::vector<SimEntityId>& ids){
    out<<cp.nextTick<<' '<<detCheckpointHash(cp);
    for(const auto& [id,state]:cp.actors){out<<' '<<id<<' '<<std::hex<<state.pose.position[0]<<' '<<state.pose.position[1]<<std::dec;
        if(std::binary_search(ids.begin(),ids.end(),id)){const auto& view=state.blocks.at(viewSchema);const auto schema=observations();
            const auto normal=readDetState<V>(schema,view,40),own=readDetState<V>(schema,view,50),carry=readDetState<V>(schema,view,60);
            out<<' '<<readDetState<bool>(schema,view,20)<<' '<<readDetState<DetEntityRef>(schema,view,30).value
                <<' '<<std::hex<<normal.x.bits()<<' '<<normal.y.bits()<<' '<<own.x.bits()<<' '<<own.y.bits()
                <<' '<<carry.x.bits()<<' '<<carry.y.bits()<<std::dec;
        }}out<<'\n';
}
std::ofstream freshOutput(const std::string& path){require(!std::filesystem::exists(path),"Output already exists: "+path);
    std::ofstream file(path,std::ios::out|std::ios::binary|std::ios::noreplace);require(static_cast<bool>(file),"Cannot create fresh output: "+path);return file;}
int run(const Options& opts){
    require(std::filesystem::is_regular_file(opts.level) && !std::filesystem::is_symlink(opts.level),"Level must be an ordinary file");
    DetLevel2D loaded;std::string error;require(loadDetLevel2D(opts.level,loaded,error),error);
    const DetLevel2D level=std::move(loaded);const auto ids=characters(level);
    // This example's bounded frame pump uses the authored sample level's1/64 step.
    require(level.stepNumerator==1 && level.stepDenominator==64,"Example frame pump requires a1/64 level step");
    require(level.character.collision.continuousTerrain,"Terrain example requires explicit continuousTerrain profile");
    if(opts.mode==DetHostMode::Record){auto base=opts.replay;if(!std::filesystem::path(base).has_extension())base+=".rpl";
        require(!std::filesystem::exists(base) && !std::filesystem::exists(ayt::replay::FileReplayRecorder::rotationPathFor(base,0)),"Record destination already exists");}
    if(!opts.output.empty())for(const auto* suffix:{".state",".trace",".summary.json"})
        require(!std::filesystem::exists(opts.output+suffix),"Output already exists: "+opts.output+suffix);
    std::ofstream state,traceFile;if(!opts.output.empty()){state=freshOutput(opts.output+".state");traceFile=freshOutput(opts.output+".trace");}
    InputBoundary boundary;const PrePoll prePoll=defaultInput(ids);
    StandardHost host(opts,level,ids,boundary);auto& controller=host.controller();const auto recording=controller.recordingPath();
    if(opts.seekSet)require(controller.seek(opts.seek),controller.error());
    if(controller.state()!=DetHostState::Completed)require(controller.resume(),controller.error());
    const auto initial=controller.session()->nextTick();std::uint64_t frames=0,traceRows=0;
    while(controller.session()->nextTick()<opts.ticks){const auto before=controller.session()->nextTick();
        // Replay never invokes recipe.input and does not acquire live devices.
        if(opts.mode!=DetHostMode::Replay){boundary.tick=before;boundary.samples=prePoll(before);}
        require(++frames<=4*(opts.ticks+1),"Host made no bounded progress");host.frame(opts.frameDivisor,frames);
        require(controller.state()!=DetHostState::Faulted,controller.error());const auto after=controller.session()->nextTick();
        if(after==before){require(controller.state()!=DetHostState::Completed,"Replay ended before requested ticks");continue;}
        require(after==before+1,"Host advanced more than one tick per frame");const auto cp=controller.checkpoint();require(cp.has_value(),controller.error());
        if(traceFile.is_open())trace(traceFile,*cp,ids);++traceRows;
        if(after%64==0){std::cout<<"present tick="<<after;for(const auto id:ids){const auto& xy=cp->actors.at(id).pose.position;
            std::cout<<" actor="<<id<<" xy="<<D::fromBits(xy[0]).toFloat()<<','<<D::fromBits(xy[1]).toFloat();}std::cout<<'\n';}
    }
    const auto cp=controller.checkpoint();require(cp && cp->nextTick==opts.ticks,"Final tick mismatch");
    if(opts.mode==DetHostMode::Replay){require(controller.state()==DetHostState::Completed,"Requested ticks do not reach replay completion");require(boundary.calls==0,"Replay invoked live acquisition");}
    const auto& counts=cp->globals.at(statsSchema);require(counts.size()==2,"Unexpected application feature shape");
    if(opts.requireFeatures)require(counts[0]>0 && counts[1]>0,"Actual static-slope walking and moving-slope carry were not both observed");
    if(opts.mode==DetHostMode::Record)require(controller.stop(),controller.error());
    const auto identity=detLevelSessionConfig(level);std::ostringstream summary;
    summary<<"{\"schema\":1,\"status\":\"passed\",\"scenario\":\"terrain\",\"levelProfile\":1,\"controllerProfile\":7,\"collisionProfile\":8,\"mode\":"
        <<quoted(opts.mode==DetHostMode::Live?"live":opts.mode==DetHostMode::Record?"record":"replay")
        <<",\"initialTick\":"<<initial<<",\"nextTick\":"<<cp->nextTick<<",\"traceRows\":"<<traceRows<<",\"inputCallbacks\":"<<boundary.calls
        <<",\"contentHash\":"<<quoted(std::to_string(identity.contentHash))<<",\"finalHash\":"<<quoted(std::to_string(detCheckpointHash(*cp)))
        <<",\"recordingPath\":"<<quoted(recording)<<",\"featureChecks\":"<<(opts.requireFeatures?"true":"false")
        <<",\"features\":{\"staticSlopeWalkTicks\":"<<counts[0]<<",\"movingSlopeCarryTicks\":"<<counts[1]<<"}}\n";
    if(!opts.output.empty()){const auto bytes=encodeDetCheckpoint(*cp);state.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        state.close();traceFile.close();require(static_cast<bool>(state) && static_cast<bool>(traceFile),"State/trace write failed");
        auto json=freshOutput(opts.output+".summary.json");json<<summary.str();json.close();require(static_cast<bool>(json),"Summary write failed");}
    std::cout<<summary.str();return 0;
}
} // namespace
int main(int argc,char** argv){
    if(argc==2 && std::string_view(argv[1])=="--help"){
        std::cout<<"AYEntity_DetTerrainExample --level terrain.dlt [--ticks 128] [--frame-divisor 64|128]"
            " [--output freshPrefix] [--require-features] [--record fresh.rpl | --replay fresh_000.rpl] [--seek tick]\n";return 0;}
    Options opts;try{opts=options(argc,argv);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    try{return run(opts);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
