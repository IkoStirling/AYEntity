// design reference: AYEntity/design.md Stage49 bounded periodic content.
// HP/clock rows below are hand-derived. Full canonical CP comparisons establish
// execution-path equality, not independently generated all-byte state or authentication.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
unsigned cases=0,rejects=0,comparisons=0;
void require(bool condition,const std::string& why){if(!condition)throw std::runtime_error(why);}
DetSessionCheckpoint cp(DeterministicSession& s){auto value=s.checkpoint();require(bool(value),s.error());return *value;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
void advance(DeterministicSession& s,const DetTickInput& i){const bool advanced=s.advance(i);require(advanced,"tick "+std::to_string(i.tick)+": "+s.error());}
DetLevelActor2D actor(SimEntityId id,bool trigger,V center,V half){DetLevelActor2D a;a.id=id;a.position=center;
    a.body={trigger?DetBodyMode2D::Static:DetBodyMode2D::Kinematic,half,{},{},trigger?2u:1u,trigger?1u:2u,trigger};a.character=!trigger;return a;}
DetLevel2D scene(){DetLevel2D l;l.formatVersion=6;l.applicationVersion=49;l.stepDenominator=4;
    auto& c=l.character;c.collision.extended=true;c.collision.oneWayPlatforms=true;c.advanced=true;
    c.maxHorizontalSpeed=D::fromInt(16);c.acceleration=D::fromInt(128);c.deceleration=D::fromInt(128);c.gravity={};c.jumpSpeed={};
    l.actors={actor(10,false,{},{D::fromInt(1)/D::fromInt(4),D::fromInt(1)/D::fromInt(4)}),actor(20,true,{},{D::fromInt(1),D::fromInt(1)})};
    l.vitalityActors={{10,201,10,10,0,0,false}};l.vitalityRegions={{1,20,10,DetVitalityKind2D::Hurt,2,3}};
    l.periodicVitalityRegions=true;return l;}
DetTickInput frame(std::uint64_t tick,int speed=0,bool reverse=false){DetTickInput i{tick,1,{{1,0,1,{static_cast<std::uint8_t>(speed<0?-speed:speed),static_cast<std::uint8_t>(speed<0)}},{1,1,99,{49}}}};
    if(reverse)std::reverse(i.commands.begin(),i.commands.end());return i;}
bool inputSystem(DeterministicSession& s,const DetLevel2D& l){return s.registerSystem(4,l.character.collision.priority,[character=l.character](DetTickContext& ctx){
    for(const auto& command:ctx.input().commands){if(command.source!=1)return false;
        if(command.type==1 && command.payload.size()==2){auto speed=D::fromUInt(command.payload[0]);if(command.payload[1]>1)return false;if(command.payload[1])speed=-speed;
            detCharacterInput2D(ctx,10,character,{speed,false,false,false});}
        else if(command.type!=99 || command.payload!=Bytes{49})return false;}return true;});}
void configure(DeterministicSession& s,const DetLevel2D& l,bool reverse=false){std::string error;if(!reverse)require(inputSystem(s,l),s.error());
    const bool installed=configureDetLevel2D(s,l,error);require(installed,"Level configuration: "+error);if(reverse)require(inputSystem(s,l),s.error());const bool sealed=s.seal();require(sealed,"seal: "+s.error());}
std::string read(const fs::path& file){std::ifstream in(file,std::ios::binary);require(bool(in),"open sealed old5 input");
    std::string text((std::istreambuf_iterator<char>(in)),{});require(text.size()<=kDetLevel2DMaxTextBytes,"legacy text budget");return text;}
void write(const fs::path& file,const std::string& text){std::ofstream out(file,std::ios::binary);out<<text;out.close();require(bool(out),"fresh evidence write");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"existing output parent required");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto file=parent/("level-vitality6-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(file))return file;}throw std::runtime_error("fresh directory unavailable");}
std::string replace(std::string s,const std::string& before,const std::string& after){const auto at=s.find(before);require(at!=std::string::npos,"draft fixture token");s.replace(at,before.size(),after);return s;}
// Fixed format5 fixture copied byte-for-byte from the completed Stage48 gate.
// Content hash379386434137296218; source file SHA256
// 900d2dd7fa9ebafc5e805c2385f8aa8625a08f2e696022ad8a6d471d1fbc32a2.
void legacy(const fs::path& file){const auto text=read(file);DetLevel2D l;std::string error;require(decodeDetLevel2D(text,l,error),error);
    require(l.formatVersion==5 && detLevelSessionConfig(l).contentHash==UINT64_C(379386434137296218) && encodeDetLevel2D(l)==text,"actual sealed old5 canonical bytes unchanged");
    const auto v=detLevelVitalityConfig2D(l);require(!v.periodicRegions && v.regionStateSchema==224,"old5 periodic options default only");
    const auto state=detVitalityStateSchemas2D(v);require(state.front().version==(v.respawnRecovery?2u:1u),"old5 selected Vitality1/2 unchanged");
    for(const auto& s:state)require(s.id!=224,"old5 installs no region schema");
    DeterministicSession runtime(detLevelSessionConfig(l));std::string setup;require(configureDetLevel2D(runtime,l,setup),setup);require(runtime.seal(),runtime.error());
    require(!cp(runtime).globals.contains(224),"old5 actual region authority absent");++cases;}
void authoring(const fs::path& dir){auto l=scene();const auto text=encodeDetLevel2D(l);DetLevel2D out;std::string error;require(decodeDetLevel2D(text,out,error),error);
    require(encodeDetLevel2D(out)==text && text.find("vitality 32 0 1 1 0 1")!=std::string::npos
        && text.find("vitalityregion 1 20 10 1 2 3")!=std::string::npos,"exact Level6 fields");
    const auto refused=[&](const std::string& bad){require(!decodeDetLevel2D(bad,out,error) && !error.empty() && encodeDetLevel2D(out)==text,"atomic bounded parser rejection");++rejects;};
    refused(replace(text,"AYDLEVEL 6","AYDLEVEL 7"));refused(replace(text,"vitality 32 0 1 1 0 1","vitality 32 0 2 1 0 1"));
    refused(replace(text,"vitalityregion 1 20 10 1 2 3","vitalityregion 1 20 10 1 2 65536"));
    refused(replace(text,"vitality 32 0 1 1 0 1","vitality 32 0 0 1 0 1"));refused(text+"unknown");
    const auto invalid=[&](const DetLevel2D& bad,const DetLevel2DOptions& options=DetLevel2DOptions{}){require(!validateDetLevel2D(bad,error,options),"semantic periodic rejection");++rejects;};
    auto bad=l;bad.formatVersion=5;invalid(bad);bad=l;bad.vitalityRegions[0].periodTicks=65536;invalid(bad);auto options=DetLevel2DOptions{};
    options.vitalityRegionStateSchema=192;invalid(l,options);options={};options.vitalityRegionStateSchema=UINT32_MAX;invalid(l,options);
    bad=l;bad.periodicVitalityRegions=false;bad.vitalityRegions[0].periodTicks=0;require(validateDetLevel2D(bad,error),error);
    require(detVitalityStateSchemas2D(detLevelVitalityConfig2D(bad))[0].version==1,"Level6 explicit false still uses Vitality1");
    const auto cooked=encodeDetLevelCooked2D(l);DetLevel2D decoded;require(decodeDetLevelCooked2D(cooked,decoded,error) && encodeDetLevel2D(decoded)==text,"same cooked envelope1");
    write(dir/"periodic.dlt",text);++cases;}
void hand(const DetSessionCheckpoint& checkpoint,const DetVitality2DConfig& v,unsigned tick){const unsigned attempts=tick<1?0u:tick<4?1u:tick<7?2u:3u;
    const auto actor=detVitalityState2D(checkpoint,v,10);const auto region=detVitalityRegionState2D(checkpoint,v,1);
    require(actor.hp==10-2*attempts && region.attemptCount==attempts && region.active==(tick>=1)
        && region.episodeCount==(tick>=1?1u:0u),"hand HP10,8,6,4 from processing1/4/7 only");
    if(attempts)require(region.lastAttemptTick==1+3*(attempts-1) && region.nextEligible==1+3*attempts,"exact logical interval clock");}
void closure(const fs::path& dir){const auto l=scene();const auto v=detLevelVitalityConfig2D(l);const auto state=detVitalityStateSchemas2D(v);
    require(state.size()==2 && state[0].version==kDetVitality2DPeriodicProfileVersion && state[1].id==224 && state[1].version==3,"actual primary3 plus bounded region3");
    DeterministicSession a(detLevelSessionConfig(l)),b(detLevelSessionConfig(l));configure(a,l);configure(b,l,true);std::vector<DetSessionCheckpoint> checkpoints{cp(a)};
    std::vector<Bytes> states{bytes(a)};std::vector<DetConfirmedEvent> events;for(unsigned t=0;t<8;++t){advance(a,frame(t,t==3?4:0));advance(b,frame(t,t==3?4:0,true));
        require(bytes(a)==bytes(b),"full reverse registration/input canonical state");++comparisons;const auto row=cp(a);hand(row,v,t);checkpoints.push_back(row);states.push_back(encodeDetCheckpoint(row));
        for(const auto& event:row.pendingEvents)events.push_back({1,t,event});}
    require(a.restore(checkpoints[2]),a.error());for(unsigned t=2;t<8;++t){advance(a,frame(t,t==3?4:0));require(bytes(a)==states[t+1],"restore pending deadline complete state");++comparisons;}
    DeterministicSession corrected(detLevelSessionConfig(l));configure(corrected,l);DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};
    DeterministicRollback rollback(corrected,rc);for(unsigned t=0;t<8;++t){const auto i=frame(t,t==3?4:0);if(t==3){require(rollback.advance(),rollback.error());require(rollback.submit(1,i),rollback.error());}
        else require(rollback.submit(1,i) && rollback.advance(),rollback.error());require(bytes(corrected)==states[t+1],"real late motion restores all clocks");++comparisons;}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"confirmed effects exactly once");
    DeterministicSession recording(detLevelSessionConfig(l));configure(recording,l);DetReplayWriter writer;require(writer.begin(recording,(dir/"periodic.rpl").string(),2),writer.error());
    for(unsigned t=0;t<8;++t){require(writer.advance(frame(t,t==3?4:0)),writer.error());require(bytes(recording)==states[t+1],"actual recorded complete state");++comparisons;}
    require(writer.finish(),writer.error());DeterministicSession playback(detLevelSessionConfig(l));configure(playback,l);DetReplayReader reader;require(reader.open(writer.path()),reader.error());
    require(reader.restoreInitial(playback),reader.error());while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"real rpl complete state");++comparisons;}
    require(reader.seek(playback,2) && bytes(playback)==states[2],"seek pending deadline");++comparisons;require(reader.seek(playback,8) && bytes(playback)==states[8],"seek final deadline");++comparisons;
    auto changed=l;changed.vitalityRegions[0].periodTicks=4;auto recipe=detLevelSessionConfig(l);recipe.contentHash=0;DeterministicSession original(recipe),foreign(recipe);configure(original,l);configure(foreign,changed);
    const auto before=bytes(foreign);require(original.manifest()!=foreign.manifest() && !foreign.restore(cp(original)) && bytes(foreign)==before,"full interval identity even same contentHash");
    const auto prior=bytes(a);for(unsigned n=0;n<3;++n){auto broken=checkpoints[2];if(n==0)writeDetState(state[1],broken.globals.at(v.regionStateSchema),
        detVitalityRegionField2D(v,1,DetVitalityRegionNextEligible),std::uint64_t{5});else if(n==1)writeDetState(state[1],broken.globals.at(v.regionStateSchema),
        detVitalityRegionField2D(v,1,DetVitalityRegionAttempts),std::uint64_t{2});else broken.globals.at(v.policySchemaBase).at(3)^=1;
        require(!a.restore(broken) && bytes(a)==prior,"deadline/count/literal periodic policy atomic refusal");}++cases;}
