// design reference: AYEntity/design.md Stage58 bounded registered line-of-sight decisions.
// Selected dyadic geometry/decision rows are hand expectations. Whole checkpoint
// equality below compares real execution paths; it is not an independent CP golden.
#include <AYEntity/DeterministicBrain2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec2;
using Bytes=std::vector<std::uint8_t>;
using M=DetBrainMode2D;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why.empty()?"Unspecified fixture setup/execution failure":why);}
D q(int value,int denominator=4){return D::fromInt(value)/D::fromInt(denominator);}
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
std::uint64_t advances=0,handRows=0,geometryRows=0,cpComparisons=0,negativeRestores=0,negativeConfigs=0;
std::ofstream trace;
DetSessionConfig sessionConfig(){return {58,1,1,4,58};}
DetSessionCheckpoint checkpoint(DeterministicSession& session){const auto value=session.checkpoint();require(bool(value),session.error());return *value;}
void saveBytes(const fs::path& path,const Bytes& value){std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(value.data()),static_cast<std::streamsize>(value.size()));out.close();require(bool(out),"Write "+path.filename().string());}
void advance(DeterministicSession& session,const DetTickInput& input,const std::string& phase){
    witness={phase,checkpoint(session),{},{},input};const bool moved=session.advance(input);witness.actual=session.checkpoint();require(moved,session.error());++advances;}
void equalCP(DeterministicSession& session,const DetSessionCheckpoint& expected,const std::string& phase){
    witness={phase,checkpoint(session),checkpoint(session),expected,{}};
    require(encodeDetCheckpoint(*witness.actual)==encodeDetCheckpoint(expected),phase);++cpComparisons;}
DetTickInput frame(std::uint64_t tick,bool clear=false){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(clear)}},{1,1,99,{58}}}};}

DetBrain2DConfig policy(bool sight=true,bool slope=false){DetBrain2DConfig c;c.lineOfSight=sight;
    auto& character=c.character;character.advanced=true;character.collision.oneWayPlatforms=true;
    character.collision.staticSlopes=slope;character.collision.maxTriggerPairs=slope?58:32;
    character.maxHorizontalSpeed=D::fromInt(4);character.acceleration=character.deceleration=D::fromInt(128);
    character.gravity=character.jumpSpeed=D{};
    auto& vitality=c.attack.vitality;vitality.progress.stateSchema=176;vitality.progress.policySchemaBase=177;
    vitality.progress.validatorId=vitality.progress.logicProfileId=76;
    vitality.progress.actions.collision=character.collision;vitality.progress.actions.targets.clear();
    vitality.progress.counters={{1,0,20,20},{2,0,20,20}};vitality.actors={{10,1,0},{20,2,0}};
    vitality.permissions={{1,20,DetVitalityKind2D::Hurt,3}};
    c.attack.owners={{10,{q(1,16),q(1,16)},{D::fromInt(1),D{}},1,1,1,{{20,1,3}}}};
    DetBrainOwner2D owner;owner.actor=10;owner.target=20;owner.patrolLeft=D::fromInt(-2);owner.patrolRight=D::fromInt(2);
    owner.patrolSpeed=D::fromInt(1);owner.chaseSpeed=D::fromInt(2);owner.senseHalfExtent={D::fromInt(4),D::fromInt(2)};
    owner.attackDistance=D::fromInt(1);owner.initialRight=false;c.owners={owner};return c;}
enum class Obstacle {None,Wall,Trigger,Wedge};
struct Scene {V owner{D{},q(1)},target{D::fromInt(2),q(1)};Obstacle obstacle=Obstacle::Wall;std::uint32_t clearSystem=2;};
DetActorState actor(const DetBrain2DConfig& c,const Scene& scene,SimEntityId id){DetActorState result;V center,half;
    DetCollisionBody2D body;body.mode=id<30?DetBodyMode2D::Kinematic:DetBodyMode2D::Static;
    if(id==10){center=scene.owner;half=c.character.collision.staticSlopes?V{q(1,16),q(1,16)}:V{q(1),q(1)};body.layer=1;body.mask=6;}
    else if(id==20){center=scene.target;half=c.character.collision.staticSlopes?V{q(1,16),q(1,16)}:V{q(1),q(1)};body.layer=2;body.mask=1;}
    else if(id==30){body.layer=4;body.mask=1;
        if(scene.obstacle==Obstacle::Wedge){center={D::fromInt(1),q(2)};half={q(2),q(2)};body.shape=DetCollisionShape2D::SlopeRisesRight;}
        else {center={D::fromInt(1),q(1)};half={q(1),q(1)};body.trigger=scene.obstacle==Obstacle::Trigger;}}
    else throw std::invalid_argument("Unknown LOS fixture actor");
    body.half=half;DetSimTransformComponent pose;require(pose.setPosition({center.x,center.y,D{}}),"Author dyadic fixture pose");result.pose=pose.snapshot();
    result.blocks[c.character.collision.bodySchema]=detCollisionBodyState2D(c.character.collision,body);
    if(id<30){DetCharacterState2D state;state.enabled=true;result.blocks[c.character.stateSchema]=detCharacterState2D(c.character,state);}return result;}
