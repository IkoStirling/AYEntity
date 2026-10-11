// design reference: AYEntity/design.md Stage56 finite Control1 file authoring.
// OUT-only draft. Selected hand windows/positions are distinct from full CP path equality.
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
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
using Phase=DetCharacterControlPhase2D;using Outcome=DetCharacterControlOutcome2D;
std::uint64_t handRows=0,comparisons=0,rejections=0,advanceCalls=0;
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
void require(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason.empty()?"Unspecified authored setup failure":reason);}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetSessionCheckpoint cp(DeterministicSession& s){const auto value=s.checkpoint();require(bool(value),s.error());return *value;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
void write(const fs::path& file,const std::string& text){std::ofstream output(file,std::ios::binary);output<<text;output.close();require(bool(output),"Write fresh author artifact");}
void write(const fs::path& file,const Bytes& data){std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    output.close();require(bool(output),"Write fresh binary artifact");}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);require(bool(input),"Read accepted Level9 resource");
    std::string value(kDetLevel2DMaxTextBytes+1,'\0');input.read(value.data(),static_cast<std::streamsize>(value.size()));const auto count=input.gcount();
    require(!input.bad() && count>=0 && static_cast<std::size_t>(count)<=kDetLevel2DMaxTextBytes,"Bounded author text");value.resize(static_cast<std::size_t>(count));return value;}
std::uint64_t hash(std::string_view bytes){std::uint64_t result=UINT64_C(14695981039346656037);for(unsigned char value:bytes){result^=value;result*=UINT64_C(1099511628211);}return result;}
std::string replaced(std::string text,const std::string& old,const std::string& value){const auto at=text.find(old);require(at!=std::string::npos,"Exact fixture token");
    text.replace(at,old.size(),value);return text;}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires an existing directory");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto child=parent/("level-control10-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(child))return child;}throw std::runtime_error("No fresh output directory");}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=10;l.applicationVersion=56;l.stepDenominator=4;auto& c=l.character;
    c.advanced=true;c.collision.oneWayPlatforms=true;c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=c.deceleration=D::fromInt(128);c.gravity=c.jumpSpeed=D{};
    DetLevelActor2D hero;hero.id=10;hero.character=true;hero.position={D{},q(1)};hero.body={DetBodyMode2D::Kinematic,{q(1),q(1)},{},{},1,2,false};
    DetLevelActor2D floor;floor.id=30;floor.position={D{},-q(1)};floor.body={DetBodyMode2D::Static,{D::fromInt(128),q(1)},{},{},2,1,false};
    l.actors={hero,floor};l.characterControlOwners={{10,D::fromInt(4),3,2,false,true}};return l;}
DetTickInput frame(unsigned tick){const bool held=tick!=2 && tick!=6 && tick!=10 && tick!=13,right=tick==0 || (tick>=8 && tick!=15);
    return {tick,1,{{1,0,1,{1,static_cast<std::uint8_t>(held),static_cast<std::uint8_t>(right),static_cast<std::uint8_t>(tick==8)}},{1,1,99,{56}}}};}
void configure(DeterministicSession& session,const DetLevel2D& l,const DetLevel2DOptions& options={}){const auto config=detLevelCharacterControlConfig2D(l,options);std::string error;
    const bool installed=configureDetLevel2D(session,l,error,options,[config](DeterministicSession& s){return s.registerSystem(4,config.character.collision.priority,
        [config](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
            if(cmd.type==1 && cmd.payload.size()==4 && cmd.payload[0]<=4 && cmd.payload[1]<=1 && cmd.payload[2]<=1 && cmd.payload[3]<=1){
                DetCharacterControlInput2D input;input.character.targetSpeed=D::fromInt(cmd.payload[0]);input.dashHeld=cmd.payload[1]!=0;
                input.facingRight=cmd.payload[2]!=0;input.cancel=cmd.payload[3]!=0;detCharacterControlInput2D(ctx,config,10,input);}
            else if(cmd.type!=99 || cmd.payload!=Bytes{56})return false;}return true;});});
    require(installed,"Level10 configure: "+error);const bool sealed=session.seal();require(sealed,"Level10 seal: "+session.error());}
