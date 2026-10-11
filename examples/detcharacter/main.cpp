// Stage22 public example: module-owned standard Host; no unittest dependencies.
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYGameLoop.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec2;
constexpr std::array<SimEntityId,5> characters{10,11,12,13,14};
constexpr std::uint32_t statsSchema=8,viewSchema=9,inputType=1;
constexpr std::array<const char*,7> featureNames{
    "coyoteJumps","bufferedJumps","drops","crouchTicks",
    "blockedStandTicks","carryTicks","oneWayGroundedTicks"};
void require(bool value,const std::string& message) {
    if(!value)throw std::runtime_error(message);
}
D ratio(int n,int d){return D::fromInt(n)/D::fromInt(d);}
std::uint64_t integer(std::string_view text) {
    std::uint64_t value=0;
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && error==std::errc{} && end==text.data()+text.size(),"Invalid unsigned integer");
    return value;
}
void asciiPath(const std::string& path) {
    require(!path.empty(),"Empty path");
    for(const auto byte:path)require(static_cast<unsigned char>(byte)>=32
        && static_cast<unsigned char>(byte)<127,"This file-adapter example requires printable ASCII paths");
}
std::string quoted(std::string_view value) {
    std::string result="\"";
    for(char c:value){if(c=='\\' || c=='\"')result+='\\';result+=c;}
    return result+'\"';
}
struct Options {
    DetHostMode mode=DetHostMode::Live;
    bool steps=false;
    std::string replayPath,output,commands;
    std::uint64_t ticks=512,seek=0;
    unsigned frameDivisor=64;
    bool seekSet=false;
};
Options options(int argc,char** argv) {
    Options result;
    bool modeSet=false;
    for(int index=1;index<argc;++index) {
        const std::string key=argv[index];
        require(index+1<argc,"Every option requires a value");
        const std::string value=argv[++index];
        if(key=="--record" || key=="--replay") {
            require(!modeSet,"Record and replay are mutually exclusive");modeSet=true;
            result.mode=key=="--record"?DetHostMode::Record:DetHostMode::Replay;
            asciiPath(value);result.replayPath=value;
        }else if(key=="--scenario") {
            require(value=="advanced" || value=="steps","Scenario must be advanced or steps");
            result.steps=value=="steps";
        }else if(key=="--ticks")result.ticks=integer(value);
        else if(key=="--seek"){result.seek=integer(value);result.seekSet=true;}
        else if(key=="--frame-divisor") {
            const auto divisor=integer(value);
            require(divisor==64 || divisor==128,"Frame divisor must be64 or128");
            result.frameDivisor=static_cast<unsigned>(divisor);
        }else if(key=="--output"){asciiPath(value);result.output=value;}
        else if(key=="--commands"){asciiPath(value);result.commands=value;}
        else throw std::runtime_error("Unknown option: "+key);
    }
    require(result.ticks>0 && result.ticks<=65536,"Ticks must be in1..65536");
    require(!result.seekSet || (result.mode==DetHostMode::Replay && result.seek<=result.ticks),"Seek requires replay and seek<=ticks");
    require(result.mode!=DetHostMode::Replay || result.commands.empty(),"Replay consumes recorded commands; do not provide --commands");
    return result;
}
struct Sample {int speed=0;bool jump=false,crouch=false,drop=false;};
using Samples=std::array<Sample,characters.size()>;
class InputScript {
public:
    explicit InputScript(const Options& opts):custom_(!opts.commands.empty()),steps_(opts.steps) {
        if(!custom_)return;
        require(std::filesystem::file_size(opts.commands)<=1048576,"Command script exceeds1MiB");
        std::ifstream file(opts.commands);
        require(static_cast<bool>(file),"Cannot read command script");
        std::string line;std::size_t lineNumber=0,rows=0;
        while(std::getline(file,line)) {
            ++lineNumber;
            require(line.size()<=256,"Command line exceeds256 bytes");
            const auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);
            if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
            std::istringstream fields(line);std::string tickText,actorText,speedText,jumpText,crouchText,dropText,extra;
            require(static_cast<bool>(fields>>tickText>>actorText>>speedText>>jumpText>>crouchText>>dropText)
                && !(fields>>extra),"Expected tick actor speed jump crouch drop at line "+std::to_string(lineNumber));
            const auto tick=integer(tickText),actor=integer(actorText);
            int speed=0;const auto [end,error]=std::from_chars(speedText.data(),speedText.data()+speedText.size(),speed);
            require(error==std::errc{} && end==speedText.data()+speedText.size() && speed>=-2 && speed<=2,"Speed must be an integer in-2..2");
            const auto jump=integer(jumpText),crouch=integer(crouchText),drop=integer(dropText);
            require(tick<opts.ticks && jump<=1 && crouch<=1 && drop<=1,"Command tick/button out of range");
            const auto position=std::find(characters.begin(),characters.end(),actor);
            require(position!=characters.end(),"Unknown character ID");
            require(++rows<=32768,"Command script exceeds32768 rows");
            require(rows_.emplace(std::pair{tick,actor},Sample{speed,jump!=0,crouch!=0,drop!=0}).second,"Duplicate tick/actor command");
        }
        require(file.eof(),"Command script read failed");
    }
    Samples poll(std::uint64_t tick)const {
        Samples result{};
        if(custom_) {
            for(std::size_t index=0;index<characters.size();++index)
                if(const auto found=rows_.find({tick,characters[index]});found!=rows_.end())result[index]=found->second;
            return result; // Every absent row explicitly samples neutral, including released buttons.
        }
        if(steps_) {
            result[0].speed=tick<96?2:0;
            result[1].speed=result[2].speed=tick<128?2:0;
            result[3].speed=tick<128?1:0;
            return result;
        }
        result[0].drop=tick>=32 && tick<=40;
        result[1].speed=tick<80?2:0;result[1].jump=tick>=42 && tick<=48;
        result[2].jump=tick>=18 && tick<=24;
        result[3].speed=tick<256?1:0;result[3].crouch=tick<96;
        return result;
    }
