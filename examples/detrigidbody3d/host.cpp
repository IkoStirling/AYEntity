#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/EngineRuntimeScope.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/DeterministicHost.h>
#include <AYEntity/DeterministicRigidBody3D.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYGameLoop.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>

namespace {
using namespace ayt::entity;using D=ayt::math::DetFloat32;using F=ayt::math::DetFloat64;using V=ayt::math::DetVec3;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
std::uint64_t integer(const std::string& s){std::uint64_t v=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);require(e==std::errc{}&&p==s.data()+s.size()&&!s.empty(),"invalid integer");return v;}
struct Options{std::string output,replay;DetHostMode mode=DetHostMode::Live;std::uint64_t ticks=64;bool restore=false;};
Options options(int argc,char** argv){Options o;std::set<std::string> seen;for(int i=1;i<argc;++i){std::string key=argv[i];require(seen.insert(key).second,"duplicate option");if(key=="--verify-restore"){o.restore=true;continue;}require(i+1<argc,"missing argument");std::string value=argv[++i];
    if(key=="--output")o.output=value;else if(key=="--ticks")o.ticks=integer(value);else if(key=="--record"||key=="--replay"){require(o.mode==DetHostMode::Live,"multiple modes");o.mode=key=="--record"?DetHostMode::Record:DetHostMode::Replay;o.replay=value;}else throw std::runtime_error("unknown option");}
    require(!o.output.empty()&&o.ticks>=2&&o.ticks<=1024,"fresh output required; ticks2..1024");require(!o.restore||o.mode==DetHostMode::Live,"restore is Live only");return o;}