void advance(DeterministicSession& s,const DetTickInput& input,const std::string& phase){witness={phase,cp(s),{},{},input};const bool ok=s.advance(input);
    witness.actual=s.checkpoint();require(ok,"Input tick "+std::to_string(input.tick)+": "+s.error());++advanceCalls;}
void equal(DeterministicSession& s,const DetSessionCheckpoint& expected,const std::string& phase){witness.actual=cp(s);witness.expected=expected;
    if(encodeDetCheckpoint(*witness.actual)!=encodeDetCheckpoint(expected)){const auto diff=firstDetDifference(expected,*witness.actual);
        throw std::runtime_error(phase+" nextTick="+std::to_string(expected.nextTick)+(diff?" field="+std::to_string(diff->field):" bytes"));}++comparisons;}
V own(const DetSessionCheckpoint& row,const DetCharacterControl2DConfig& config){const auto schema=detCharacterStateSchema2D(config.character);
    return readDetState<V>(schema,row.actors.at(10).blocks.at(schema.id),DetCharacterVelocity);}
bool sameSchema(const DetTypedStateSchema& a,const DetTypedStateSchema& b){if(a.id!=b.id || a.version!=b.version || a.fields.size()!=b.fields.size())return false;
    for(std::size_t n=0;n<a.fields.size();++n)if(a.fields[n].id!=b.fields[n].id || detStateType(a.fields[n].initial)!=detStateType(b.fields[n].initial)
        || encodeDetStateValue(a.fields[n].initial)!=encodeDetStateValue(b.fields[n].initial))return false;return true;}
DetLevel2D compatibility(const fs::path& legacy){witness={"accepted-level9-byte-compatibility",{},{},{},{}};const auto text=read(legacy);
    // Stage54 installed/accepted examples/detbrain/content.dlt, complete source
    // SHA25620d071e3ebab83a22aad0453b8e669d7d3f95f49c58db85ed30b835d1f19f274.
    // This raw-byte witness is neither a regenerated full CP nor authentication.
    require(text.size()==1007 && hash(text)==UINT64_C(3709426871840008472),"Accepted Level9 complete source-byte fingerprint");
    DetLevel2D old;std::string error;const bool decoded=decodeDetLevel2D(text,old,error);require(decoded,error);
    // The accepted source is physical CRLF; the Stage54 accepted writer emitted LF.
    // Pin that earlier canonical result independently rather than equating the two spellings.
    const auto canonical=encodeDetLevel2D(old);
    require(old.formatVersion==9 && old.characterControlOwners.empty() && canonical.size()==980
        && hash(canonical)==UINT64_C(3155852323544543593)
        && detLevelSessionConfig(old).contentHash==UINT64_C(2553473865409833715),
        "Level9 accepted canonical/semantic fingerprints keep Control defaults absent");
    const auto before=detLevelSessionConfig(old).contentHash;old.characterControlOwners.clear();require(detLevelSessionConfig(old).contentHash==before,"Level9 empty Control adds no semantic bytes");
    auto unused=DetLevel2DOptions{};unused.characterControlInputSchema=340;unused.characterControlStateSchema=341;unused.characterControlPolicySchemaBase=342;unused.characterControlSystemId=16;
    require(encodeDetLevel2D(old,unused)==canonical,"Unused Control registration options do not acquire old-level authority");
    for(unsigned version=1;version<=9;++version){auto plain=scene();plain.formatVersion=version;plain.characterControlOwners.clear();require(validateDetLevel2D(plain,error),"Valid default old format: "+error);
        const auto original=encodeDetLevel2D(plain);const auto identity=detLevelSessionConfig(plain).contentHash;plain.characterControlOwners=scene().characterControlOwners;
        require(!validateDetLevel2D(plain,error) && !error.empty(),"All older formats reject nondefault Control owner table");++rejections;
        plain.characterControlOwners.clear();require(encodeDetLevel2D(plain)==original && detLevelSessionConfig(plain).contentHash==identity,"All old formats keep absent Control bytes unchanged");}
    return old;}
