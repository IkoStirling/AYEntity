// design reference: AYEntity/design.md Stage53 bounded registered enemy decisions.
// Hand tables cover selected decisions/HP/positions; whole CP comparisons prove execution equality only.
#include <AYEntity/DeterministicBrain2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
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
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why.empty()?"Unspecified setup or execution failure":why);}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
std::uint64_t advances=0,handRows=0,cpComparisons=0,negativeRestores=0,negativeConfigs=0,confirmedEvents=0,rollbacks=0,replayedTicks=0;
DetSessionConfig sessionConfig(){return {53,1,1,4,53};}
DetSessionCheckpoint checkpoint(DeterministicSession& s){const auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,const DetTickInput& in,const std::string& phase){
    witness={phase,checkpoint(s),{},{},in};const bool ok=s.advance(in);witness.actual=s.checkpoint();require(ok,s.error());++advances;}
void equalCP(DeterministicSession& s,const DetSessionCheckpoint& expected,const std::string& why){
    witness.expected=expected;witness.actual=checkpoint(s);require(encodeDetCheckpoint(*witness.actual)==encodeDetCheckpoint(expected),why);++cpComparisons;}
DetBrain2DConfig policy(){DetBrain2DConfig c;auto& character=c.character;character.advanced=true;character.collision.oneWayPlatforms=true;
    character.maxHorizontalSpeed=D::fromInt(4);character.acceleration=character.deceleration=D::fromInt(128);
    character.gravity=character.jumpSpeed=D{};auto& v=c.attack.vitality;v.progress.stateSchema=176;v.progress.policySchemaBase=177;
    v.progress.validatorId=v.progress.logicProfileId=76;v.progress.actions.collision=character.collision;v.progress.actions.targets.clear();
    v.progress.counters={{1,0,20,20},{2,0,20,20}};v.actors={{10,1,0},{20,2,0}};
    v.permissions={{1,20,DetVitalityKind2D::Hurt,3},{2,10,DetVitalityKind2D::Hurt,20}};
    c.attack.owners={{10,{q(2),q(2)},{q(2),D{}},1,1,1,{{20,1,3}}}};
    DetBrainOwner2D owner;owner.actor=10;owner.target=20;owner.patrolLeft=D::fromInt(-2);owner.patrolRight=D::fromInt(2);
    owner.patrolSpeed=D::fromInt(1);owner.chaseSpeed=D::fromInt(2);owner.senseHalfExtent={D::fromInt(4),D::fromInt(1)};owner.attackDistance=D::fromInt(1);
    c.owners={owner};return c;}
struct Geometry {D ownerX{},targetX=D::fromInt(2);};
DetActorState actor(const DetBrain2DConfig& c,SimEntityId id,Geometry geometry){DetActorState a;V center,half;std::uint32_t layer,mask;bool trigger=false;
    if(id==10){center={geometry.ownerX,q(1)};half={q(1),q(1)};layer=1;mask=14;}
    else if(id==20){center={geometry.targetX,q(1)};half={q(1),q(1)};layer=2;mask=5;}
    else if(id==30){center={D{},-q(1)};half={D::fromInt(128),q(1)};layer=4;mask=3;}
    else if(id==40){center={D::fromInt(1),q(1)};half={D::fromInt(4),D::fromInt(1)};layer=8;mask=1;trigger=true;}
    else throw std::invalid_argument("Unknown fixture actor");
    DetSimTransformComponent pose;require(pose.setPosition({center.x,center.y,D{}}),"Author fixture pose");a.pose=pose.snapshot();
    a.blocks[c.character.collision.bodySchema]=detCollisionBodyState2D(c.character.collision,
        {id<30?DetBodyMode2D::Kinematic:DetBodyMode2D::Static,half,V{},V{},layer,mask,trigger});
    if(id<30){DetCharacterState2D state;state.enabled=true;a.blocks[c.character.stateSchema]=detCharacterState2D(c.character,state);}return a;}
