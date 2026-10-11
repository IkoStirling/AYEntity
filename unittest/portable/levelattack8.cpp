// design reference: AYEntity/design.md Stage52 bounded Attack1 file authoring.
// OUT-only draft: selected hand windows/HP differ from full CP execution equality.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
unsigned cases=0,rejects=0,comparisons=0,handRows=0;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetSessionCheckpoint cp(DeterministicSession& s){auto value=s.checkpoint();require(bool(value),"checkpoint: "+s.error());return *value;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);require(bool(input),"canonical source open");
    std::string result((std::istreambuf_iterator<char>(input)),{});require(result.size()<=kDetLevel2DMaxTextBytes,"bounded author text");return result;}
void write(const fs::path& file,const std::string& text){std::ofstream output(file,std::ios::binary);output<<text;output.close();require(bool(output),"fresh text output");}
void write(const fs::path& file,const Bytes& data){std::ofstream output(file,std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));output.close();require(bool(output),"fresh binary output");}
std::string replaced(std::string text,const std::string& a,const std::string& b){const auto at=text.find(a);require(at!=std::string::npos,"fixture token");text.replace(at,a.size(),b);return text;}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"existing output parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto p=parent/("level-attack8-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}throw std::runtime_error("fresh output unavailable");}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=8;l.applicationVersion=52;l.stepDenominator=4;auto& c=l.character;
    c.collision.extended=true;c.collision.oneWayPlatforms=true;c.advanced=true;c.maxHorizontalSpeed=D::fromInt(4);
    c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    DetLevelActor2D owner;owner.id=10;owner.character=true;owner.body={DetBodyMode2D::Kinematic,{q(1),q(1)},{},{},1,2,false};
    auto target=owner;target.id=20;target.position.x=D::fromInt(1);target.body.layer=2;target.body.mask=1;l.actors={owner,target};
    l.vitalityActors={{10,201,20,20,0,0,false},{20,202,20,20,0,0,false}};
    l.vitalityPermissions={{1,20,DetVitalityKind2D::Hurt,3}};
    DetAttackOwner2D attack;attack.actor=10;attack.halfExtent={q(2),q(2)};attack.offset={D::fromInt(1),{}};
    attack.warmupTicks=2;attack.activeTicks=2;attack.recoveryTicks=2;attack.targets={{20,1,3}};l.attackOwners={attack};return l;}
DetTickInput frame(unsigned t,bool reverse=false){const bool held=t==0 || (t>=2 && t<=5) || t>=7;
    DetTickInput i{t,1,{{1,0,1,{static_cast<std::uint8_t>(held)}},{1,1,99,{52}}}};
    if(reverse)std::reverse(i.commands.begin(),i.commands.end());return i;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& level){return s.registerSystem(4,level.character.collision.priority,
    [cfg=detLevelAttackConfig2D(level)](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
        if(cmd.type==1 && cmd.payload.size()==1 && cmd.payload[0]<=1)detSetAttackInput2D(ctx,cfg,10,{cmd.payload[0]!=0,true});
        else if(cmd.type!=99 || cmd.payload!=Bytes{52})return false;}return true;});}
void configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;if(!reverse)require(inputSystem(s,l),"input: "+s.error());
    const bool setup=configureDetLevel2D(s,l,error);require(setup,"Level8 pipeline: "+error);
    if(reverse)require(inputSystem(s,l),"reverse input: "+s.error());const bool sealed=s.seal();require(sealed,"seal: "+s.error());}
void advance(DeterministicSession& s,const DetTickInput& i){const bool ok=s.advance(i);require(ok,"tick "+std::to_string(i.tick)+": "+s.error());}
void equal(const DetSessionCheckpoint& actual,const DetSessionCheckpoint& expected,const std::string& phase){
    if(const auto diff=firstDetDifference(expected,actual))throw std::runtime_error(phase+" tick "+std::to_string(actual.nextTick)
        +" entity "+std::to_string(diff->entity)+" field "+std::to_string(diff->field));++comparisons;}
void compatibility(const fs::path& legacy){const auto text=read(legacy);DetLevel2D old;std::string error;
    const bool decoded=decodeDetLevel2D(text,old,error);require(decoded,error);
    require(old.formatVersion==6 && old.attackOwners.empty() && old.attackCancelOnRecovery
        && detLevelSessionConfig(old).contentHash==UINT64_C(15889101324626947613) && encodeDetLevel2D(old)==text,
        "completed old6 source and actual manifest pin; new default attack state not encoded");
    // This is the same known-valid path-free old source, upgraded only to7. It is
    // a literal default-encoding check, not a claim of an earlier executed old7 hash.
    auto seventh=old;seventh.formatVersion=7;require(encodeDetLevel2D(seventh)==replaced(text,"AYDLEVEL 6","AYDLEVEL 7"),
        "format7 default attack fields add no text");
    for(std::uint32_t version=1;version<=7;++version){auto plain=scene();plain.formatVersion=version;plain.vitalityActors.clear();plain.vitalityPermissions.clear();plain.attackOwners.clear();
        require(validateDetLevel2D(plain,error),"old default format "+std::to_string(version)+": "+error);
        const auto before=encodeDetLevel2D(plain);const auto hash=detLevelSessionConfig(plain).contentHash;
        plain.attackCancelOnRecovery=true;require(encodeDetLevel2D(plain)==before && detLevelSessionConfig(plain).contentHash==hash,
            "all old formats omit default appended attack fields");
        auto bad=plain;bad.attackOwners=scene().attackOwners;require(!validateDetLevel2D(bad,error),"old format forbids authored attack owner");++rejects;
        bad=plain;bad.attackCancelOnRecovery=false;require(!validateDetLevel2D(bad,error),"old format forbids nondefault attack option");++rejects;}
    ++cases;}