bool configure(DeterministicSession& session,const DetBrain2DConfig& c,Scene scene={}){
    witness={"configuration",{},{},{},{}};
    if(!installDetActions2D(session,c.attack.vitality.progress.actions) || !installDetProgress2D(session,c.attack.vitality.progress)
        || !installDetCharacterController2D(session,c.character) || !installDetVitality2D(session,c.attack.vitality)
        || !installDetAttack2D(session,c.attack) || !installDetBrain2D(session,c))return false;
    if(!session.registerSystem(scene.clearSystem,c.character.collision.priority,[c,scene](DetTickContext& ctx){
        for(const auto& command:ctx.input().commands){if(command.source!=1)return false;
            if(command.type==1 && command.payload.size()==1 && command.payload[0]<=1){
                if(command.payload[0]){if(scene.obstacle==Obstacle::None)return false;ctx.write(30,c.character.collision.bodySchema,DetBodyMask,std::uint32_t{});}}
            else if(command.type==99 && command.payload==Bytes{58})continue;else return false;}return true;}))return false;
    for(auto id:{SimEntityId{10},SimEntityId{20}})if(!session.addEntity(id,actor(c,scene,id)))return false;
    if(scene.obstacle!=Obstacle::None && !session.addEntity(30,actor(c,scene,30)))return false;
    return session.seal();}
void setup(DeterministicSession& session,const DetBrain2DConfig& c,Scene scene={}){const bool configured=configure(session,c,scene);require(configured,session.error());}
D x(const DetSessionCheckpoint& cp,SimEntityId id){return D::fromBits(cp.actors.at(id).pose.position[0]);}
void hand(DeterministicSession& session,const DetBrain2DConfig& c,const std::string& name,std::uint64_t tick,M mode,D speed,D position,V observed){
    const auto cp=checkpoint(session);const auto state=detBrainState2D(cp,c,10);
    const bool pulse=mode==M::AttackRequested;
    require(state.mode==mode && state.desiredSpeed.bits()==speed.bits() && state.desiredAttackHeld==pulse,name+": selected hand mode/speed/held");
    require(state.decisionCount==tick+1 && state.lastDecisionTick==tick && state.observedAttackProcessedTick==tick
        && state.observedOwnerAlive && state.observedTargetPresent && state.observedTargetAlive,name+": processed/alive observations");
    require(state.observedOwnerPosition.x.bits()==observed.x.bits() && state.observedOwnerPosition.y.bits()==observed.y.bits()
        && x(cp,10).bits()==position.bits(),name+": pre-motion center and actual dyadic movement");
    require(detVitalityState2D(cp,c.attack.vitality,10).hp==20 && detVitalityState2D(cp,c.attack.vitality,20).hp==20,
        name+": LOS does not write HP or fabricate a hit");
    trace<<"{\"case\":\""<<name<<"\",\"tick\":\""<<tick<<"\",\"mode\":"<<static_cast<std::uint32_t>(state.mode)
        <<",\"speedBits\":"<<state.desiredSpeed.bits()<<",\"positionXBits\":"<<x(cp,10).bits()
        <<",\"observedXBits\":"<<state.observedOwnerPosition.x.bits()<<",\"observedYBits\":"<<state.observedOwnerPosition.y.bits()
        <<",\"stateHash\":\""<<detCheckpointHash(cp)<<"\"}\n";
    require(bool(trace),"Write hand trace");++handRows;}
void restore(DeterministicSession& session,const DetSessionCheckpoint& cp,const std::string& phase){
    witness={phase,checkpoint(session),{},cp,{}};const bool accepted=session.restore(cp);witness.actual=session.checkpoint();require(accepted,session.error());equalCP(session,cp,phase);}