bool registerInput(DeterministicSession& s,const DetBrain2DConfig& c){return s.registerSystem(4,c.character.collision.priority,[c](DetTickContext& ctx){
    for(const auto& command:ctx.input().commands){if(command.source!=1)return false;
        if(command.type==1 && command.payload.size()==1 && command.payload[0]<=1){const auto ids=ctx.entities();
            if(std::binary_search(ids.begin(),ids.end(),SimEntityId{20}))detCharacterInput2D(ctx,20,c.character,{D::fromInt(command.payload[0]),false,false,false});}
        else if(command.type==2 && command.payload.size()==1 && (command.payload[0]==10 || command.payload[0]==20))ctx.despawn(command.payload[0]);
        else if(command.type==3 && command.payload==Bytes{0})detCharacterInput2D(ctx,10,c.character,{D::fromInt(2),false,false,false});
        else if(command.type==3 && command.payload==Bytes{1})detSetAttackInput2D(ctx,c.attack,10,{true,true});
        else if(command.type==99 && command.payload==Bytes{53})continue;else return false;}return true;});}
bool configure(DeterministicSession& s,const DetBrain2DConfig& c,bool reverse=false,Geometry geometry={}){
    witness={"configuration",{},{},{},{}};if(reverse && !registerInput(s,c))return false;
    if(!installDetActions2D(s,c.attack.vitality.progress.actions) || !installDetProgress2D(s,c.attack.vitality.progress)
        || !installDetCharacterController2D(s,c.character) || !installDetVitality2D(s,c.attack.vitality)
        || !installDetAttack2D(s,c.attack) || !installDetBrain2D(s,c))return false;
    if(!reverse && !registerInput(s,c))return false;std::vector<SimEntityId> ids{10,20,30,40};if(reverse)std::reverse(ids.begin(),ids.end());
    for(auto id:ids)if(!s.addEntity(id,actor(c,id,geometry)))return false;return s.seal();}
