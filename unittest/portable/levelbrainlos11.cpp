// design reference: AYEntity/design.md Stage59 explicit Level11 Brain2 authoring.
// One finite authoring fixture. Full CP path equality is not an independent byte oracle.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
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
using D=ayt::math::DetFloat32;using Bytes=std::vector<std::uint8_t>;
unsigned advances=0,handRows=0,comparisons=0,rejections=0;
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
void require(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason.empty()?"Unspecified authoring failure":reason);}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetSessionCheckpoint cp(DeterministicSession& session){const auto result=session.checkpoint();require(bool(result),session.error());return *result;}
void write(const fs::path& path,const std::string& value){std::ofstream out(path,std::ios::binary);out<<value;out.close();require(bool(out),"Write text artifact");}
void write(const fs::path& path,const Bytes& value){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(value.data()),static_cast<std::streamsize>(value.size()));
    out.close();require(bool(out),"Write checkpoint/cooked artifact");}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"Read accepted Level10 resource");std::string text(kDetLevel2DMaxTextBytes+1,'\0');
    in.read(text.data(),static_cast<std::streamsize>(text.size()));const auto size=in.gcount();require(!in.bad() && size>=0 && static_cast<std::size_t>(size)<=kDetLevel2DMaxTextBytes,"Bounded source input");
    text.resize(static_cast<std::size_t>(size));return text;}
std::uint64_t hash(std::string_view text){std::uint64_t h=UINT64_C(14695981039346656037);for(unsigned char ch:text){h^=ch;h*=UINT64_C(1099511628211);}return h;}
std::string replaced(std::string value,const std::string& from,const std::string& to){const auto at=value.find(from);require(at!=std::string::npos,"Expected exact author grammar anchor");
    value.replace(at,from.size(),to);return value;}
std::string jsonQuote(const std::string& text){std::string out="\"";for(char ch:text){if(ch=='\"' || ch=='\\')out+='\\';if(ch=='\n')out+="\\n";
    else if(ch=='\r')out+="\\r";else if(ch=='\t')out+="\\t";else if(static_cast<unsigned char>(ch)>=0x20)out+=ch;}return out+'\"';}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires an existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto path=parent/("level-brain-los11-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}
    throw std::runtime_error("Cannot create fresh bounded artifacts");}

DetLevel2D scene(bool sight){DetLevel2D level;level.formatVersion=11;level.applicationVersion=59;level.stepDenominator=4;level.brainLineOfSight=sight;
    auto& c=level.character;c.advanced=true;c.collision.oneWayPlatforms=true;c.maxHorizontalSpeed=D::fromInt(4);
    c.acceleration=c.deceleration=D::fromInt(128);c.gravity=c.jumpSpeed=D{};
    DetLevelActor2D owner;owner.id=10;owner.character=true;owner.position={D{},q(1)};owner.body={DetBodyMode2D::Kinematic,{q(1),q(1)},{},{},1,6,false};
    auto target=owner;target.id=20;target.position.x=D::fromInt(8);target.body.layer=2;target.body.mask=1;
    DetLevelActor2D wall;wall.id=30;wall.position={D::fromInt(1),q(1)};wall.body={DetBodyMode2D::Static,{q(1),q(1)},{},{},4,1,false};
    level.actors={owner,target,wall};level.vitalityActors={{10,1,20,20,0,0,false},{20,2,20,20,0,0,false}};
    level.vitalityPermissions={{1,20,DetVitalityKind2D::Hurt,3}};
    DetAttackOwner2D attack;attack.actor=10;attack.halfExtent={q(1),q(1)};attack.offset={q(2),D{}};
    attack.warmupTicks=attack.activeTicks=attack.recoveryTicks=1;attack.targets={{20,1,3}};level.attackOwners={attack};
    DetBrainOwner2D brain;brain.actor=10;brain.target=20;brain.patrolLeft=D::fromInt(-8);brain.patrolRight=D::fromInt(8);
    brain.patrolSpeed=D::fromInt(1);brain.chaseSpeed=D::fromInt(2);brain.senseHalfExtent={D::fromInt(16),D::fromInt(2)};
    brain.attackDistance=q(1);brain.initialRight=false;level.brainOwners={brain};return level;}
