// design reference: AYEntity/design.md Stage54 finite Brain1 file authoring.
// OUT draft. Selected hand decisions/positions/HP are distinct from whole CP path equality.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
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
unsigned handRows=0,comparisons=0,rejections=0;
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
void require(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason.empty()?"Unspecified level setup failure":reason);}
D q(int value){return D::fromInt(value)/D::fromInt(4);}
DetSessionCheckpoint cp(DeterministicSession& s){const auto value=s.checkpoint();require(bool(value),s.error());return *value;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
void write(const fs::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();require(bool(out),"Write author artifact");}
void write(const fs::path& path,const Bytes& data){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    out.close();require(bool(out),"Write binary artifact");}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);require(bool(input),"Read actual existing Level8 fixture");
    std::string result(kDetLevel2DMaxTextBytes+1,'\0');input.read(result.data(),static_cast<std::streamsize>(result.size()));const auto count=input.gcount();
    require(!input.bad() && count>=0 && static_cast<std::size_t>(count)<=kDetLevel2DMaxTextBytes,"Bounded fixture text");result.resize(static_cast<std::size_t>(count));return result;}
std::uint64_t hash(std::string_view value){std::uint64_t h=UINT64_C(14695981039346656037);for(unsigned char byte:value){h^=byte;h*=UINT64_C(1099511628211);}return h;}
std::string replaced(std::string text,const std::string& old,const std::string& value){const auto at=text.find(old);require(at!=std::string::npos,"Expected exact fixture token");
    text.replace(at,old.size(),value);return text;}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires an existing directory");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();for(unsigned n=0;n<1024;++n){
        const auto p=parent/("level-brain9-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}throw std::runtime_error("No fresh artifact directory");}
DetLevel2D scene(){DetLevel2D level;level.formatVersion=9;level.applicationVersion=54;level.stepDenominator=4;
    auto& c=level.character;c.advanced=true;c.collision.oneWayPlatforms=true;c.maxHorizontalSpeed=D::fromInt(4);
    c.acceleration=c.deceleration=D::fromInt(128);c.gravity=c.jumpSpeed=D{};
    DetLevelActor2D hero;hero.id=10;hero.character=true;hero.position={D::fromInt(2),q(1)};
    hero.body={DetBodyMode2D::Kinematic,{q(1),q(1)},{},{},1,6,false};
    auto enemy=hero;enemy.id=20;enemy.position.x={};enemy.body.layer=2;enemy.body.mask=13;
    DetLevelActor2D floor;floor.id=30;floor.position={D{},-q(1)};floor.body={DetBodyMode2D::Static,{D::fromInt(128),q(1)},{},{},4,3,false};
    DetLevelActor2D trigger;trigger.id=40;trigger.position={D::fromInt(1),q(1)};trigger.body={DetBodyMode2D::Static,{D::fromInt(4),D::fromInt(1)},{},{},8,2,true};
    level.actors={hero,enemy,floor,trigger};level.vitalityActors={{10,201,20,20,0,0,false},{20,202,20,20,0,0,false}};
    level.vitalityPermissions={{1,10,DetVitalityKind2D::Hurt,3}};
    DetAttackOwner2D attack;attack.actor=20;attack.halfExtent={q(2),q(2)};attack.offset={q(2),D{}};
    attack.warmupTicks=attack.activeTicks=attack.recoveryTicks=1;attack.targets={{10,1,3}};level.attackOwners={attack};
    DetBrainOwner2D brain;brain.actor=20;brain.target=10;brain.patrolLeft=D::fromInt(-2);brain.patrolRight=D::fromInt(2);
    brain.patrolSpeed=D::fromInt(1);brain.chaseSpeed=D::fromInt(2);brain.senseHalfExtent={D::fromInt(4),D::fromInt(1)};brain.attackDistance=D::fromInt(1);
    level.brainOwners={brain};return level;}
DetTickInput frame(unsigned tick){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(tick==8)}},{1,1,99,{54}}}};}
void configure(DeterministicSession& s,const DetLevel2D& level){std::string error;const bool configured=configureDetLevel2D(s,level,error,{},
    [character=level.character](DeterministicSession& session){return session.registerSystem(4,character.collision.priority,[character](DetTickContext& ctx){
        for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
            if(cmd.type==1 && cmd.payload.size()==1 && cmd.payload[0]<=1)detCharacterInput2D(ctx,10,character,{D::fromInt(cmd.payload[0]),false,false,false});
            else if(cmd.type!=99 || cmd.payload!=Bytes{54})return false;}return true;});});
    require(configured,"Level9 configure: "+error);const bool sealed=s.seal();require(sealed,"Level9 seal: "+s.error());}