DetLevel2D author(const fs::path& output,const DetLevel2D& legacy){witness={"level10-file-getter-cooked",{},{},{},{}};const auto authored=scene();const auto text=encodeDetLevel2D(authored);
    require(text.ends_with("brains 0\ncontrols 1\ncontrol 10 40800000 3 2 0 1\nend\n"),"Actual strict complete Control author row");write(output/"control10.dlt",text);
    DetLevel2D loaded;std::string error;const bool readLevel=loadDetLevel2D(output/"control10.dlt",loaded,error);require(readLevel,error);require(encodeDetLevel2D(loaded)==text,"Atomic actual file load full contents");
    const auto cfg=detLevelCharacterControlConfig2D(loaded);require(!cfg.respawn && cfg.character.advanced && cfg.owners.size()==1 && cfg.owners[0].actor==10
        && cfg.owners[0].dashSpeed.bits()==D::fromInt(4).bits() && cfg.owners[0].durationTicks==3 && cfg.owners[0].cooldownTicks==2
        && !cfg.owners[0].groundOnly && cfg.owners[0].initialFacingRight && cfg.inputSchema==304 && cfg.stateSchema==305 && cfg.policySchemaBase==306
        && cfg.systemId==15 && cfg.validatorId==79 && cfg.logicProfileId==79,"Actual exact no-HP/no-respawn complete Control getter");
    const auto state=detCharacterControlStateSchemas2D(cfg);const auto policy=detCharacterControlPolicySchemas2D(cfg);
    require(state.size()==2 && state[0].id==304 && state[1].id==305 && state[0].version==1 && state[1].version==1 && !policy.empty() && policy[0].id==306,
        "Actual bounded facade descriptors, no copied fields/chunk-count table");
    const auto cooked=encodeDetLevelCooked2D(loaded);DetLevel2D cookedLevel;const bool decodedCooked=decodeDetLevelCooked2D(cooked,cookedLevel,error);
    require(decodedCooked && encodeDetLevel2D(cookedLevel)==text && detLevelSessionConfig(cookedLevel).contentHash==detLevelSessionConfig(loaded).contentHash,
        "Cooked1 preserves complete Level10 policy identity");write(output/"control10.dlc",cooked);
    const auto refuse=[&](const std::string& value){const bool accepted=decodeDetLevel2D(value,loaded,error);
        require(!accepted && !error.empty() && encodeDetLevel2D(loaded)==text,"Malformed Control source leaves output atomic");++rejections;};
    refuse(replaced(text,"controls 1","controls 9"));refuse(replaced(text,"control 10 40800000","control 10 7fc00000"));
    refuse(replaced(text,"40800000 3 2 0 1","40800000 0 2 0 1"));refuse(replaced(text,"40800000 3 2 0 1","40800000 65536 2 0 1"));
    refuse(replaced(text,"40800000 3 2 0 1","40800000 3 2 0 2"));refuse(replaced(text,"AYDLEVEL 10","AYDLEVEL 9"));
    const auto invalid=[&](const DetLevel2D& l,const DetLevel2DOptions& options=DetLevel2DOptions{}){require(!validateDetLevel2D(l,error,options) && !error.empty(),"Actual role/order/joint-namespace refusal");++rejections;};
    auto bad=authored;bad.actors[0].character=false;invalid(bad);bad=authored;bad.actors[0].body.trigger=true;invalid(bad);
    bad=authored;bad.characterControlOwners[0].actor=999;invalid(bad);bad=authored;bad.characterControlOwners.push_back(bad.characterControlOwners[0]);invalid(bad);
    auto opts=DetLevel2DOptions{};opts.characterControlInputSchema=authored.character.stateSchema;invalid(authored,opts);
    opts={};opts.characterControlPolicySchemaBase=opts.identitySchemaBase;invalid(authored,opts);opts={};opts.characterControlSystemId=authored.character.preSystemId;invalid(authored,opts);
    // Start from the genuinely valid accepted Brain/Attack/HP resource. The new
    // refusal is owner intersection, not missing Brain dependencies or fake IDs.
    bad=legacy;bad.formatVersion=10;bad.characterControlOwners={{20,D::fromInt(4),3,2,false,true}};
    require(!validateDetLevel2D(bad,error) && error.find("Brain/Control")!=std::string::npos,"Actual valid Brain policy rejects shared input owner");++rejections;
    auto remapped=DetLevel2DOptions{};remapped.characterControlInputSchema=340;remapped.characterControlStateSchema=341;remapped.characterControlPolicySchemaBase=342;remapped.characterControlSystemId=16;
    const auto mapped=detLevelCharacterControlConfig2D(authored,remapped);require(mapped.inputSchema==340 && mapped.stateSchema==341 && mapped.policySchemaBase==342 && mapped.systemId==16
        && encodeDetLevel2D(authored,remapped)==text,"Registration IDs are complete runtime policy while author source remains unchanged");
    DeterministicSession mappedSession(detLevelSessionConfig(authored));configure(mappedSession,authored,remapped);
    require(detCharacterControlState2D(cp(mappedSession),mapped,10).phase==Phase::Ready,"Remapped complete descriptor installs and seals");
    return cookedLevel;}