DetTickInput frame(unsigned tick){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(tick==1)}}}};}
void configure(DeterministicSession& session,const DetLevel2D& level){std::string error;const auto collision=level.character.collision;
    const bool installed=configureDetLevel2D(session,level,error,{},[collision](DeterministicSession& target){
        // This test-only canonical input stimulus precedes Brain3. It edits only
        // the registered wall mask, never character intent, HP, pose or velocity.
        return target.registerSystem(2,collision.priority,[collision](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
            if(command.source!=1 || command.type!=1 || command.payload.size()!=1 || command.payload[0]>1)return false;
            if(command.payload[0])ctx.write(30,collision.bodySchema,DetBodyMask,std::uint32_t{});}return true;});});
    require(installed,"Level11 configure: "+error);const bool sealed=session.seal();require(sealed,"Level11 seal: "+session.error());}
void advance(DeterministicSession& session,const DetTickInput& input,const std::string& phase){witness={phase,cp(session),{},{},input};
    const bool ok=session.advance(input);witness.actual=session.checkpoint();require(ok,"input tick "+std::to_string(input.tick)+": "+session.error());++advances;}
void equal(DeterministicSession& session,const DetSessionCheckpoint& expected,const std::string& phase){witness.actual=cp(session);witness.expected=expected;
    require(encodeDetCheckpoint(*witness.actual)==encodeDetCheckpoint(expected),phase+" at nextTick "+std::to_string(expected.nextTick));++comparisons;}