void reject(DeterministicSession& session,const DetSessionCheckpoint& bad,const std::string& phase){
    const auto before=checkpoint(session);witness={phase,before,{},{},{}};const bool accepted=session.restore(bad);witness.actual=session.checkpoint();
    require(!accepted && encodeDetCheckpoint(checkpoint(session))==encodeDetCheckpoint(before),phase+": malformed restore is atomic");++negativeRestores;}
void corruptFiniteLane(const DetTypedStateSchema& schema,std::vector<std::uint64_t>& words,std::uint32_t id){
    std::size_t offset=0;for(const auto& member:schema.fields){if(member.id==id){require(detStateType(member.initial)==DetStateType::Vec2,"Finite corruption targets real Vec2 field");
            words.at(offset)=0x7fc00000u;return;}offset+=encodeDetStateValue(member.initial).size();}throw std::runtime_error("Missing finite observation field");}

void wallAndRestore(const fs::path& output){const auto c=policy();DeterministicSession base(sessionConfig());setup(base,c);
    std::vector<DetSessionCheckpoint> rows{checkpoint(base)};
    // At T0 the closed ray crosses wall X[3/4,5/4], so patrol left moves -1/4.
    // System2 clears its mask at T1 before Brain3: chase2 then moves +1/2 each step.
    static const std::array<int,4> quarters{-1,1,3,5};
    for(unsigned t=0;t<4;++t){advance(base,frame(t,t==1),"wall-before-brain");
        hand(base,c,"wall-before-brain",t,t?M::Chase:M::SightBlocked,D::fromInt(t?2:-1),q(quarters[t]),{t?q(quarters[t-1]):D{},q(1)});
        rows.push_back(checkpoint(base));}
    DeterministicSession restored(sessionConfig());setup(restored,c);restore(restored,rows[1],"wall-restore-CP1");
    for(unsigned t=1;t<4;++t){advance(restored,frame(t,t==1),"wall-restored-continuation");equalCP(restored,rows[t+1],"Whole actual CP after restored LOS continuation");}
    saveBytes(output/"initial.state",encodeDetCheckpoint(rows.front()));saveBytes(output/"blocked.state",encodeDetCheckpoint(rows[1]));
    saveBytes(output/"final.state",encodeDetCheckpoint(rows.back()));
    std::ofstream hashes(output/"tick-hashes.txt",std::ios::binary);for(const auto& cp:rows)hashes<<cp.nextTick<<' '<<detCheckpointHash(cp)<<'\n';
    hashes.close();require(bool(hashes),"Write finite execution hash trace");
    std::ofstream inputs(output/"canonical-inputs.bin",std::ios::binary);for(unsigned t=0;t<4;++t){const auto value=encodeDetInput(frame(t,t==1));
        const auto size=static_cast<std::uint32_t>(value.size());for(unsigned n=0;n<4;++n)inputs.put(static_cast<char>(size>>(8*n)));
        inputs.write(reinterpret_cast<const char*>(value.data()),static_cast<std::streamsize>(value.size()));}
    inputs.close();require(bool(inputs),"Write original four framed inputs");
    const auto schema=detBrainStateSchema2D(c);const auto field=[&](auto member){return detBrainField2D(c,10,member);};
    for(unsigned n=0;n<3;++n){auto bad=rows[1];if(n==0)writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainMode),std::uint32_t{8});
        else if(n==1)writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainDesiredSpeed),D::fromInt(1));
        else corruptFiniteLane(schema,bad.globals.at(c.stateSchema),field(DetBrainObservedOwnerPosition));
        reject(restored,bad,"reject-recorded-mode-speed-or-nonfinite-observation-"+std::to_string(n));}
    auto legacy=c;legacy.lineOfSight=false;DeterministicSession old(sessionConfig());setup(old,legacy);
    require(detBrainStateSchema2D(legacy).version==1 && detBrainStateSchema2D(c).version==2
        && encodeDetBrainConfig2D(c)!=encodeDetBrainConfig2D(legacy),"Explicit LOS changes selected full identity");
    reject(old,rows[1],"foreign-LOS-profile-restore");
    // This is a current default/profile-selection contrast, not a historical wire golden.
    advance(old,frame(0),"default-profile1-wall");hand(old,legacy,"default-profile1-wall",0,M::Chase,D::fromInt(2),q(2),{D{},q(1)});
    std::cout<<"PASS real wall/mask ordering, four-tick restore continuation and profile selection\n";}