DetTickInput frame(std::uint64_t tick,bool moving=false){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(moving)}},{1,1,99,{53}}}};}
std::vector<DetTickInput> inputs(){std::vector<DetTickInput> out;for(unsigned t=0;t<16;++t)out.push_back(frame(t,t==8));return out;}
void extra(DetTickInput& in,std::uint32_t type,std::uint8_t data){in.commands.push_back({1,static_cast<std::uint32_t>(in.commands.size()),type,{data}});}
D positionX(const DetSessionCheckpoint& cp,SimEntityId id){return D::fromBits(cp.actors.at(id).pose.position[0]);}
void golden(DeterministicSession& s,const DetBrain2DConfig& c,unsigned t){const auto cp=checkpoint(s);const auto brain=detBrainState2D(cp,c,10);
    const auto attack=detAttackState2D(cp,c.attack,10);const auto result=detAttackResult2D(cp,c.attack,10);const auto hp=detVitalityState2D(cp,c.attack.vitality,20);
    // An explicit table derived from dt=1/4, chase speed2, distance1 and W1/A1/R1.
    // It deliberately distinguishes the previous Attack phase observed by Brain3
    // from the current phase subsequently processed by Attack5.
    using M=DetBrainMode2D;using P=DetAttackPhase2D;
    static constexpr std::array<M,16> modes{M::Chase,M::Chase,M::AttackRequested,M::Busy,M::Busy,M::Busy,M::AttackRequested,M::Busy,
        M::Busy,M::Busy,M::Chase,M::AttackRequested,M::Busy,M::Busy,M::Busy,M::AttackRequested};
    static constexpr std::array<P,16> observed{P::Idle,P::Idle,P::Idle,P::Warmup,P::Active,P::Recovery,P::Idle,P::Warmup,
        P::Active,P::Recovery,P::Idle,P::Idle,P::Warmup,P::Active,P::Recovery,P::Idle};
    static constexpr std::array<P,16> current{P::Idle,P::Idle,P::Warmup,P::Active,P::Recovery,P::Idle,P::Warmup,P::Active,
        P::Recovery,P::Idle,P::Idle,P::Warmup,P::Active,P::Recovery,P::Idle,P::Warmup};
    static constexpr std::array<int,16> ownerQuarters{2,4,4,4,4,4,4,4,4,4,6,6,6,6,6,6};
    static constexpr std::array<unsigned,16> pulses{0,0,1,1,1,1,2,2,2,2,2,3,3,3,3,4};
    static constexpr std::array<unsigned,16> health{20,20,20,17,17,17,17,14,14,14,14,14,11,11,11,11};
    const bool chase=t==0 || t==1 || t==10;const bool pulse=t==2 || t==6 || t==11 || t==15;
    require(brain.mode==modes[t] && brain.observedAttackPhase==observed[t] && attack.phase==current[t],"Hand Brain/previous Attack/current Attack table tick="+std::to_string(t));
    require(brain.desiredSpeed.bits()==(chase?D::fromInt(2):D{}).bits() && brain.desiredAttackHeld==pulse
        && brain.attackPulseCount==pulses[t] && attack.attackCount==pulses[t],"Hand chase intent and held edge pulses2/6/11/15");
    require(brain.decisionCount==t+1 && brain.lastDecisionTick==t && brain.observedAttackProcessedTick==t
        && brain.observedOwnerAlive && brain.observedTargetPresent && brain.observedTargetAlive,"Hand processed observations and actual alive snapshots");
    require(brain.observedOwnerPosition.x.bits()==(t?q(ownerQuarters[t-1]):D{}).bits()
        && brain.observedTargetPosition.x.bits()==q(t<=8?8:9).bits(),"Brain observes pre-player/pre-solver positions, not a later capture");
    require(positionX(cp,10).bits()==q(ownerQuarters[t]).bits() && positionX(cp,20).bits()==q(t<8?8:9).bits()
        && D::fromBits(cp.actors.at(10).pose.position[1]).bits()==q(1).bits(),"Dyadic chase and target motion physical positions");
    require(hp.hp==health[t] && detVitalityState2D(cp,c.attack.vitality,10).hp==20
        && result.attemptCount==(t<3?0u:(t<7?1u:(t<12?2u:3u))),"Hand actual Hurt only3/7/12, no owner damage");
    if(t==3 || t==7 || t==12)require(result.tick==t && result.target==20 && result.actual==3 && result.code==DetVitalityCode2D::Applied,"Literal permission actual active hit ledger");
    ++handRows;}
void selected(){auto c=policy();auto patrol=c;patrol.owners[0].patrolLeft=-q(1);patrol.owners[0].patrolRight=q(1);
    DeterministicSession p(sessionConfig());require(configure(p,patrol,false,{D{},D::fromInt(100)}),p.error());
    static constexpr std::array<int,6> quarters{1,0,-1,0,1,0};static constexpr std::array<bool,6> right{true,false,false,true,true,false};
    for(unsigned t=0;t<6;++t){advance(p,frame(t),"selected-patrol");const auto cp=checkpoint(p);const auto b=detBrainState2D(cp,patrol,10);
        require(b.mode==DetBrainMode2D::Patrol && b.patrolRight==right[t] && b.facingRight==right[t] && !b.desiredAttackHeld
            && b.desiredSpeed.bits()==D::fromInt(right[t]?1:-1).bits() && positionX(cp,10).bits()==q(quarters[t]).bits(),"Hand inclusive patrol-bound direction change, no pose clamp");++handRows;}
    auto slow=c;slow.character.deceleration=D::fromInt(1);DeterministicSession coast(sessionConfig());require(configure(coast,slow),coast.error());
    for(unsigned t=0;t<3;++t)advance(coast,frame(t),"selected-real-deceleration");const auto cp=checkpoint(coast);
    require(detBrainState2D(cp,slow,10).desiredSpeed.bits()==0
        && readDetState<V>(detCharacterStateSchema2D(slow.character),cp.actors.at(10).blocks.at(slow.character.stateSchema),DetCharacterVelocity).x.bits()==q(7).bits()
        && positionX(cp,10).bits()==(D::fromInt(23)/D::fromInt(16)).bits(),"Zero Brain intent uses controller deceleration, not instant physical freeze");++handRows;
    std::cout<<"PASS selected patrol and real controller deceleration\n";}