DetLevel2D authoring(const fs::path& dir){const auto l=scene();const auto text=encodeDetLevel2D(l);write(dir/"attack8.dlt",text);DetLevel2D output;std::string error;
    const bool loaded=loadDetLevel2D(dir/"attack8.dlt",output,error);require(loaded,error);
    require(encodeDetLevel2D(output)==text && text.find("attacks 1 1\nattack 10 3f000000 3f000000 3f800000 00000000 2 2 2 1\nattacktarget 20 1 3\nend")!=std::string::npos,
        "actual file load preserves all fixed geometry, windows and permissions");
    const auto cfg=detLevelAttackConfig2D(output);require(cfg.owners.size()==1 && cfg.owners[0].targets.size()==1
        && cfg.owners[0].targets[0].permission==1 && cfg.owners[0].targets[0].amount==3
        && cfg.vitality.permissions[0].actor==20 && cfg.vitality.progress.actions.targets.empty()
        && detActionsStateSchema2D(cfg.vitality.progress.actions).version==1,"complete zero-target Actions plus dedicated HP nested identity");
    const auto state=detAttackStateSchemas2D(cfg);require(state.size()==2 && state[0].id==240 && state[1].id==241
        && state[0].version==1 && !detAttackPolicySchemas2D(cfg).empty(),"actual bounded Attack1 factories and namespaces");
    const auto refuse=[&](const std::string& bad){const bool accepted=decodeDetLevel2D(bad,output,error);
        require(!accepted && !error.empty() && encodeDetLevel2D(output)==text,"atomic malformed attack source refusal");++rejects;};
    refuse(replaced(text,"attacks 1 1","attacks 2 1"));refuse(replaced(text,"attacks 1 1","attacks 1 9"));
    refuse(replaced(text,"2 2 2 1\nattacktarget","65536 2 2 1\nattacktarget"));
    refuse(replaced(text,"2 2 2 1\nattacktarget","2 0 2 1\nattacktarget"));
    refuse(replaced(text,"attacktarget 20 1 3","attacktarget 20 1 4"));refuse(replaced(text,"attacktarget 20 1 3","attacktarget 20 0 3"));
    refuse(text+"unknown");
    const auto invalid=[&](const DetLevel2D& bad,const DetLevel2DOptions& options=DetLevel2DOptions{}){
        require(!validateDetLevel2D(bad,error,options) && !error.empty(),"actual joint factory/registration budget refusal");++rejects;};
    auto bad=l;bad.attackOwners[0].targets[0].actor=10;invalid(bad);bad=l;bad.actors[0].body.trigger=true;invalid(bad);
    auto options=DetLevel2DOptions{};options.attackStateSchema=192;invalid(l,options);options={};options.attackSystemId=6;invalid(l,options);
    options={};options.attackPolicySchemaBase=32;invalid(l,options);
    auto empty=l;empty.attackOwners.clear();require(validateDetLevel2D(empty,error),error);empty.attackCancelOnRecovery=false;invalid(empty);
    const auto cooked=encodeDetLevelCooked2D(l);DetLevel2D decoded;const bool decodedCooked=decodeDetLevelCooked2D(cooked,decoded,error);
    require(decodedCooked && encodeDetLevel2D(decoded)==text,"same cooked1 complete Level8 canonical bytes");write(dir/"attack8.dlc",cooked);++cases;return l;}