private:
    bool custom_,steps_;
    std::map<std::pair<std::uint64_t,SimEntityId>,Sample> rows_;
};
struct InputBoundary {std::uint64_t tick=0,calls=0;Samples samples{};};
DetCharacterController2DConfig policy() {
    DetCharacterController2DConfig config;
    config.advanced=true;config.collision.oneWayPlatforms=true;
    config.collision.maxBodies=24;config.collision.maxTriggerPairs=4;
    config.maxHorizontalSpeed=D::fromInt(2);config.acceleration=config.deceleration=D::fromInt(128);
    config.gravity=D::fromInt(16);config.jumpSpeed=D::fromInt(4);
    config.coyoteTicks=8;config.jumpBufferTicks=8;config.dropThroughTicks=8;config.crouchEnabled=true;
    return config;
}
DetCharacterController2DConfig stepPolicy() {
    auto config=policy();
    config.collision.stepUp=true;config.collision.maxStepHeight=ratio(1,2);
    config.collision.maxTriggerPairs=59;
    return config;
}
DetTypedStateSchema observations(bool steps=false) {
    DetTypedStateSchema schema{viewSchema,1,{{10,false},{20,D{}},{30,std::uint32_t{0}},{40,std::uint32_t{0}},
        {50,false},{60,false},{70,DetEntityRef{}},{80,DetEntityRef{}},
        {90,std::uint32_t{0}},{100,std::uint32_t{0}},{110,std::uint32_t{0}}}};
    if(steps){schema.version=2;schema.fields.push_back({120,false});schema.fields.push_back({130,std::uint64_t{0}});}
    return schema;
}
DetActorState actor(const DetCharacterController2DConfig& config,DetBodyMode2D mode,V position,V half,
        V velocity={},bool character=false,bool oneWay=false) {
    DetActorState state;DetSimTransformComponent pose;
    require(pose.setPosition({position.x,position.y,{}}),"Invalid example pose");state.pose=pose.snapshot();
    DetCollisionBody2D body;body.mode=mode;body.half=half;body.velocity=velocity;
    body.layer=character?1:2;body.mask=character?2:1;body.oneWay=oneWay;
    state.blocks[config.collision.bodySchema]=detCollisionBodyState2D(config.collision,body);
    if(character){DetCharacterState2D movement;movement.enabled=true;
        state.blocks[config.stateSchema]=detCharacterState2D(config,movement);}
    return state;
}
bool configure(DeterministicSession& session) {
    const auto config=policy();
    DetTypedStateSchema stats{statsSchema,1,{}};
    for(std::uint32_t field=10;field<=70;field+=10)stats.fields.push_back({field,std::uint64_t{0}});
    if(!session.registerTypedSchema(stats) || !session.registerGlobalState(statsSchema)
        || !session.registerTypedSchema(observations())
        || !session.registerLogicProfile(60,1,0x4443433245583031ULL))return false;
    if(!session.registerSystem(10,0,[config](DetTickContext& context) {
        if(context.input().commands.size()!=characters.size())return false;
        for(std::size_t index=0;index<characters.size();++index) {
            const auto id=characters[index];const auto before=detCharacterState2D(context,id,config);
            context.write(id,viewSchema,10,before.grounded);context.write(id,viewSchema,20,before.velocity.y);
            context.write(id,viewSchema,30,before.coyoteRemaining);context.write(id,viewSchema,40,before.jumpBufferRemaining);
            const auto& command=context.input().commands[index];
            if(command.source!=id || command.sequence!=0 || command.type!=inputType || command.payload.size()!=4
                || command.payload[0]>4 || command.payload[1]>1 || command.payload[2]>1 || command.payload[3]>1)return false;
            detCharacterInput2D(context,id,config,{D::fromInt(static_cast<int>(command.payload[0])-2),
                command.payload[1]!=0,command.payload[2]!=0,command.payload[3]!=0});
        }
        return true;
    }) || !installDetCharacterController2D(session,config)
        || !session.registerSystem(50,0,[config](DetTickContext& context) {
            const auto count=[&](std::uint32_t field){context.writeGlobal(statsSchema,field,
                context.readGlobal<std::uint64_t>(statsSchema,field)+1);};
            for(const auto id:characters) {
                const auto movement=detCharacterState2D(context,id,config);
                const bool upwardTransition=movement.velocity.y>D{} && context.read<D>(id,viewSchema,20)<=D{};
                if(id==11 && upwardTransition && !context.read<bool>(id,viewSchema,10)
                    && context.read<std::uint32_t>(id,viewSchema,30)>0)count(10);
                if(id==12 && upwardTransition && context.read<std::uint32_t>(id,viewSchema,40)>0)count(20);
                const auto ignored=context.read<DetEntityRef>(id,config.collision.bodySchema,DetBodyIgnoredOneWay);
                if(ignored.value && !context.read<DetEntityRef>(id,viewSchema,80).value)count(30);
                if(movement.crouched)count(40);
                if(movement.crouched && !movement.crouchHeld)count(50);
                if(movement.grounded && movement.supportVelocity.x!=D{})count(60);
                if(id==10 && movement.grounded && movement.support.value==100)count(70);
                context.write(id,viewSchema,50,movement.grounded);context.write(id,viewSchema,60,movement.crouched);
                context.write(id,viewSchema,70,movement.support);context.write(id,viewSchema,80,ignored);
                context.write(id,viewSchema,90,movement.coyoteRemaining);
                context.write(id,viewSchema,100,movement.jumpBufferRemaining);
                context.write(id,viewSchema,110,movement.dropRemaining);
            }
            return true;
        }))return false;
    const auto quarter=ratio(1,4),half=ratio(1,2),threeQuarters=ratio(3,4);
    for(std::size_t index=0;index<characters.size();++index) {
        const auto y=index==2?ratio(7,4):threeQuarters;
        if(!session.addEntity(characters[index],actor(config,DetBodyMode2D::Kinematic,
            {D::fromInt(static_cast<int>(index)*10),y},{quarter,half},{},true)))return false;
    }
    const auto add=[&](SimEntityId id,int x,int y,int width,bool oneWay=false,V velocity=V{}) {
        return session.addEntity(id,actor(config,velocity==V{}?DetBodyMode2D::Static:DetBodyMode2D::MovingObstacle,
            V::fromInts(x,y),{D::fromInt(width),quarter},velocity,false,oneWay));};
    return add(100,0,0,3,true) && add(101,0,-3,4) && add(110,10,0,1)
        && add(111,13,-3,4) && add(120,20,0,2) && add(130,30,0,8)
        && session.addEntity(131,actor(config,DetBodyMode2D::Static,{D::fromInt(32),ratio(9,8)},
            {D::fromInt(1),quarter})) && add(140,40,0,2,false,{half,{}});
}
bool configureSteps(DeterministicSession& session) {
    const auto config=stepPolicy();
    DetTypedStateSchema stats{statsSchema,2,{}};
    for(std::uint32_t field=10;field<=80;field+=10)stats.fields.push_back({field,std::uint64_t{0}});
    if(!session.registerTypedSchema(stats) || !session.registerGlobalState(statsSchema)
        || !session.registerTypedSchema(observations(true))
        || !session.registerLogicProfile(60,2,0x4443535445503031ULL))return false;
    if(!session.registerSystem(10,0,[config](DetTickContext& context) {
        if(context.input().commands.size()!=characters.size())return false;
        for(std::size_t index=0;index<characters.size();++index) {
            const auto& command=context.input().commands[index];
            if(command.source!=characters[index] || command.sequence!=0 || command.type!=inputType
                || command.payload.size()!=4 || command.payload[0]>4 || command.payload[1]>1
                || command.payload[2]>1 || command.payload[3]>1)return false;
            detCharacterInput2D(context,characters[index],config,{D::fromInt(static_cast<int>(command.payload[0])-2),
                command.payload[1]!=0,command.payload[2]!=0,command.payload[3]!=0});
        }
        return true;
    }) || !installDetCharacterController2D(session,config)
        || !session.registerSystem(50,0,[config](DetTickContext& context) {
            for(const auto id:characters) {
                const auto movement=detCharacterState2D(context,id,config);
                if(movement.stepped) {
                    context.writeGlobal(statsSchema,80,context.readGlobal<std::uint64_t>(statsSchema,80)+1);
                    context.write(id,viewSchema,130,context.read<std::uint64_t>(id,viewSchema,130)+1);
                }
                context.write(id,viewSchema,50,movement.grounded);
                context.write(id,viewSchema,60,movement.crouched);
                context.write(id,viewSchema,70,movement.support);
                context.write(id,viewSchema,80,context.read<DetEntityRef>(id,config.collision.bodySchema,DetBodyIgnoredOneWay));
                context.write(id,viewSchema,120,movement.stepped);
            }
            return true;
        }))return false;
    const auto quarter=ratio(1,4),half=ratio(1,2);
    for(std::size_t index=0;index<characters.size();++index) {
        const auto x=D::fromInt(static_cast<int>(index)*10);
        if(!session.addEntity(characters[index],actor(config,DetBodyMode2D::Kinematic,
            {x,ratio(3,4)},{quarter,half},{},true)))return false;
        if(!session.addEntity(100+index*10,actor(config,DetBodyMode2D::Static,
            {x,D{}},{D::fromInt(4),quarter})))return false;
    }
    // All dimensions are dyadic. These bodies are ordinary solid static boxes;
    // authoring positions is initialization only, never a second tick motion writer.
    for(int level=1;level<=3;++level) {
        const auto top=quarter+D::fromInt(level)*quarter;
        if(!session.addEntity(101+level,actor(config,DetBodyMode2D::Static,
            {D::fromInt(level),(top-quarter)*half},{half,(top+quarter)*half})))return false;
    }
    return session.addEntity(111,actor(config,DetBodyMode2D::Static,{D::fromInt(11),ratio(3,8)},
            {half,ratio(5,8)}))
        && session.addEntity(121,actor(config,DetBodyMode2D::Static,{D::fromInt(21),ratio(1,8)},
            {half,ratio(3,8)}))
        && session.addEntity(122,actor(config,DetBodyMode2D::Static,{D::fromInt(21),ratio(3,2)},
            {D::fromInt(2),ratio(1,8)}));
}
class StandardHost {
public:
    StandardHost(const Options& opts,InputBoundary& boundary):host(ayt::app::defaultEngineHost()),
        loop(ayt::game::GameLoop::instance()),runtime(host),scope(host) {
        try {
            loop.setRenderThreadEnabled(false);loop.setParallelEnabled(false);loop.setTimeScale(1);
            require(loop.setFixedTimestepRatio(1,64),"Host rejected rational fixed step");
            auto& modules=runtime.modules();
            require(modules.emplace<EntityComponentModule>().succeeded(),"Add component module");
            require(modules.emplace<EntityRuntimeModule>().succeeded(),"Add Entity runtime module");
            DetHostedSceneRecipe recipe;recipe.config.applicationVersion=opts.steps?26:22;
            recipe.config.stepNumerator=1;recipe.config.stepDenominator=64;
            recipe.config.contentHash=opts.steps?0x4443535445503031ULL:0x444343324c564c31ULL;
            recipe.mode=opts.mode;recipe.replayPath=opts.replayPath;recipe.checkpointInterval=64;
            recipe.configure=opts.steps?configureSteps:configure;
            recipe.input=[&boundary](const auto& request,auto& packet) {
                if(request.tick!=boundary.tick)return false;++boundary.calls;
                packet={request.tick,request.version,{}};
                for(std::size_t index=0;index<characters.size();++index) {
                    const auto input=boundary.samples[index];
                    packet.commands.push_back({static_cast<std::uint32_t>(characters[index]),0,inputType,
                        {static_cast<std::uint8_t>(input.speed+2),static_cast<std::uint8_t>(input.jump),
                        static_cast<std::uint8_t>(input.crouch),static_cast<std::uint8_t>(input.drop)}});
                }
                return true;
            };
            DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};
            require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"Add deterministic Host module");
            const auto prepared=runtime.prepare();require(prepared.succeeded(),prepared.message());
            runtime.context().componentRegistry().seal();
            const auto installed=runtime.install();require(installed.succeeded(),installed.message());
            require(loop.prepareHostedSession(),"Standard Host preparation failed");scope.refresh();
            scene=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-character-example");
            host.scenes()->setCurrent(scene.get());
            controller=deterministicHost(host);require(controller && controller->session(),"Missing module-owned deterministic Host session");
            require(controller->state()!=DetHostState::Faulted,controller->error());
        }catch(...){close();throw;}
    }
    ~StandardHost(){close();}
    void frame(unsigned divisor,std::uint64_t number) {
        loop.tickHostedFrame({1.0f/static_cast<float>(divisor),number,number});
    }
    ayt::app::IEngineHost& host;
    ayt::game::GameLoop& loop;
    DeterministicHostController* controller=nullptr;