void contactTransitions(){auto l=scene();l.actors[0].position.x=D::fromInt(-2);l.actors[1].body.half.x=D::fromInt(1)/D::fromInt(2);const auto v=detLevelVitalityConfig2D(l);
    DeterministicSession s(detLevelSessionConfig(l));configure(s,l);for(unsigned t=0;t<12;++t){advance(s,frame(t,t<5?4:t<8?-4:0));const auto row=cp(s);
        const auto region=detVitalityRegionState2D(row,v,1);const unsigned count=t<2?0u:t<8?1u:t<11?2u:3u;
        require(detVitalityState2D(row,v,10).hp==10-2*count && region.attemptCount==count,"real exit and reentry hand HP");
        if(t>=3 && t<8)require(!region.active && !region.nextEligible,"Exit clears timer without retry");if(t>=8)require(region.episodeCount==2,"real reentry starts another episode");}
    auto transient=scene();transient.actors[0].position.x=D::fromInt(-2);transient.actors[1].body.half.x=D::fromInt(1)/D::fromInt(2);const auto c=detLevelVitalityConfig2D(transient);
    DeterministicSession crossing(detLevelSessionConfig(transient));configure(crossing,transient);for(unsigned t=0;t<5;++t){advance(crossing,frame(t,t?0:16));const auto row=cp(crossing);
        if(t){const auto r=detVitalityRegionState2D(row,c,1);require(r.attemptCount==1 && !r.active && !r.nextEligible && detVitalityState2D(row,c,10).hp==8,"transient Enter+Exit one attempt no timer");}}
    ++cases;}