D ratio(int a,int b){return D::fromInt(a)/D::fromInt(b);}
DetRigidBody3DConfig policy(){DetRigidBody3DConfig c;DetRigidJoint3D j;j.id=1;j.kind=DetJointKind3D::BallSocket;j.first=200;j.second=201;j.anchorSecond=V::fromInts(0,1,0);c.joints={j};return c;}
bool configure(DeterministicSession& s){auto c=policy();if(!installDetRigidBody3D(s,c)||!s.registerTypedSchema({700,1,{{1,F{}}}})||!s.registerGlobalState(700)||!s.registerLogicProfile(780,1,0x5249474944484f31ULL))return false;
    if(!s.registerSystem(4,0,[c](DetTickContext& ctx){auto& commands=ctx.input().commands;if(commands.size()!=1||commands[0].source!=1||commands[0].sequence!=0||commands[0].type!=78||commands[0].payload.size()!=1||commands[0].payload[0]>1)return false;
        if(commands[0].payload[0])detRigidBodyForce3D(ctx,c,201,V::fromInts(4,0,0));ctx.writeGlobal(700,1,ctx.readGlobal<F>(700,1)+F::fromInt(1)/F::fromInt(64));return true;}))return false;
    DetRigidBody3D floor;floor.mode=DetRigidMode3D::Static;floor.mass=D{};floor.half=V::fromInts(8,1,8);
    DetRigidBody3D box;box.half={ratio(1,2),ratio(1,2),ratio(1,2)};box.mass=D::fromInt(2);
    DetRigidBody3D ball;ball.shape=DetRigidShape3D::Sphere;ball.half={ratio(1,2),ratio(1,2),ratio(1,2)};ball.velocity=V::fromInts(-1,0,0);
    DetRigidBody3D anchor;anchor.mode=DetRigidMode3D::Static;anchor.mass=D{};anchor.shape=DetRigidShape3D::Sphere;anchor.half={ratio(1,5),ratio(1,5),ratio(1,5)};anchor.mask=0;
    auto pendulum=ball;pendulum.half={ratio(1,4),ratio(1,4),ratio(1,4)};pendulum.velocity={};pendulum.mask=0;
    return s.addEntity(10,detRigidBodyActor3D(c,box,{D::fromInt(-1),ratio(1,2),D{}}))&&s.addEntity(20,detRigidBodyActor3D(c,ball,V::fromInts(1,2,0)))
       &&s.addEntity(100,detRigidBodyActor3D(c,floor,V::fromInts(0,-1,0)))&&s.addEntity(200,detRigidBodyActor3D(c,anchor,V::fromInts(4,2,0)))&&s.addEntity(201,detRigidBodyActor3D(c,pendulum,V::fromInts(4,1,0)));
}
class Host {
public:
    explicit Host(const Options& o):host_(ayt::app::defaultEngineHost()),loop_(ayt::game::GameLoop::instance()),runtime_(host_),scope_(host_){try{
        loop_.setRenderThreadEnabled(false);loop_.setParallelEnabled(false);loop_.setTimeScale(1);require(loop_.setFixedTimestepRatio(1,64),"fixed step");auto& modules=runtime_.modules();
        require(modules.emplace<EntityComponentModule>().succeeded(),"Core module");require(modules.emplace<EntityRuntimeModule>().succeeded(),"Runtime module");
        DetHostedSceneRecipe recipe;recipe.config={78,1,1,64,0x5249474944484f31ULL};recipe.mode=o.mode;recipe.replayPath=o.replay;recipe.checkpointInterval=16;recipe.configure=configure;
        recipe.input=[](const DetHostInputRequest& request,DetTickInput& packet){packet={request.tick,request.version,{{1,0,78,{static_cast<std::uint8_t>(request.tick<32)}}}};return true;};
        DetHostOptions selection{[recipe](const auto&)->std::optional<DetHostedSceneRecipe>{return recipe;}};require(modules.emplace<DeterministicHostIntegrationModule>(std::move(selection)).succeeded(),"deterministic Host module");
        auto prepared=runtime_.prepare();require(prepared.succeeded(),prepared.message());runtime_.context().componentRegistry().seal();auto installed=runtime_.install();require(installed.succeeded(),installed.message());require(loop_.prepareHostedSession(),"prepare Host");scope_.refresh();
        scene_=std::make_unique<ayt::scene::Scene>(ayt::scene::SceneMode::Play,"det-rigidbody3d-example");host_.scenes()->setCurrent(scene_.get());controller_=deterministicHost(host_);require(controller_&&controller_->session(),"missing Session");require(controller_->state()!=DetHostState::Faulted,controller_->error());
    }catch(...){close();throw;}}
    ~Host(){close();}
    DeterministicHostController& controller(){return *controller_;}
    void frame(){loop_.tickHostedFrame({1.0f/64.0f,frame_,frame_});++frame_;}
private:
    void close()noexcept{loop_.endHostedSession();runtime_.shutdown();host_.scenes()->setCurrent(nullptr);scene_.reset();scope_.reset();}
    ayt::app::IEngineHost& host_;ayt::game::GameLoop& loop_;ayt::app::EngineModuleRuntime runtime_;ayt::app::EngineRuntimeScope scope_;std::unique_ptr<ayt::scene::Scene> scene_;DeterministicHostController* controller_=nullptr;std::uint64_t frame_=0;
};
std::ofstream fresh(const std::string& name){std::ofstream f(name,std::ios::binary|std::ios::out|std::ios::noreplace);require(bool(f),"cannot create fresh output: "+name);return f;}
std::string quote(const std::string& text){std::string out="\"";for(unsigned char c:text){require(c>=32&&c<127,"example metadata path must be printable ASCII");if(c=='\\'||c=='\"')out+='\\';out+=c;}return out+'\"';}
}
int main(int argc,char** argv){try{auto o=options(argc,argv);Host host(o);auto& controller=host.controller();auto trace=fresh(o.output+".trace");std::optional<DetSessionCheckpoint> middle;std::uint64_t advances=0;
    while(controller.checkpoint()->nextTick<o.ticks){auto before=controller.checkpoint()->nextTick;host.frame();require(controller.state()!=DetHostState::Faulted,controller.error());auto cp=controller.checkpoint();require(cp&&cp->nextTick==before+1,"exact Host tick");++advances;
        trace<<cp->nextTick<<' '<<detCheckpointHash(*cp);for(auto id:{10u,20u,201u}){const auto& p=cp->actors.at(id).pose;trace<<' '<<id;for(auto bits:p.position)trace<<' '<<std::hex<<bits;for(auto bits:p.rotation)trace<<' '<<bits;trace<<std::dec;}trace<<'\n';
        if(o.restore&&cp->nextTick==o.ticks/2)middle=*cp;
    }
    auto final=*controller.checkpoint();auto bytes=encodeDetCheckpoint(final);auto recording=controller.recordingPath();bool restored=false;
    if(o.restore){require(middle.has_value(),"missing restore checkpoint");require(controller.restore(*middle),controller.error());require(controller.resume(),controller.error());while(controller.checkpoint()->nextTick<o.ticks){host.frame();require(controller.state()!=DetHostState::Faulted,controller.error());++advances;}
        require(encodeDetCheckpoint(*controller.checkpoint())==bytes,"full 3D/Float64 restore suffix");restored=true;}
    require(F::fromBits(final.globals.at(700)[0])==F::fromUInt(o.ticks)/F::fromInt(64),"Float64 clock hand result");
    if(o.mode==DetHostMode::Replay){controller.pause();require(controller.seek(o.ticks),controller.error());require(encodeDetCheckpoint(*controller.checkpoint())==bytes,"final replay seek");}
    require(controller.stop(),controller.error());trace.flush();require(bool(trace),"trace write");auto state=fresh(o.output+".state");state.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());state.flush();require(bool(state),"state write");
    auto summary=fresh(o.output+".summary.json");summary<<"{\"schema\":1,\"status\":\"passed\",\"ticks\":"<<final.nextTick<<",\"advances\":"<<advances<<",\"manifestVersion\":5,\"finalHash\":\""<<detCheckpointHash(final)<<"\",\"restoreVerified\":"<<(restored?"true":"false")<<",\"recordingPath\":"<<quote(recording)<<"}\n";summary.flush();require(bool(summary),"summary write");
    std::cout<<"PASS rigid3D standard Host ticks="<<final.nextTick<<" advances="<<advances<<" manifest5 Float64 clock; recording="<<recording<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