void advance(DeterministicSession& s,const DetTickInput& input){witness={"authored-level9-advance",cp(s),{},{},input};
    const bool ok=s.advance(input);witness.actual=s.checkpoint();require(ok,"input tick "+std::to_string(input.tick)+": "+s.error());}
void equal(DeterministicSession& s,const DetSessionCheckpoint& expected){witness.expected=expected;witness.actual=cp(s);
    if(encodeDetCheckpoint(*witness.actual)!=encodeDetCheckpoint(expected)){const auto diff=firstDetDifference(expected,*witness.actual);
        throw std::runtime_error("Restored full CP differs at tick "+std::to_string(expected.nextTick)+(diff?" field "+std::to_string(diff->field):" bytes"));}++comparisons;}
void compatible(const fs::path& legacy){witness={"existing-level8-default-compatibility",{},{},{},{}};const auto text=read(legacy);
    // Input bytes come from the actually accepted Stage52 Host resource, not a
    // synthetic version-header upgrade or a independently regenerated full CP.
    require(text.size()==994 && hash(text)==UINT64_C(851155516068888591),"Existing accepted Level8 complete source-byte fingerprint");
    DetLevel2D old;std::string error;const bool decoded=decodeDetLevel2D(text,old,error);require(decoded,error);
    require(old.formatVersion==8 && old.brainOwners.empty() && encodeDetLevel2D(old)==text,"Level8 literal source unchanged by appended default Brain table");
    const auto oldHash=detLevelSessionConfig(old).contentHash;auto explicitDefault=old;explicitDefault.brainOwners.clear();
    require(detLevelSessionConfig(explicitDefault).contentHash==oldHash && encodeDetLevel2D(explicitDefault)==text,"Level8 complete semantic identity unchanged by default Brain fields");
    auto options=DetLevel2DOptions{};options.brainStateSchema=300;options.brainPolicySchemaBase=301;options.brainSystemId=2;
    require(encodeDetLevel2D(old,options)==text,"Unused Brain registration options do not register old format authority");
    for(unsigned version=1;version<=8;++version){auto plain=scene();plain.formatVersion=version;plain.brainOwners.clear();plain.attackOwners.clear();
        plain.vitalityActors.clear();plain.vitalityPermissions.clear();require(validateDetLevel2D(plain,error),"Default old-format preflight: "+error);
        const auto source=encodeDetLevel2D(plain);plain.brainOwners=scene().brainOwners;
        require(!validateDetLevel2D(plain,error) && !error.empty(),"All older formats reject nondefault Brain ownership");++rejections;
        plain.brainOwners.clear();require(encodeDetLevel2D(plain)==source,"No empty Brain row appended to old grammar");}
}
DetLevel2D author(const fs::path& output){witness={"level9-file-getter-cooked",{},{},{},{}};const auto authored=scene();const auto text=encodeDetLevel2D(authored);
    require(text.find("\nbrains 1\nbrain 20 10 c0000000 40000000 3f800000 40000000 40800000 3f800000 3f800000 1\nend\n")!=std::string::npos,
        "Strict actual full fixed Brain row in canonical author text");write(output/"brain9.dlt",text);DetLevel2D loaded;std::string error;
    const bool readLevel=loadDetLevel2D(output/"brain9.dlt",loaded,error);require(readLevel,error);require(encodeDetLevel2D(loaded)==text,"Atomic actual file load complete content");
    const auto config=detLevelBrainConfig2D(loaded);const auto attack=detLevelAttackConfig2D(loaded);
    require(config.owners.size()==1 && config.owners[0].actor==20 && config.owners[0].target==10
        && config.stateSchema==272 && config.policySchemaBase==273 && config.systemId==3 && config.validatorId==78 && config.logicProfileId==78
        && encodeDetAttackConfig2D(config.attack)==encodeDetAttackConfig2D(attack)
        && !config.attack.vitality.respawnRecovery && !config.character.collision.characterRespawn,"Actual no-recovery fixed enemy20→hero10 getter identity");
    const auto state=detBrainStateSchema2D(config);const auto policy=detBrainPolicySchemas2D(config);
    require(state.id==272 && state.version==1 && state.fields.size()==16 && !policy.empty() && policy.front().id==273,
        "Actual selected Brain factories, no copied descriptor/chunk constants");
    const auto cooked=encodeDetLevelCooked2D(loaded);DetLevel2D roundtrip;const bool decoded=decodeDetLevelCooked2D(cooked,roundtrip,error);
    require(decoded && encodeDetLevel2D(roundtrip)==text && detLevelSessionConfig(roundtrip).contentHash==detLevelSessionConfig(loaded).contentHash,
        "Cooked1 actual complete canonical Level9 identity");write(output/"brain9.dlc",cooked);
    const auto refuse=[&](const std::string& malformed){const bool accepted=decodeDetLevel2D(malformed,loaded,error);
        require(!accepted && !error.empty() && encodeDetLevel2D(loaded)==text,"Malformed Brain authoring atomic output refusal");++rejections;};
    refuse(replaced(text,"brains 1","brains 9"));refuse(replaced(text,"brain 20 10","brain 20 20"));
    refuse(replaced(text,"brain 20 10","brain 10 20"));refuse(replaced(text,"brain 20 10","brain 20 999"));
    refuse(replaced(text,"c0000000 40000000 3f800000","40000000 40000000 3f800000"));
    refuse(replaced(text,"40800000 3f800000 3f800000 1","7fc00000 3f800000 3f800000 1"));
    refuse(replaced(text,"40800000 3f800000 3f800000 1","40800000 3f800000 40a00000 1"));
    refuse(replaced(text,"40800000 3f800000 3f800000 1","40800000 3f800000 3f800000 2"));
    refuse(replaced(text,"AYDLEVEL 9","AYDLEVEL 8"));refuse(text+"unknown");
    const auto invalid=[&](const DetLevel2D& level,const DetLevel2DOptions& options=DetLevel2DOptions{}){
        require(!validateDetLevel2D(level,error,options) && !error.empty(),"Real selected joint policy/role namespace refusal");++rejections;};
    auto bad=authored;bad.actors[1].character=false;invalid(bad);bad=authored;bad.attackOwners.clear();invalid(bad);
    bad=authored;bad.brainOwners.push_back(bad.brainOwners.front());invalid(bad);
    auto options=DetLevel2DOptions{};options.brainStateSchema=options.attackStateSchema;invalid(authored,options);
    options={};options.brainPolicySchemaBase=options.identitySchemaBase;invalid(authored,options);
    options={};options.brainSystemId=options.attackSystemId;invalid(authored,options);
    options={};options.brainValidatorId=options.attackValidatorId;invalid(authored,options);
    options={};options.brainLogicProfileId=options.attackLogicProfileId;invalid(authored,options);
    auto empty=authored;empty.brainOwners.clear();require(validateDetLevel2D(empty,error),error);bool throws=false;
    try{(void)detLevelBrainConfig2D(empty);}catch(const std::invalid_argument&){throws=true;}require(throws,"Empty Brain table installs no facade and getter throws");
    return authored;
}
void hand(const DetSessionCheckpoint& checkpoint,const DetBrain2DConfig& cfg,unsigned tick){const auto brain=detBrainState2D(checkpoint,cfg,20);
    const auto attack=detAttackState2D(checkpoint,cfg.attack,20);const auto result=detAttackResult2D(checkpoint,cfg.attack,20);
    using M=DetBrainMode2D;using P=DetAttackPhase2D;
    static constexpr std::array<M,16> mode{M::Chase,M::Chase,M::AttackRequested,M::Busy,M::Busy,M::Busy,M::AttackRequested,M::Busy,
        M::Busy,M::Busy,M::Chase,M::AttackRequested,M::Busy,M::Busy,M::Busy,M::AttackRequested};
    static constexpr std::array<P,16> phase{P::Idle,P::Idle,P::Warmup,P::Active,P::Recovery,P::Idle,P::Warmup,P::Active,
        P::Recovery,P::Idle,P::Idle,P::Warmup,P::Active,P::Recovery,P::Idle,P::Warmup};
    static constexpr std::array<int,16> x{2,4,4,4,4,4,4,4,4,4,6,6,6,6,6,6};
    static constexpr std::array<unsigned,16> pulses{0,0,1,1,1,1,2,2,2,2,2,3,3,3,3,4};
    static constexpr std::array<unsigned,16> health{20,20,20,17,17,17,17,14,14,14,14,14,11,11,11,11};
    const bool moving=tick==0 || tick==1 || tick==10,held=tick==2 || tick==6 || tick==11 || tick==15;
    require(brain.mode==mode[tick] && brain.observedAttackPhase==(tick?phase[tick-1]:P::Idle) && attack.phase==phase[tick]
        && brain.desiredSpeed.bits()==(moving?D::fromInt(2):D{}).bits() && brain.desiredAttackHeld==held
        && brain.attackPulseCount==pulses[tick] && attack.attackCount==pulses[tick],"Independent authored chase and separate previous/current attack table");
    require(brain.decisionCount==tick+1 && brain.lastDecisionTick==tick && brain.observedAttackProcessedTick==tick
        && brain.observedOwnerAlive && brain.observedTargetAlive,"Real processed decision/HP observations");
    require(checkpoint.actors.at(20).pose.position[0]==q(x[tick]).bits() && checkpoint.actors.at(10).pose.position[0]==q(tick<8?8:9).bits()
        && checkpoint.actors.at(20).pose.position[1]==q(1).bits(),"Hand dyadic physical enemy and canonical player motion");
    require(brain.observedOwnerPosition.x.bits()==(tick?q(x[tick-1]):D{}).bits()
        && brain.observedTargetPosition.x.bits()==q(tick<=8?8:9).bits(),"Actual Brain pre-player/pre-solver capture");
    require(detVitalityState2D(checkpoint,cfg.attack.vitality,10).hp==health[tick]
        && detVitalityState2D(checkpoint,cfg.attack.vitality,20).hp==20,"Actual fixed Hurt3 at3/7/12; no HP or pose writes in Brain");
    if(tick==3 || tick==7 || tick==12)require(result.tick==tick && result.target==10 && result.code==DetVitalityCode2D::Applied
        && result.actual==3,"Matching real Hurt permission lifecycle");++handRows;
}
void behavior(const fs::path& output,const DetLevel2D& level){const auto cfg=detLevelBrainConfig2D(level);DeterministicSession session(detLevelSessionConfig(level));configure(session,level);
    std::vector<DetSessionCheckpoint> rows{cp(session)};std::string trace="0 "+std::to_string(detCheckpointHash(rows.front()))+"\n";
    for(unsigned tick=0;tick<16;++tick){advance(session,frame(tick));rows.push_back(cp(session));hand(rows.back(),cfg,tick);
        trace+=std::to_string(tick+1)+" "+std::to_string(detCheckpointHash(rows.back()))+"\n";}
    witness={"restore-committed-brain-window",cp(session),{},rows[4],{}};const bool restored=session.restore(rows[4]);witness.actual=session.checkpoint();require(restored,session.error());equal(session,rows[4]);
    for(unsigned tick=4;tick<16;++tick){advance(session,frame(tick));equal(session,rows[tick+1]);}
    auto bad=rows[4];const auto state=detBrainStateSchema2D(cfg);writeDetState(state,bad.globals.at(cfg.stateSchema),detBrainField2D(cfg,20,DetBrainObservedAttackProcessedTick),std::uint64_t{4});
    const auto unchanged=bytes(session);witness={"atomic-malformed-brain-record",cp(session),{},bad,{}};const bool accepted=session.restore(bad);witness.actual=session.checkpoint();
    require(!accepted && bytes(session)==unchanged,"Registered observation stamp malformed restore is atomic");++rejections;
    auto other=level;other.brainOwners[0].senseHalfExtent.x=D::fromInt(5);auto recipe=detLevelSessionConfig(level);recipe.contentHash=0;
    DeterministicSession source(recipe),target(recipe);configure(source,level);configure(target,other);const auto before=bytes(target);
    require(source.manifest()!=target.manifest() && !target.restore(cp(source)) && bytes(target)==before,
        "Complete Brain/Level identity rejects changed authoring even with identical contentHash");++rejections;
    write(output/"initial.state",encodeDetCheckpoint(rows.front()));write(output/"final.state",encodeDetCheckpoint(rows.back()));write(output/"tick-hashes.txt",trace);
    Bytes frames;for(unsigned tick=0;tick<16;++tick){const auto data=encodeDetInput(frame(tick));const auto count=static_cast<std::uint32_t>(data.size());
        for(unsigned n=0;n<4;++n)frames.push_back(static_cast<std::uint8_t>(count>>(8*n)));frames.insert(frames.end(),data.begin(),data.end());}write(output/"canonical-inputs.bin",frames);
    witness={"completed-authoring-behavior",{},{},rows.back(),{}};
}
std::string json(const std::string& value){std::string out="\"";for(char ch:value){if(ch=='\"' || ch=='\\')out+='\\';
    if(ch=='\n')out+="\\n";else if(ch=='\r')out+="\\r";else if(ch=='\t')out+="\\t";else if(static_cast<unsigned char>(ch)>=0x20)out+=ch;}return out+'\"';}