void hand(const DetSessionCheckpoint& row,const DetCharacterControl2DConfig& cfg,unsigned t){const auto state=detCharacterControlState2D(row,cfg,10);
    static constexpr std::array<Phase,16> phases{Phase::Active,Phase::Active,Phase::Active,Phase::Cooldown,Phase::Cooldown,Phase::Ready,Phase::Ready,
        Phase::Active,Phase::Cooldown,Phase::Cooldown,Phase::Cooldown,Phase::Cooldown,Phase::Ready,Phase::Ready,Phase::Active,Phase::Active};
    static constexpr std::array<Outcome,16> outcomes{Outcome::Started,Outcome::Active,Outcome::Active,Outcome::DeniedCooldown,Outcome::Normal,Outcome::Normal,Outcome::Normal,
        Outcome::Started,Outcome::Canceled,Outcome::Normal,Outcome::Normal,Outcome::DeniedCooldown,Outcome::Normal,Outcome::Normal,Outcome::Started,Outcome::Active};
    static constexpr std::array<int,16> quarters{4,8,12,13,14,15,16,12,13,14,15,16,17,18,22,26};
    static constexpr std::array<int,16> speed{4,4,4,1,1,1,1,-4,1,1,1,1,1,1,4,4};
    static constexpr std::array<unsigned,16> requests{1,1,1,2,2,2,2,3,3,3,3,4,4,4,5,5};
    const std::uint64_t start=t<7?0:t<14?7:14;const auto physical=own(row,cfg);
    require(state.phase==phases[t] && state.outcome==outcomes[t] && state.emitted.targetSpeed.bits()==D::fromInt(speed[t]).bits()
        && state.startTick==start && state.endTick==start+3 && state.readyTick==start+5 && state.lockedFacingRight==(t<7 || t>=14),"Hand authored D3/C2 finite window/edge/cancel table");
    require(state.requestCount==requests[t] && state.startCount==(t<7?1u:t<14?2u:3u) && state.deniedCount==(t<3?0u:t<11?1u:2u)
        && state.cancelCount==(t<8?0u:1u) && state.decisionCount==t+1 && state.lastDecisionTick==t,"Hand counts and processing tick");
    require(row.actors.at(10).pose.position[0]==q(quarters[t]).bits() && row.actors.at(10).pose.position[1]==q(1).bits()
        && physical.x.bits()==D::fromInt(speed[t]).bits() && physical.y.bits()==0,"Hand dyadic physical target speed and position via sole collision owner");++handRows;}