void hand(const DetSessionCheckpoint& row,const DetAttack2DConfig& cfg,unsigned tick){
    static constexpr std::array<DetAttackPhase2D,16> phases{
        DetAttackPhase2D::Warmup,DetAttackPhase2D::Warmup,DetAttackPhase2D::Active,DetAttackPhase2D::Active,
        DetAttackPhase2D::Recovery,DetAttackPhase2D::Recovery,DetAttackPhase2D::Idle,
        DetAttackPhase2D::Warmup,DetAttackPhase2D::Warmup,DetAttackPhase2D::Active,DetAttackPhase2D::Active,
        DetAttackPhase2D::Recovery,DetAttackPhase2D::Recovery,DetAttackPhase2D::Idle,DetAttackPhase2D::Idle,DetAttackPhase2D::Idle};
    const auto state=detAttackState2D(row,cfg,10);const auto result=detAttackResult2D(row,cfg,10);
    const std::uint64_t attempts=tick<2?0u:tick<9?1u:2u;const std::uint64_t expectedHP=tick<2?20u:tick<9?17u:14u;
    require(state.phase==phases[tick] && state.attackCount==(tick<7?1u:2u) && state.lockedFacingRight && !state.cancelCount,
        "hand W2/A2/R2 processed window and busy held edge consumes without buffer");
    require(detVitalityState2D(row,cfg.vitality,20).hp==expectedHP && detVitalityState2D(row,cfg.vitality,10).hp==20
        && result.attemptCount==attempts,"hand actual fixed permission Hurt3 at2/9 only");
    if(attempts)require(result.tick==(tick<9?2u:9u) && result.target==20 && result.permission==1
        && result.code==DetVitalityCode2D::Applied && result.requested==3 && result.actual==3
        && result.hpAfter==expectedHP && result.hpBefore==expectedHP+3,"historical literal permission and numeric result");
    if((tick>=2 && tick<=6) || tick>=9)require(state.hitMask==1,"target bit persists until next cycle start");
    require(row.actors.at(10).pose.position[0]==D{}.bits() && row.actors.at(20).pose.position[0]==D::fromInt(1).bits(),
        "attack does not acquire a pose writer");++handRows;}
void closure(const fs::path& dir,const DetLevel2D& l){const auto cfg=detLevelAttackConfig2D(l);
    DeterministicSession original(detLevelSessionConfig(l)),reverse(detLevelSessionConfig(l));configure(original,l);configure(reverse,l,true);
    std::vector<DetSessionCheckpoint> rows{cp(original)};std::string trace="0 "+std::to_string(detCheckpointHash(rows.front()))+"\n";
    for(unsigned t=0;t<16;++t){advance(original,frame(t));advance(reverse,frame(t,true));const auto row=cp(original);
        hand(row,cfg,t);equal(cp(reverse),row,"reverse application callback/input order");rows.push_back(row);trace+=std::to_string(t+1)+" "+std::to_string(detCheckpointHash(row))+"\n";}
    const bool restored=original.restore(rows[3]);require(restored,original.error());for(unsigned t=3;t<16;++t){advance(original,frame(t));equal(cp(original),rows[t+1],"restore already-consumed active target bit");}
    const auto before=bytes(original);auto broken=rows[3];const auto schema=detAttackStateSchemas2D(cfg)[0];
    writeDetState(schema,broken.globals.at(cfg.stateSchema),detAttackField2D(cfg,10,DetAttackHitMask),std::uint32_t{0});
    const bool accepted=original.restore(broken);require(!accepted && bytes(original)==before,"ledger consumed bit restore is atomic");++rejects;
    auto other=l;other.attackOwners[0].warmupTicks=3;auto recipe=detLevelSessionConfig(l);recipe.contentHash=0;
    DeterministicSession a(recipe),b(recipe);configure(a,l);configure(b,other);const auto prior=bytes(b);
    require(a.manifest()!=b.manifest() && !b.restore(cp(a)) && bytes(b)==prior,"complete attack window identity even when contentHash agrees");++rejects;
    write(dir/"tick-hashes.txt",trace);write(dir/"initial.state",encodeDetCheckpoint(rows[0]));write(dir/"final.state",encodeDetCheckpoint(rows.back()));
    Bytes inputs;for(unsigned t=0;t<16;++t){const auto encoded=encodeDetInput(frame(t));const auto size=static_cast<std::uint32_t>(encoded.size());
        for(unsigned byte=0;byte<4;++byte)inputs.push_back(static_cast<std::uint8_t>(size>>(8*byte)));inputs.insert(inputs.end(),encoded.begin(),encoded.end());}
    write(dir/"canonical-inputs.bin",inputs);++cases;}
} // namespace
int main(int argc,char** argv){fs::path dir;try{fs::path parent,old;for(int n=1;n<argc;++n){const std::string arg=argv[n];if((arg=="--output" || arg=="--legacy") && n+1<argc){
        if(arg=="--output")parent=argv[++n];else old=argv[++n];}else throw std::invalid_argument("--output existingParent --legacy completedFormat6Input");}
    require(!parent.empty() && !old.empty(),"output and canonical legacy required");dir=fresh(parent);compatibility(old);const auto level=authoring(dir);closure(dir,level);
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)+",\"rejects\":"+std::to_string(rejects)
        +",\"ticks\":16,\"handRows\":"+std::to_string(handRows)+",\"completeCheckpointComparisons\":"+std::to_string(comparisons)
        +",\"oracleScope\":\"selected hand authored attack windows/HP; full CP execution equality\"}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;
}catch(const std::exception& error){std::cerr<<"FAIL levelattack8: "<<error.what()<<'\n';if(!dir.empty())try{write(dir/"failure.txt",error.what());}catch(...){}return 1;}}