void failure(const fs::path& output,const std::string& reason){if(output.empty())return;if(witness.before)write(output/"failure-before.state",encodeDetCheckpoint(*witness.before));
    if(witness.actual)write(output/"failure-actual.state",encodeDetCheckpoint(*witness.actual));if(witness.expected)write(output/"failure-expected.state",encodeDetCheckpoint(*witness.expected));
    if(witness.input)write(output/"failure-canonical-input.bin",encodeDetInput(*witness.input));
    write(output/"failure.json","{\"status\":\"failed\",\"phase\":"+json(witness.phase)+",\"reason\":"+json(reason)+"}\n");}
}
int main(int argc,char** argv){fs::path output;try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string key=argv[n];
    if(key=="--output" && n+1<argc && parent.empty())parent=argv[++n];else if(key=="--legacy" && n+1<argc && legacy.empty())legacy=argv[++n];
    else throw std::invalid_argument("Usage: AYEntity_LevelBrain9Checks --output EXISTING_PARENT --legacy ACTUAL_LEVEL8.dlt");}
    require(!legacy.empty(),"--legacy actual accepted Level8 resource required");output=fresh(parent);compatible(legacy);const auto level=author(output);behavior(output,level);
    write(output/"summary.json","{\"status\":\"passed\",\"levelProfile\":9,\"brainProfile\":1,\"ticks\":16,\"cpRows\":17,\"handRows\":"+std::to_string(handRows)
        +",\"cpComparisons\":"+std::to_string(comparisons)+",\"rejections\":"+std::to_string(rejections)+",\"oracleScope\":\"selected authored hand decisions/positions/HP; full CP restore path equality\"}\n");
    std::cout<<"PASS Level9 file/getter/cooked/no-recovery enemy20-target10;16tick hand+restore; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){try{failure(output,e.what());}catch(const std::exception& logging){std::cerr<<"Witness failure: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Level9 authoring: "<<e.what()<<'\n';return 1;}}