void behavior(const fs::path& output,const DetLevel2D& loaded){const auto direct=scene();const auto config=detLevelCharacterControlConfig2D(loaded);
    DeterministicSession baseline(detLevelSessionConfig(direct)),filePath(detLevelSessionConfig(loaded));configure(baseline,direct);configure(filePath,loaded);
    std::vector<DetSessionCheckpoint> rows{cp(baseline)};equal(filePath,rows.front(),"Direct vs file/cooked initial full CP");
    std::string trace="0 "+std::to_string(detCheckpointHash(rows.front()))+"\n";
    for(unsigned tick=0;tick<16;++tick){advance(baseline,frame(tick),"authored-direct-control");rows.push_back(cp(baseline));hand(rows.back(),config,tick);
        advance(filePath,frame(tick),"file-cooked-control");equal(filePath,rows.back(),"Direct vs loaded/cooked full CP");trace+=std::to_string(tick+1)+" "+std::to_string(detCheckpointHash(rows.back()))+"\n";}
    witness={"restore-authored-active-cp2",cp(filePath),{},rows[2],{}};const bool restored=filePath.restore(rows[2]);witness.actual=filePath.checkpoint();require(restored,filePath.error());
    equal(filePath,rows[2],"Active locked direction and remaining deadline restore full CP");
    for(unsigned tick=2;tick<16;++tick){advance(filePath,frame(tick),"restored-authored-control");equal(filePath,rows[tick+1],"Restored complete policy/input continuation");}
    auto corrupt=rows[2];const auto schema=detCharacterControlStateSchemas2D(config)[1];
    writeDetState(schema,corrupt.globals.at(schema.id),detCharacterControlField2D(config,10,DetControlReadyTick),std::uint64_t{4});
    const auto before=bytes(filePath);witness={"atomic-malformed-control-deadline",cp(filePath),{},corrupt,{}};const bool accepted=filePath.restore(corrupt);witness.actual=filePath.checkpoint();
    require(!accepted && bytes(filePath)==before,"Changed finite deadline restore rejected atomically");++rejections;
    auto other=loaded;other.characterControlOwners[0].durationTicks=4;auto recipe=detLevelSessionConfig(loaded);recipe.contentHash=0;
    DeterministicSession original(recipe),changed(recipe);configure(original,loaded);configure(changed,other);const auto unchanged=bytes(changed);
    require(original.manifest()!=changed.manifest() && !changed.restore(cp(original)) && bytes(changed)==unchanged,"Full authored Control duration differs despite equal contentHash");++rejections;
    write(output/"initial.state",encodeDetCheckpoint(rows.front()));write(output/"final.state",encodeDetCheckpoint(rows.back()));write(output/"tick-hashes.txt",trace);
    Bytes frames;for(unsigned tick=0;tick<16;++tick){const auto encoded=encodeDetInput(frame(tick));const auto count=static_cast<std::uint32_t>(encoded.size());
        for(unsigned n=0;n<4;++n)frames.push_back(static_cast<std::uint8_t>(count>>(8*n)));frames.insert(frames.end(),encoded.begin(),encoded.end());}write(output/"canonical-inputs.bin",frames);}