void postBrainMask(){const auto c=policy();Scene scene;scene.clearSystem=4;DeterministicSession base(sessionConfig());setup(base,c,scene);
    advance(base,frame(0,true),"wall-mask-after-brain");hand(base,c,"wall-mask-after-brain",0,M::SightBlocked,D::fromInt(-1),-q(1),scene.owner);
    const auto captured=checkpoint(base);require(readDetState<std::uint32_t>(detCollisionBodySchema2D(c.character.collision),
        captured.actors.at(30).blocks.at(c.character.collision.bodySchema),DetBodyMask)==0,"CP already contains the later wall mask write");
    // The ray at this CP is now clear. Its stored pre-Brain observation is still
    // SightBlocked and must restore without replacing history with a new query.
    const auto hits=DetCollisionQuery2D(captured,c.character.collision).raycast({-q(1),q(1)},{q(9),D{}},{1,6,10,false});
    require(std::none_of(hits.begin(),hits.end(),[](const auto& hit){return hit.entity==30;}),"Later CP geometry has no masked wall hit");++geometryRows;
    DeterministicSession restored(sessionConfig());setup(restored,c,scene);restore(restored,captured,"restore-recorded-blocked-after-later-mask-write");
    advance(base,frame(1),"wall-next-decision");hand(base,c,"wall-next-decision",1,M::Chase,D::fromInt(2),q(1),{-q(1),q(1)});
    advance(restored,frame(1),"wall-next-decision-restored");equalCP(restored,checkpoint(base),"Later clear decision after historical blocked restore");
    std::cout<<"PASS later mask write does not rewrite the recorded LOS observation\n";}

void excludedProxies(){const auto c=policy();for(const auto obstacle:{Obstacle::None,Obstacle::Trigger}){Scene scene;scene.obstacle=obstacle;
    DeterministicSession session(sessionConfig());setup(session,c,scene);const auto initial=checkpoint(session);
    witness={obstacle==Obstacle::None?"target-ray-geometry":"trigger-ray-geometry",initial,initial,{},{}};
    const auto hits=DetCollisionQuery2D(initial,c.character.collision).raycast(scene.owner,scene.target-scene.owner,{1,6,10,false});
    require(hits.size()==1 && hits.front().entity==20,"Own ID and triggers excluded; the actual target is still a closed geometric ray hit");++geometryRows;
    const std::string name=obstacle==Obstacle::None?"target-self-hit-skipped":"trigger-does-not-block";
    advance(session,frame(0),name);hand(session,c,name,0,M::Chase,D::fromInt(2),q(2),scene.owner);}
    std::cout<<"PASS target geometric hit skipped and trigger proxy excluded\n";}

void wedges(){auto c=policy(true,true);c.owners[0].patrolSpeed=c.owners[0].chaseSpeed=D{};c.owners[0].initialRight=true;
    // The filled right wedge is X[1/2,3/2], Y[0,1], surface y=x-1/2.
    // All actual hero bodies are wholly above/outside it. Target's layer2/mask1
    // intentionally does not reciprocate the wedge layer4, allowing endpoint
    // targets on/below its surface without an unrelated solid penetration fault.
    const std::array<Scene,3> scenes{{
        {{q(3),q(2)},{q(3),q(6)},Obstacle::Wedge,2},
        {{q(3),q(2)},{q(3),D{}},Obstacle::Wedge,2},
        {{D{},q(3)},{q(3),q(1)},Obstacle::Wedge,2}}};
    static const std::array<std::string,3> names{"triangle-empty-enclosing-corner","triangle-real-slope-face","triangle-closed-endpoint"};
    for(unsigned n=0;n<scenes.size();++n){const auto scene=scenes[n];DeterministicSession session(sessionConfig());setup(session,c,scene);
        const auto initial=checkpoint(session);witness={names[n]+"-query-geometry",initial,initial,{},{}};
        const auto hits=DetCollisionQuery2D(initial,c.character.collision).raycast(scene.owner,scene.target-scene.owner,{1,6,10,false});
        const auto slope=std::find_if(hits.begin(),hits.end(),[](const auto& hit){return hit.entity==30;});
        if(n==0)require(slope==hits.end(),"Filled wedge empty corner must not inherit its enclosing AABB hit");
        else require(slope!=hits.end() && slope->hit.fraction.bits()==(n==1?q(2):D::fromInt(1)).bits() && slope->hit.axis==2
            && slope->hit.normal.x.bits()==D::fromInt(-1).bits() && slope->hit.normal.y.bits()==D::fromInt(1).bits(),
            "Hand slope fractions1/2 and1, raw(-1,1) upward plane normal");++geometryRows;
        advance(session,frame(0),names[n]);hand(session,c,names[n],0,n?M::SightBlocked:M::AttackRequested,D{},scene.owner.x,scene.owner);}
    std::cout<<"PASS filled-wedge empty corner, real plane and closed endpoint\n";}