void absence(){for(bool ownerDead:{false,true}){auto c=policy();c.attack.vitality.progress.counters[ownerDead?0:1].initial=0;
    DeterministicSession s(sessionConfig());require(configure(s,c),s.error());advance(s,frame(0),"selected-initial-dead");const auto cp=checkpoint(s);const auto b=detBrainState2D(cp,c,10);
    require(b.mode==(ownerDead?DetBrainMode2D::Dead:DetBrainMode2D::Patrol) && !b.desiredAttackHeld && !b.attackPulseCount
        && b.observedOwnerAlive!=ownerDead && b.observedTargetAlive==ownerDead,"Actual initial dead owner stops; dead target causes patrol");++handRows;}
    for(auto retired:{SimEntityId{10},SimEntityId{20}}){auto c=policy();DeterministicSession s(sessionConfig());require(configure(s,c),s.error());
        auto in=frame(0);extra(in,2,static_cast<std::uint8_t>(retired));advance(s,in,"selected-queued-retirement");advance(s,frame(1),"selected-retired-next-decision");
        const auto cp=checkpoint(s);const auto b=detBrainState2D(cp,c,10);require(!cp.actors.contains(retired) && b.decisionCount==2
            && b.mode==(retired==10?DetBrainMode2D::Retired:DetBrainMode2D::Patrol) && !b.desiredAttackHeld,"Retired stable IDs release inputs without querying a missing character");
        require(retired==10?!b.observedOwnerAlive:!b.observedTargetPresent,"Copied retirement provenance");++handRows;}
    std::cout<<"PASS initial dead and real structural retirement without hidden decisions\n";}