private:
    void close()noexcept {
        loop.endHostedSession();runtime.shutdown();
        host.scenes()->setCurrent(nullptr);scene.reset();scope.reset();
    }
    ayt::app::EngineModuleRuntime runtime;
    ayt::app::EngineRuntimeScope scope;
    std::unique_ptr<ayt::scene::Scene> scene;
};
std::array<std::uint64_t,7> features(const DetSessionCheckpoint& checkpoint,bool steps=false) {
    std::array<std::uint64_t,7> result{};
    const auto& words=checkpoint.globals.at(statsSchema);
    require(words.size()==result.size()+(steps?1:0),"Unexpected application stats shape");
    std::copy_n(words.begin(),result.size(),result.begin());return result;
}
void trace(std::ostream& stream,const DetSessionCheckpoint& checkpoint,bool steps=false) {
    stream<<checkpoint.nextTick<<' '<<detCheckpointHash(checkpoint);
    const auto schema=observations(steps);
    for(const auto id:characters) {
        const auto& actor=checkpoint.actors.at(id);const auto& view=actor.blocks.at(viewSchema);
        stream<<' '<<id<<' '<<std::hex<<actor.pose.position[0]<<' '<<actor.pose.position[1]<<std::dec
            <<' '<<readDetState<bool>(schema,view,50)<<' '<<readDetState<bool>(schema,view,60)
            <<' '<<readDetState<DetEntityRef>(schema,view,70).value
            <<' '<<readDetState<DetEntityRef>(schema,view,80).value;
        if(steps)stream<<' '<<readDetState<bool>(schema,view,120);
    }
    stream<<'\n';
}
void presentation(const DetSessionCheckpoint& checkpoint,bool steps=false) {
    // Copies from a const committed checkpoint. Native floats and stdout are presentation only.
    std::cout<<"present tick="<<checkpoint.nextTick;
    for(const auto id:characters) {
        const auto& position=checkpoint.actors.at(id).pose.position;
        std::cout<<" actor="<<id<<" xy="<<D::fromBits(position[0]).toFloat()<<','<<D::fromBits(position[1]).toFloat();
        if(steps)std::cout<<" stepped="<<readDetState<bool>(observations(true),checkpoint.actors.at(id).blocks.at(viewSchema),120)
            <<" stepCount="<<readDetState<std::uint64_t>(observations(true),checkpoint.actors.at(id).blocks.at(viewSchema),130);
    }
    std::cout<<'\n';
}
std::ofstream freshOutput(const std::string& path,bool binary=false) {
    require(!std::filesystem::exists(path),"Output already exists: "+path);
    std::ofstream stream(path,std::ios::out|std::ios::noreplace|(binary?std::ios::binary:std::ios::openmode{}));
    require(static_cast<bool>(stream),"Cannot create fresh output: "+path);return stream;
}
int run(const Options& opts) {
    if(opts.mode==DetHostMode::Record) {
        auto base=opts.replayPath;if(!std::filesystem::path(base).has_extension())base+=".rpl";
        require(!std::filesystem::exists(base) && !std::filesystem::exists(ayt::replay::FileReplayRecorder::rotationPathFor(base,0)),"Record destination already exists");
    }
    if(!opts.output.empty())for(const auto* suffix:{".state",".trace",".summary.json"})
        require(!std::filesystem::exists(opts.output+suffix),"Output already exists: "+opts.output+suffix);
    const InputScript script(opts);InputBoundary boundary;
    std::ofstream state,traceFile;
    if(!opts.output.empty()){state=freshOutput(opts.output+".state",true);traceFile=freshOutput(opts.output+".trace");}
    StandardHost host(opts,boundary);auto& controller=*host.controller;
    const auto recording=controller.recordingPath();
    if(opts.seekSet)require(controller.seek(opts.seek),controller.error());
    if(controller.state()!=DetHostState::Completed)require(controller.resume(),controller.error());
    const auto initial=controller.session()->nextTick();
    std::uint64_t frames=0,traceRows=0;
    while(controller.session()->nextTick()<opts.ticks) {
        const auto before=controller.session()->nextTick();
        boundary.tick=before;boundary.samples=script.poll(before); // Poll application input before the Host frame.
        require(++frames<=4*(opts.ticks+1),"Host made no bounded progress");host.frame(opts.frameDivisor,frames);
        require(controller.state()!=DetHostState::Faulted,controller.error());
        const auto after=controller.session()->nextTick();
        if(after==before){require(controller.state()!=DetHostState::Completed,"Replay ended before requested ticks");continue;}
        require(after==before+1,"Host advanced more than one tick per frame");
        const auto checkpoint=controller.checkpoint();require(checkpoint.has_value(),controller.error());
        if(traceFile.is_open())trace(traceFile,*checkpoint,opts.steps);++traceRows;
        if(after%64==0)presentation(*checkpoint,opts.steps);
    }
    const auto checkpoint=controller.checkpoint();require(checkpoint && checkpoint->nextTick==opts.ticks,"Final tick mismatch");
    if(opts.mode==DetHostMode::Replay)require(controller.state()==DetHostState::Completed,"Requested ticks do not reach replay completion");
    const auto counts=features(*checkpoint,opts.steps);
    // Replay may have been recorded with a custom script; it must not assume built-in inputs.
    const bool featureCheck=!opts.steps && opts.mode!=DetHostMode::Replay && opts.commands.empty() && opts.ticks>=512;
    if(featureCheck)for(std::size_t index=0;index<counts.size();++index)
        require(counts[index]>0,std::string("Built-in feature was not observed: ")+featureNames[index]);
    const auto stepCount=opts.steps?checkpoint->globals.at(statsSchema).at(7):0;
    const bool stepCheck=opts.steps && opts.mode!=DetHostMode::Replay && opts.commands.empty() && opts.ticks>=128;
    if(stepCheck) {
        require(readDetState<std::uint64_t>(observations(true),checkpoint->actors.at(characters[0]).blocks.at(viewSchema),130)>=3,
            "Built-in stairs did not observe three committed character10 steps");
        require(checkpoint->actors.at(characters[0]).pose.position[1]==ratio(3,2).bits(),
            "Built-in stairs did not settle on the third static top");
    }
    if(opts.mode==DetHostMode::Replay)require(boundary.calls==0,"Replay invoked live input acquisition");
    if(opts.mode==DetHostMode::Record)require(controller.stop(),controller.error());
    const auto hash=detCheckpointHash(*checkpoint);
    std::ostringstream summary;
    summary<<"{\"schema\":1,\"status\":\"passed\",\"mode\":"
        <<quoted(opts.mode==DetHostMode::Live?"live":opts.mode==DetHostMode::Record?"record":"replay")
        <<",\"initialTick\":"<<initial<<",\"nextTick\":"<<checkpoint->nextTick<<",\"traceRows\":"<<traceRows
        <<",\"inputCallbacks\":"<<boundary.calls<<",\"finalHash\":"<<quoted(std::to_string(hash))
        <<",\"recordingPath\":"<<quoted(recording)<<",\"builtInFeatureChecks\":"<<(featureCheck?"true":"false")<<",\"features\":{";
    for(std::size_t index=0;index<counts.size();++index){if(index)summary<<',';summary<<quoted(featureNames[index])<<':'<<counts[index];}
    summary<<'}';
    if(opts.steps) {
        summary<<",\"scenario\":\"steps\",\"controllerProfile\":3,\"collisionProfile\":4,\"steppedTicks\":"
            <<stepCount<<",\"builtInStepChecks\":"<<(stepCheck?"true":"false")<<",\"characterStepCounts\":{";
        for(std::size_t index=0;index<characters.size();++index) {
            if(index)summary<<',';
            summary<<quoted(std::to_string(characters[index]))<<':'
                <<readDetState<std::uint64_t>(observations(true),checkpoint->actors.at(characters[index]).blocks.at(viewSchema),130);
        }
        summary<<'}';
    }
    summary<<"}\n";
    if(!opts.output.empty()) {
        const auto bytes=encodeDetCheckpoint(*checkpoint);
        state.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        state.close();traceFile.close();require(static_cast<bool>(state) && static_cast<bool>(traceFile),"Write state/trace failed");
        auto json=freshOutput(opts.output+".summary.json");json<<summary.str();json.close();require(static_cast<bool>(json),"Write summary failed");
    }
    std::cout<<summary.str();return 0;
}
} // namespace
int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"AYEntity_DetCharacterExample [--record fresh.rpl | --replay recorded_000.rpl] [--seek tick]"
            " [--scenario advanced|steps] [--ticks 512] [--frame-divisor 64|128] [--commands file] [--output freshPrefix]\n";return 0;
    }
    Options opts;
    try{opts=options(argc,argv);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    try{return run(opts);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