void optionalRespawn(){witness={"complete-optional-respawn-getter-initial",{},{},{},{}};auto level=scene();level.character.collision.characterRespawn=true;
    level.respawnSpawns={{1,10,{D::fromInt(4),q(1)}}};level.respawnBindings={{10,1}};const auto config=detLevelCharacterControlConfig2D(level);
    const auto respawn=detLevelRespawnConfig2D(level);require(config.respawn && config.respawn->stateSchema==respawn.stateSchema && config.respawn->policySchema==respawn.policySchema
        && config.respawn->validatorId==respawn.validatorId && config.respawn->logicProfileId==respawn.logicProfileId
        && config.respawn->spawns.size()==1 && config.respawn->spawns[0].id==1 && config.respawn->spawns[0].actor==10
        && config.respawn->spawns[0].position.x.bits()==D::fromInt(4).bits() && config.respawn->spawns[0].position.y.bits()==q(1).bits()
        && sameSchema(detCharacterPolicySchema2D(config.respawn->character),detCharacterPolicySchema2D(respawn.character)),
        "Optional prepared Respawn full typed Character policy/spawn authority is resolved without HP recovery");
    DeterministicSession session(detLevelSessionConfig(level));configure(session,level);const auto initial=cp(session);const auto state=detCharacterControlState2D(initial,config,10);
    require(state.phase==Phase::Ready && !state.startCount && !state.observedAccepted && detCharacterRespawnState2D(initial,10,respawn).result==DetRespawnResult2D::None,
        "Actual combined Respawn/Flow/Control initial seal and default copied state");const auto before=encodeDetCheckpoint(initial);
    const bool restored=session.restore(initial);require(restored && bytes(session)==before,"Combined initial full CP restore");++comparisons;
    // Runtime accepted/occupied cancellation is already a Stage55 core fixture;
    // authoring does not duplicate that movement or claim a second execution here.
}
std::string jsonQuote(const std::string& text){std::string out="\"";for(char ch:text){if(ch=='\"' || ch=='\\')out+='\\';if(ch=='\n')out+="\\n";
    else if(ch=='\r')out+="\\r";else if(ch=='\t')out+="\\t";else if(static_cast<unsigned char>(ch)>=0x20)out+=ch;}return out+'\"';}
void failure(const fs::path& output,const std::string& reason){if(output.empty())return;if(witness.before)write(output/"failure-before.state",encodeDetCheckpoint(*witness.before));
    if(witness.actual)write(output/"failure-actual.state",encodeDetCheckpoint(*witness.actual));if(witness.expected)write(output/"failure-expected.state",encodeDetCheckpoint(*witness.expected));
    if(witness.input)write(output/"failure-canonical-input.bin",encodeDetInput(*witness.input));write(output/"failure.json","{\"status\":\"failed\",\"phase\":"+jsonQuote(witness.phase)+",\"reason\":"+jsonQuote(reason)+"}\n");}
} // namespace
int main(int argc,char** argv){fs::path output;try{fs::path parent,legacy;for(int n=1;n<argc;++n){const std::string option=argv[n];
    if(option=="--output" && n+1<argc && parent.empty())parent=argv[++n];else if(option=="--legacy" && n+1<argc && legacy.empty())legacy=argv[++n];
    else throw std::invalid_argument("Usage: AYEntity_LevelCharacterControl10Checks --output EXISTING_PARENT --legacy ACCEPTED_LEVEL9.dlt");}
    require(!legacy.empty(),"Actual accepted Level9 resource required");output=fresh(parent);const auto old=compatibility(legacy);const auto level=author(output,old);behavior(output,level);optionalRespawn();
    write(output/"summary.json","{\"status\":\"passed\",\"levelProfile\":10,\"controlProfile\":1,\"ticks\":16,\"cpRows\":17,\"handRows\":\""+std::to_string(handRows)
        +"\",\"advanceCalls\":\""+std::to_string(advanceCalls)+"\",\"cpComparisons\":\""+std::to_string(comparisons)+"\",\"rejections\":\""+std::to_string(rejections)
        +"\",\"oracleScope\":\"selected authored finite Control windows/counts/positions; full CP direct-file-cooked/restore execution equality; optional Respawn initial binding only\"}\n");
    std::cout<<"PASS Level10 file/getter/cooked;16tick selected hand+active restore; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& error){try{failure(output,error.what());}catch(const std::exception& logging){std::cerr<<"Witness failure: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Level10 authoring: "<<error.what()<<'\n';return 1;}}