void compatibility(const fs::path& path){witness={"accepted-Level10-compatibility",{},{},{},{}};const auto source=read(path);
    // Stage56 accepted source is1156 CRLF bytes. Its already accepted CLI
    // canonical artifact is1126 LF bytes; these are distinct byte witnesses.
    require(source.size()==1156 && hash(source)==UINT64_C(3109901471447196851),"Accepted Level10 complete physical source fingerprint");
    DetLevel2D old;std::string error;const bool decoded=decodeDetLevel2D(source,old,error);require(decoded,error);
    const auto canonical=encodeDetLevel2D(old);require(canonical.size()==1126 && hash(canonical)==UINT64_C(1909346928465245357)
        && old.formatVersion==10 && !old.brainLineOfSight && detLevelSessionConfig(old).contentHash==UINT64_C(15814538279326814389),
        "Accepted Level10 canonical bytes/content identity do not acquire LOS");
    auto explicitDefault=old;explicitDefault.brainLineOfSight=false;require(encodeDetLevel2D(explicitDefault)==canonical
        && detLevelSessionConfig(explicitDefault).contentHash==detLevelSessionConfig(old).contentHash,"Explicit old false adds no words/default identity");
    const auto brain=detLevelBrainConfig2D(old);require(!brain.lineOfSight && detBrainStateSchema2D(brain).version==1,"Old10 retains actual Brain1 descriptor/profile");
    old.brainLineOfSight=true;require(!validateDetLevel2D(old,error) && error.find("line of sight")!=std::string::npos,"Old10 refuses nondefault LOS");++rejections;
    for(unsigned version=1;version<10;++version){auto forbidden=scene(true);forbidden.formatVersion=version;
        require(!validateDetLevel2D(forbidden,error) && error.find("line of sight")!=std::string::npos,"All old formats explicitly refuse new flag before other new tables");++rejections;}
}
DetLevel2D author(const fs::path& output){witness={"Level11-file-getter-cooked",{},{},{},{}};const auto initial=scene(true);const auto text=encodeDetLevel2D(initial);
    require(text.find("\nbrains 1 1\nbrain 10 20 ")!=std::string::npos && text.find("\ncontrols 0\nend\n")!=std::string::npos,"Exact new brains header, unchanged control order");
    write(output/"sight11.dlt",text);DetLevel2D loaded;std::string error;const bool loadedOk=loadDetLevel2D(output/"sight11.dlt",loaded,error);require(loadedOk,error);
    require(encodeDetLevel2D(loaded)==text,"Actual file load preserves complete Level11 fields");const auto cfg=detLevelBrainConfig2D(loaded);
    const auto attack=detLevelAttackConfig2D(loaded);const auto policy=detBrainPolicySchemas2D(cfg);
    require(cfg.lineOfSight && cfg.stateSchema==272 && cfg.policySchemaBase==273 && cfg.systemId==3 && cfg.validatorId==78 && cfg.logicProfileId==78
        && cfg.owners.size()==1 && cfg.owners[0].actor==10 && cfg.owners[0].target==20 && detBrainStateSchema2D(cfg).version==2
        && !policy.empty() && policy.front().id==273 && policy.front().version==2
        && encodeDetAttackConfig2D(cfg.attack)==encodeDetAttackConfig2D(attack),"Real selected Brain2 factory/full nested authority without copied descriptors");
    const auto cooked=encodeDetLevelCooked2D(loaded);DetLevel2D roundtrip;const bool cookedOk=decodeDetLevelCooked2D(cooked,roundtrip,error);require(cookedOk,error);
    require(encodeDetLevel2D(roundtrip)==text && detLevelSessionConfig(roundtrip).contentHash==detLevelSessionConfig(loaded).contentHash,"Cooked1 exact Level11 complete author identity");
    write(output/"sight11.dlc",cooked);const auto refuse=[&](const std::string& bad){const bool accepted=decodeDetLevel2D(bad,loaded,error);
        require(!accepted && !error.empty() && encodeDetLevel2D(loaded)==text,"Malformed new grammar/role leaves output atomic");++rejections;};
    refuse(replaced(text,"brains 1 1","brains 1 2"));refuse(replaced(text,"brains 1 1","brains 1"));
    refuse(replaced(text,"AYDLEVEL 11","AYDLEVEL 10"));refuse(replaced(text,"brain 10 20","brain 10 999"));
    auto empty=initial;empty.brainOwners.clear();require(!validateDetLevel2D(empty,error) && error.find("requires brain")!=std::string::npos,"Empty table true is a semantic refusal");++rejections;
    empty.brainLineOfSight=false;require(validateDetLevel2D(empty,error),error);bool threw=false;try{(void)detLevelBrainConfig2D(empty);}catch(const std::invalid_argument&){threw=true;}
    require(threw,"Empty false table installs no Brain and getter refuses");
    auto defaulted=initial;defaulted.brainLineOfSight=false;const auto oldCfg=detLevelBrainConfig2D(defaulted);
    require(!oldCfg.lineOfSight && detBrainStateSchema2D(oldCfg).version==1 && encodeDetBrainConfig2D(cfg)!=encodeDetBrainConfig2D(oldCfg)
        && detLevelSessionConfig(defaulted).contentHash!=detLevelSessionConfig(initial).contentHash,"Explicit false selects1; true enters complete Level/Brain identity");
    return roundtrip;
}
void hand(const DetSessionCheckpoint& row,const DetBrain2DConfig& cfg,unsigned tick,bool sight){const auto state=detBrainState2D(row,cfg,10);
    // Center target8 stays outside the1/4 attack radius throughout. At T0 only
    // LOS-on patrols -1/4. Mask0 before Brain at T1 then permits chase2 (+1/2/tick).
    static constexpr std::array<int,8> blockedX{-1,1,3,5,7,9,11,13};
    const int expectedX=sight?blockedX[tick]:2*static_cast<int>(tick+1);
    const int observedX=tick?(sight?blockedX[tick-1]:2*static_cast<int>(tick)):0;
    require(state.mode==(sight && tick==0?DetBrainMode2D::SightBlocked:DetBrainMode2D::Chase)
        && state.desiredSpeed.bits()==D::fromInt(sight && tick==0?-1:2).bits() && !state.desiredAttackHeld && state.attackPulseCount==0
        && state.decisionCount==tick+1 && state.lastDecisionTick==tick && state.observedAttackProcessedTick==tick,
        "Selected hand mode/intent/count over actual early wall mask");
    require(row.actors.at(10).pose.position[0]==q(expectedX).bits() && row.actors.at(10).pose.position[1]==q(1).bits()
        && state.observedOwnerPosition.x.bits()==q(observedX).bits() && state.observedTargetPosition.x.bits()==D::fromInt(8).bits(),
        "Independent dyadic positions and pre-motion sample");
    require(detVitalityState2D(row,cfg.attack.vitality,10).hp==20 && detVitalityState2D(row,cfg.attack.vitality,20).hp==20
        && detAttackState2D(row,cfg.attack,10).phase==DetAttackPhase2D::Idle,"Author LOS has no pose or HP authority");++handRows;
}
void behavior(const fs::path& output,const DetLevel2D& loaded){const auto cfg=detLevelBrainConfig2D(loaded);const auto direct=scene(true);
    DeterministicSession session(detLevelSessionConfig(loaded)),restored(detLevelSessionConfig(direct));configure(session,loaded);configure(restored,direct);
    std::vector<DetSessionCheckpoint> rows{cp(session)};equal(restored,rows.front(),"Direct vs file/cooked initial CP");std::string trace="0 "+std::to_string(detCheckpointHash(rows.front()))+"\n";
    for(unsigned tick=0;tick<8;++tick){advance(session,frame(tick),"authored-LOS-on");rows.push_back(cp(session));hand(rows.back(),cfg,tick,true);
        trace+=std::to_string(tick+1)+" "+std::to_string(detCheckpointHash(rows.back()))+"\n";}
    witness={"restore-real-blocked-CP1",cp(restored),{},rows[1],{}};const bool accepted=restored.restore(rows[1]);witness.actual=restored.checkpoint();require(accepted,restored.error());
    equal(restored,rows[1],"Whole blocked checkpoint restore");for(unsigned tick=1;tick<8;++tick){advance(restored,frame(tick),"restored-authored-LOS");equal(restored,rows[tick+1],"Full actual continuation equality");}
    const auto off=scene(false);const auto offCfg=detLevelBrainConfig2D(off);DeterministicSession defaulted(detLevelSessionConfig(off));configure(defaulted,off);
    for(unsigned tick=0;tick<8;++tick){advance(defaulted,frame(tick),"authored-LOS-off");hand(cp(defaulted),offCfg,tick,false);}
    const auto before=cp(defaulted);witness={"foreign-selected-Level-identity",before,{},rows[1],{}};const bool foreign=defaulted.restore(rows[1]);witness.actual=defaulted.checkpoint();
    require(!foreign && encodeDetCheckpoint(cp(defaulted))==encodeDetCheckpoint(before),"Different full selected Level/Brain manifest refuses atomically");++rejections;
    write(output/"initial.state",encodeDetCheckpoint(rows.front()));write(output/"blocked.state",encodeDetCheckpoint(rows[1]));write(output/"final.state",encodeDetCheckpoint(rows.back()));
    write(output/"tick-hashes.txt",trace);Bytes inputs;for(unsigned tick=0;tick<8;++tick){const auto raw=encodeDetInput(frame(tick));const auto length=static_cast<std::uint32_t>(raw.size());
        for(unsigned n=0;n<4;++n)inputs.push_back(static_cast<std::uint8_t>(length>>(n*8)));inputs.insert(inputs.end(),raw.begin(),raw.end());}write(output/"canonical-inputs.bin",inputs);
    witness={"completed-Level11-authoring",{},{},rows.back(),{}};
}
void failure(const fs::path& output,const std::string& reason){if(output.empty())return;
    if(witness.before)write(output/"failure-before.state",encodeDetCheckpoint(*witness.before));if(witness.actual)write(output/"failure-actual.state",encodeDetCheckpoint(*witness.actual));
    if(witness.expected)write(output/"failure-expected.state",encodeDetCheckpoint(*witness.expected));if(witness.input)write(output/"failure-canonical-input.bin",encodeDetInput(*witness.input));
    std::string text="{\"status\":\"failed\",\"phase\":"+jsonQuote(witness.phase)+",\"reason\":"+jsonQuote(reason);
    if(witness.input)text+=",\"inputTick\":\""+std::to_string(witness.input->tick)+"\"";
    if(witness.expected && witness.actual){const auto difference=firstDetDifference(*witness.expected,*witness.actual);if(difference)text+=",\"firstDifference\":{\"section\":"+jsonQuote(difference->section)
        +",\"entity\":\""+std::to_string(difference->entity)+"\",\"component\":"+std::to_string(difference->component)+",\"field\":"+std::to_string(difference->field)
        +",\"lane\":"+std::to_string(difference->lane)+",\"expected\":\""+std::to_string(difference->expected)+"\",\"actual\":\""+std::to_string(difference->actual)+"\"}";}
    write(output/"failure.json",text+"}\n");}
} // namespace
int main(int argc,char** argv){fs::path output;try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string arg=argv[n];
    if(arg=="--output" && n+1<argc && parent.empty())parent=argv[++n];else if(arg=="--legacy" && n+1<argc && legacy.empty())legacy=argv[++n];
    else throw std::invalid_argument("Usage: AYEntity_LevelBrainLineOfSight11Checks --output EXISTING_PARENT --legacy ACCEPTED_LEVEL10.dlt");}
    require(!legacy.empty(),"Actual accepted Level10 resource required");output=fresh(parent);compatibility(legacy);const auto level=author(output);behavior(output,level);
    write(output/"summary.json","{\"status\":\"passed\",\"levelProfile\":11,\"brainProfile\":2,\"closureTicks\":8,\"closureCpRows\":9,\"handRows\":"+std::to_string(handRows)
        +",\"advanceCalls\":"+std::to_string(advances)+",\"cpComparisons\":"+std::to_string(comparisons)+",\"rejections\":"+std::to_string(rejections)
        +",\"oracleScope\":\"selected authored mode/intent/dyadic positions/HP; full actual initial and restore-continuation CP equality; no historical LOS authentication\"}\n");
    std::cout<<"PASS Level11 explicit scene sight file/getter/cooked;8tick hand/restore; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& error){try{failure(output,error.what());}catch(const std::exception& logging){std::cerr<<"Witness failure: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Level11 authoring: "<<error.what()<<'\n';return 1;}}