void saveBytes(const fs::path& path,const Bytes& data){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    f.close();require(bool(f),"Write "+path.filename().string());}
void closure(const fs::path& output){const auto c=policy();const auto frames=inputs();DeterministicSession base(sessionConfig());require(configure(base,c),base.error());
    std::vector<DetSessionCheckpoint> rows{checkpoint(base)};std::vector<DetConfirmedEvent> events;
    for(unsigned t=0;t<16;++t){advance(base,frames[t],"closure-baseline");golden(base,c,t);rows.push_back(checkpoint(base));
        for(const auto& event:rows.back().pendingEvents)events.push_back({1,t,event});}
    require(!events.empty(),"Confirmed-events lane contains real collision events");confirmedEvents=events.size();
    DeterministicSession reverse(sessionConfig());require(configure(reverse,c,true),reverse.error());equalCP(reverse,rows[0],"Reverse initial full CP");
    for(unsigned t=0;t<16;++t){auto in=frames[t];std::reverse(in.commands.begin(),in.commands.end());advance(reverse,in,"closure-reverse");equalCP(reverse,rows[t+1],"Reverse full CP");}
    DeterministicSession restored(sessionConfig());require(configure(restored,c,true),restored.error());const bool loaded=restored.restore(rows[4]);require(loaded,restored.error());
    equalCP(restored,rows[4],"Restore first consumed hit full CP");for(unsigned t=4;t<16;++t){advance(restored,frames[t],"closure-restored");equalCP(restored,rows[t+1],"Restored full CP continuation");}
    DeterministicSession corrected(sessionConfig());require(configure(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;
    // The player speed is a complete held sample. Metadata and structural one-shots
    // are Omit. The actual late tick8 changes target motion and later Brain choices.
    rc.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Omit},{3,DetPredictionMode::Omit},{99,DetPredictionMode::Omit}};DeterministicRollback rb(corrected,rc);
    for(unsigned t=0;t<16;++t){if(t==8){witness={"closure-predicted-advance",checkpoint(corrected),{},rows[t+1],{}};
            const bool predicted=rb.advance();witness.actual=corrected.checkpoint();require(predicted,rb.error());
            witness={"closure-late-member-submit",checkpoint(corrected),{},rows[t+1],frames[t]};const bool submitted=rb.submit(1,frames[t]);witness.actual=corrected.checkpoint();require(submitted,rb.error());}
        else {witness={"closure-member-submit",checkpoint(corrected),{},rows[t+1],frames[t]};const bool submitted=rb.submit(1,frames[t]);witness.actual=corrected.checkpoint();require(submitted,rb.error());
            witness={"closure-confirmed-advance",checkpoint(corrected),{},rows[t+1],frames[t]};const bool moved=rb.advance();witness.actual=corrected.checkpoint();require(moved,rb.error());}
        ++advances;equalCP(corrected,rows[t+1],"Real late player motion correction full CP");}
    rollbacks=rb.rollbackCount();replayedTicks=rb.replayedTicks();require(rollbacks>0 && replayedTicks>0,"Late tick8 performs actual rollback/replay");
    require(rb.takeConfirmedEvents()==events && rb.takeConfirmedEvents().empty(),"Real confirmed trigger effects drain exactly once");
    DeterministicSession recording(sessionConfig());require(configure(recording,c),recording.error());DetReplayWriter writer;
    const std::string requestedPath=(output/"brain.rpl").string();const bool began=writer.begin(recording,requestedPath,2);require(began,writer.error());
    for(unsigned t=0;t<16;++t){witness={"closure-rpl-record",checkpoint(recording),{},rows[t+1],frames[t]};const bool moved=writer.advance(frames[t]);
        witness.actual=recording.checkpoint();require(moved,writer.error());++advances;equalCP(recording,rows[t+1],"Actual RPL recorder full CP");}
    const bool finished=writer.finish();require(finished,writer.error());const std::string recordedPath=writer.path();
    DeterministicSession playback(sessionConfig());require(configure(playback,c,true),playback.error());DetReplayReader reader;
    // open/begin replace an implementation: read error only after the mutation.
    // Keep the Foundation physical _000 path alive through all subsequent reads.
    const bool opened=reader.open(recordedPath);require(opened,reader.error());const bool initialized=reader.restoreInitial(playback);require(initialized,reader.error());
    equalCP(playback,rows.front(),"Actual RPL initial full CP");while(!reader.atEnd()){const auto t=playback.nextTick();
        witness={"closure-rpl-playback",checkpoint(playback),{},rows.at(t+1),frames.at(t)};const bool moved=reader.advance(playback);
        witness.actual=playback.checkpoint();require(moved,reader.error());++advances;equalCP(playback,rows.at(playback.nextTick()),"Actual RPL playback full CP");}
    for(auto t:{std::uint64_t{4},std::uint64_t{16}}){witness={"closure-rpl-seek",checkpoint(playback),{},rows.at(t),{}};const bool sought=reader.seek(playback,t);
        witness.actual=playback.checkpoint();require(sought,reader.error());equalCP(playback,rows.at(t),"Actual RPL seek full CP");}
    std::ofstream trace(output/"tick-hashes.txt",std::ios::binary);for(const auto& row:rows)trace<<row.nextTick<<' '<<detCheckpointHash(row)<<'\n';trace.close();require(bool(trace),"Write tick hash trace");
    saveBytes(output/"initial.state",encodeDetCheckpoint(rows.front()));saveBytes(output/"final.state",encodeDetCheckpoint(rows.back()));
    std::ofstream raw(output/"canonical-inputs.bin",std::ios::binary);for(const auto& in:frames){const auto data=encodeDetInput(in);const auto size=static_cast<std::uint32_t>(data.size());
        for(unsigned n=0;n<4;++n)raw.put(static_cast<char>(size>>(8*n)));raw.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));}
    raw.close();require(bool(raw),"Write exact framed original canonical input");
    const auto immutable=bytes(restored);const auto schema=detBrainStateSchema2D(c);const auto field=[&](auto f){return detBrainField2D(c,10,f);};
    for(unsigned n=0;n<8;++n){auto bad=rows[4];switch(n){case 0:writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainMode),std::uint32_t{7});break;
        case 1:writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainLastDecisionTick),std::uint64_t{100});break;
        case 2:writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainObservedAttackProcessedTick),std::uint64_t{4});break;
        case 3:writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainAttackPulseCount),std::uint64_t{5});break;
        case 4:bad.globals.at(c.policySchemaBase).at(3)^=1;break;
        case 5:writeDetState(schema,bad.actors.at(10).blocks.at(c.stateSchema),field(DetBrainDecisionCount),std::uint64_t{1});break;
        case 6:writeDetState(detCharacterStateSchema2D(c.character),bad.actors.at(10).blocks.at(c.character.stateSchema),DetCharacterTargetSpeed,D::fromInt(1));break;
        case 7:writeDetState(schema,bad.globals.at(c.stateSchema),field(DetBrainObservedTargetPresent),false);break;}
        witness={"invalid-brain-restore",checkpoint(restored),{},bad,{}};const bool accepted=restored.restore(bad);witness.actual=restored.checkpoint();
        require(!accepted && bytes(restored)==immutable,"Malformed registered Brain record/policy/input rejected atomically case="+std::to_string(n));++negativeRestores;}
    auto foreign=c;foreign.owners[0].patrolSpeed=D::fromInt(2);DeterministicSession different(sessionConfig());require(configure(different,foreign),different.error());
    const auto prior=bytes(different);require(!different.restore(rows.back()) && bytes(different)==prior,"Complete literal decision policy changes manifest and forbids foreign restore");++negativeRestores;
    std::cout<<"PASS one16tick/17CP reverse/restore/late/effects/actual RPL2seek lane and atomic record rejection\n";}