void budget(){auto l=scene();l.maxVitalityOperationsPerTick=1;l.actors.push_back(actor(30,true,{},{D::fromInt(1),D::fromInt(1)}));
    l.vitalityRegions.push_back({2,30,10,DetVitalityKind2D::Hurt,2,3});const auto v=detLevelVitalityConfig2D(l);DeterministicSession s(detLevelSessionConfig(l));configure(s,l);
    for(unsigned t=0;t<5;++t){advance(s,frame(t));const auto row=cp(s);const unsigned attempts=t<1?0u:t<4?1u:2u;
        require(detVitalityState2D(row,v,10).hp==10-2*attempts,"only first region's accepted budget opportunity changes HP");
        for(std::uint32_t id:{1u,2u}){const auto r=detVitalityRegionState2D(row,v,id);require(r.attemptCount==attempts,"budget denied interval is an attempt");
            if(attempts)require(r.nextEligible==1+3*attempts,"budget denial consumes exactly authored interval");}
        if(t==1 || t==4)require(detVitalityResult2D(row,v).code==DetVitalityCode2D::BudgetExceeded,"canonical second region denial");}
    ++cases;}
} // namespace
int main(int argc,char** argv){try{fs::path parent,old;for(int n=1;n<argc;++n){const std::string arg=argv[n];if((arg=="--output" || arg=="--legacy") && n+1<argc){
        if(arg=="--output")parent=argv[++n];else old=argv[++n];}else throw std::invalid_argument("--output existingParent --legacy canonicalStage48Format5File");}
    require(!parent.empty() && !old.empty(),"output and actual canonical old5 required");const auto dir=fresh(parent);legacy(old);authoring(dir);closure(dir);contactTransitions();budget();
    const auto summary=std::string("{\"schema\":1,\"status\":\"passed\",\"cases\":")+std::to_string(cases)+",\"rejects\":"+std::to_string(rejects)
        +",\"completeCheckpointComparisons\":"+std::to_string(comparisons)+",\"oracleScope\":\"selected hand HP/region clocks; full CP execution equality\"}\n";
    write(dir/"summary.json",summary);std::cout<<summary;return 0;}catch(const std::exception& e){std::cerr<<"FAIL levelvitality6: "<<e.what()<<'\n';return 1;}}