void finiteConfig(){auto c=policy();c.owners[0].senseHalfExtent.y=D::fromBits(0x7f800000u);DeterministicSession session(sessionConfig());
    witness={"nonfinite-sense-config",{},{},{},{}};require(!installDetBrain2D(session,c) && !session.faulted(),"Nonfinite LOS sensing policy rejected normally");++negativeConfigs;}
std::string json(const std::string& text){std::string value="\"";for(char c:text){if(c=='\"' || c=='\\')value+='\\';
    if(c=='\n')value+="\\n";else if(c=='\r')value+="\\r";else if(c=='\t')value+="\\t";else if(static_cast<unsigned char>(c)>=0x20)value+=c;}return value+'\"';}
void failed(const fs::path& output,const std::string& reason){if(output.empty())return;
    if(witness.before)saveBytes(output/"failure-before.state",encodeDetCheckpoint(*witness.before));
    if(witness.actual)saveBytes(output/"failure-actual.state",encodeDetCheckpoint(*witness.actual));
    if(witness.expected)saveBytes(output/"failure-expected.state",encodeDetCheckpoint(*witness.expected));
    if(witness.input)saveBytes(output/"failure-canonical-input.bin",encodeDetInput(*witness.input));
    std::ofstream out(output/"failure.json",std::ios::binary);out<<"{\"status\":\"failed\",\"phase\":"<<json(witness.phase)<<",\"reason\":"<<json(reason);
    if(witness.input)out<<",\"tick\":\""<<witness.input->tick<<'\"';
    if(witness.expected && witness.actual){const auto difference=firstDetDifference(*witness.expected,*witness.actual);if(difference)
        out<<",\"firstDifference\":{\"section\":"<<json(difference->section)<<",\"entity\":\""<<difference->entity
            <<"\",\"component\":"<<difference->component<<",\"field\":"<<difference->field<<",\"lane\":"<<difference->lane
            <<",\"expected\":\""<<difference->expected<<"\",\"actual\":\""<<difference->actual<<"\"}";}
    out<<"}\n";out.close();require(bool(out),"Write failure record");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name an existing parent directory");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();for(unsigned n=0;n<1024;++n){
        const auto path=parent/("brainlos2-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}
    throw std::runtime_error("Cannot create fresh LOS artifacts");}
} // namespace
int main(int argc,char** argv){fs::path output;try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];
        if(arg=="--output" && n+1<argc && parent.empty())parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_BrainLineOfSight2DChecks --output EXISTING_PARENT");}
    output=fresh(parent);trace.open(output/"hand-trace.jsonl",std::ios::binary);require(bool(trace),"Open selected hand trace");
    wallAndRestore(output);postBrainMask();excludedProxies();wedges();finiteConfig();trace.close();require(bool(trace),"Seal selected hand trace");
    std::ofstream summary(output/"summary.json",std::ios::binary);summary<<"{\"status\":\"passed\",\"profile\":2,\"closureTicks\":4,\"closureCpRows\":5,\"maxSceneBodies\":3"
        <<",\"handRows\":\""<<handRows<<"\",\"geometryRows\":\""<<geometryRows<<"\",\"advanceCalls\":\""<<advances
        <<"\",\"cpComparisons\":\""<<cpComparisons<<"\",\"negativeRestores\":\""<<negativeRestores<<"\",\"negativeConfigs\":\""<<negativeConfigs
        <<"\",\"oracleScope\":\"selected dyadic LOS decisions/geometry/HP/no-hit fields; complete CP actual restore execution equality, not independent full CP golden or historical visibility authentication\"}\n";
    summary.close();require(bool(summary),"Write completed summary");std::cout<<"PASS Stage58 line of sight; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& error){try{failed(output,error.what());}catch(const std::exception& logging){std::cerr<<"Witness write failed: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Stage58 line of sight: "<<error.what()<<'\n';return 1;}}