void limits(){auto c=policy();witness={"invalid-brain-configuration",{},{},{},{}};
    for(unsigned n=0;n<9;++n){auto bad=c;switch(n){case 0:bad.owners.clear();break;case 1:bad.owners[0].target=10;break;case 2:bad.owners[0].target=30;break;
        case 3:bad.owners[0].senseHalfExtent.x=D::fromBits(0x7fc00000u);break;case 4:bad.owners[0].patrolLeft=bad.owners[0].patrolRight;break;
        case 5:bad.owners[0].chaseSpeed=D::fromInt(5);break;case 6:bad.owners[0].attackDistance=D::fromInt(5);break;
        case 7:bad.systemId=bad.attack.systemId;break;case 8:bad.stateSchema=bad.attack.stateSchema;break;}
        DeterministicSession s(sessionConfig());require(!installDetBrain2D(s,bad) && !s.faulted(),"Invalid finite Brain policy rejected before registration case="+std::to_string(n));++negativeConfigs;}
    for(std::uint8_t setter:{std::uint8_t{0},std::uint8_t{1}}){auto p=c;DeterministicSession s(sessionConfig());require(configure(s,p,false,{D{},D::fromInt(100)}),s.error());
        const auto initial=checkpoint(s);const auto before=bytes(s);auto in=frame(0);extra(in,3,setter);witness={"foreign-owner-input",initial,{},{},in};
        const bool moved=s.advance(in);witness.actual=s.checkpoint();require(!moved && s.faulted(),"External input cannot replace persistent Brain-owned speed or Attack held");
        require(s.restore(initial) && bytes(s)==before,"Explicit recovery from foreign persistent-input fault restores whole CP");++negativeRestores;}
    // Descriptor bounds do not promise that every dependency maximum can jointly seal.
    auto maximum=c;maximum.owners.clear();maximum.attack.owners.clear();auto& v=maximum.attack.vitality;v.actors.clear();v.progress.counters.clear();v.permissions.clear();
    for(unsigned n=0;n<8;++n){const SimEntityId id=10*(n+1);v.actors.push_back({id,n+1,0});v.progress.counters.push_back({n+1,0,20,20});v.permissions.push_back({n+1,id,DetVitalityKind2D::Hurt,1});}
    for(unsigned n=0;n<8;++n){const SimEntityId id=10*(n+1);DetAttackOwner2D a;a.actor=id;a.halfExtent={q(1),q(1)};
        for(unsigned t=0;t<8;++t)if(t!=n)a.targets.push_back({10*(t+1),t+1,1});maximum.attack.owners.push_back(a);
        auto b=c.owners.front();b.actor=id;b.target=10*((n+1)%8+1);maximum.owners.push_back(b);}
    require(detBrainStateSchema2D(maximum).fields.size()==121 && encodeDetBrainConfig2D(maximum).size()<=kDetBrain2DMaxPolicyBytes
        && detBrainPolicySchemas2D(maximum).size()<=19,"Eight-owner descriptor/full literal policy bounds");
    maximum.owners.push_back(maximum.owners.back());DeterministicSession ninth(sessionConfig());require(!installDetBrain2D(ninth,maximum),"Ninth Brain owner rejected");++negativeConfigs;
    std::cout<<"PASS bounded config/policy descriptors and persistent-input authority\n";}
std::string json(const std::string& text){std::string out="\"";for(const auto c:text){if(c=='\"' || c=='\\')out+='\\';
    if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(static_cast<unsigned char>(c)>=0x20)out+=c;}return out+'\"';}
void failed(const fs::path& out,const std::string& reason){if(out.empty())return;if(witness.before)saveBytes(out/"failure-before.state",encodeDetCheckpoint(*witness.before));
    if(witness.actual)saveBytes(out/"failure-actual.state",encodeDetCheckpoint(*witness.actual));if(witness.expected)saveBytes(out/"failure-expected.state",encodeDetCheckpoint(*witness.expected));
    if(witness.input)saveBytes(out/"failure-canonical-input.bin",encodeDetInput(*witness.input));std::ofstream f(out/"failure.json",std::ios::binary);
    f<<"{\"status\":\"failed\",\"phase\":"<<json(witness.phase)<<",\"reason\":"<<json(reason);if(witness.input)f<<",\"tick\":\""<<witness.input->tick<<'\"';
    if(witness.expected && witness.actual){const auto diff=firstDetDifference(*witness.expected,*witness.actual);if(diff)f<<",\"firstDifference\":{\"section\":"<<json(diff->section)
        <<",\"entity\":\""<<diff->entity<<"\",\"component\":"<<diff->component<<",\"field\":"<<diff->field<<",\"lane\":"<<diff->lane<<",\"expected\":\""<<diff->expected<<"\",\"actual\":\""<<diff->actual<<"\"}";}
    f<<"}\n";f.close();require(bool(f),"Write failure witness");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto path=parent/("brain2d-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("No fresh artifact directory");}
} // namespace
int main(int argc,char** argv){fs::path output;try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];
    if(arg=="--output" && n+1<argc && parent.empty())parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_Brain2DChecks --output EXISTING_PARENT");}
    output=fresh(parent);selected();absence();closure(output);limits();std::ofstream summary(output/"summary.json",std::ios::binary);
    summary<<"{\"status\":\"passed\",\"profile\":1,\"closureTicks\":16,\"closureCpRows\":17,\"rplFiles\":1,\"seeks\":2,\"handRows\":\""<<handRows
        <<"\",\"advanceCalls\":\""<<advances<<"\",\"cpComparisons\":\""<<cpComparisons<<"\",\"negativeRestores\":\""<<negativeRestores<<"\",\"negativeConfigs\":\""<<negativeConfigs
        <<"\",\"confirmedEvents\":\""<<confirmedEvents<<"\",\"rollbacks\":\""<<rollbacks<<"\",\"replayedTicks\":\""<<replayedTicks
        <<"\",\"oracleScope\":\"selected hand Brain/previous-Attack/window/HP/geometry/deceleration/dead/retired checks; entire CP actual execution equality\"}\n";
    summary.close();require(bool(summary),"Write completed summary");std::cout<<"PASS Stage53 Brain; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){try{failed(output,e.what());}catch(const std::exception& logging){std::cerr<<"Witness write failed: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Stage53 Brain: "<<e.what()<<'\n';return 1;}}
