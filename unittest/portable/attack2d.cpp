// design reference: AYEntity/design.md Stage51 finite registered attack windows.
// Selected hand expectations do not independently generate a CP.
#include <AYEntity/DeterministicAttack2D.h>
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
D q(int numerator){return D::fromInt(numerator)/D::fromInt(4);}
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
std::uint64_t advances=0,handRows=0,negativeRestores=0,negativeConfigs=0;
DetSessionConfig sessionConfig(){return {51,1,1,4,51};}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,const DetTickInput& input,const std::string& phase){
    witness={phase,checkpoint(s),{},{},input};const bool ok=s.advance(input);witness.actual=s.checkpoint();require(ok,s.error());++advances;
}
void equalCP(DeterministicSession& s,const DetSessionCheckpoint& expected,const std::string& why){
    witness.expected=expected;witness.actual=checkpoint(s);require(encodeDetCheckpoint(*witness.actual)==encodeDetCheckpoint(expected),why);
}
DetAttack2DConfig policy(){DetAttack2DConfig c;auto& v=c.vitality;v.progress.stateSchema=176;v.progress.policySchemaBase=177;
    v.progress.validatorId=v.progress.logicProfileId=76;v.progress.actions.collision.extended=true;
    // An empty Actions table confers no gate/path authority; it still binds collision.
    v.progress.actions.targets.clear();for(unsigned n=0;n<5;++n){const auto actor=SimEntityId{10*(n+1)};
        v.progress.counters.push_back({n+1,0,20,20});v.actors.push_back({actor,n+1,0});}
    v.permissions={{1,20,DetVitalityKind2D::Hurt,20},{2,30,DetVitalityKind2D::Hurt,20},{3,40,DetVitalityKind2D::Hurt,20},
        {4,10,DetVitalityKind2D::Hurt,20},{5,50,DetVitalityKind2D::Hurt,20}};
    c.owners={{10,{q(2),q(2)},{D::fromInt(1),D{}},2,2,2,{{20,1,3},{30,2,3},{40,3,3}}}};return c;
}
DetActorState actor(const DetAttack2DConfig& c,SimEntityId id){DetActorState a;D x;
    switch(id){case 10:x=D{};break;case 20:x=D::fromInt(1);break;case 30:x=D::fromInt(-1);break;
        case 40:x=q(7);break;case 50:x=q(5);break;case 60:x=D{};break;default:throw std::invalid_argument("Unknown fixture actor");}
    DetSimTransformComponent p;require(p.setPosition({x,D{},D{}}),"Fixture pose");a.pose=p.snapshot();const bool trigger=id==60;
    const auto& collision=c.vitality.progress.actions.collision;a.blocks[collision.bodySchema]=detCollisionBodyState2D(collision,
        {trigger?DetBodyMode2D::Static:DetBodyMode2D::Kinematic,trigger?V{q(2),q(2)}:V{q(1),q(1)},V{},V{},id==10?1u:2u,id==10?2u:1u,trigger});
    if(c.vitality.respawnRecovery){DetCharacterState2D state;state.enabled=true;
        const auto& character=c.vitality.respawnFlow->respawn.character;a.blocks[character.stateSchema]=detCharacterState2D(character,state);}
    return a;
}
enum class Setup {Normal,MaskedTarget,ForeignPermission,LateSetter};
bool registerInput(DeterministicSession& s,const DetAttack2DConfig& c){return s.registerSystem(4,c.vitality.progress.actions.collision.priority,[c](DetTickContext& ctx){
    for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
        if(cmd.type==1 && cmd.payload.size()==3 && cmd.payload[1]<=1 && cmd.payload[2]<=1)
            detSetAttackInput2D(ctx,c,cmd.payload[0],{bool(cmd.payload[1]),bool(cmd.payload[2])});
        else if(cmd.type==2 && cmd.payload.size()==2)(void)executeDetVitality2D(ctx,c.vitality,cmd.payload[0],cmd.payload[1]);
        else if(cmd.type==3 && cmd.payload.size()==1 && c.vitality.respawnFlow)
            detCharacterRespawnFlowInput2D(ctx,cmd.payload[0],*c.vitality.respawnFlow,true);
        else if(cmd.type==99 && cmd.payload==Bytes{51})continue;
        else return false;}
    return true;});}
bool configure(DeterministicSession& s,const DetAttack2DConfig& c,bool reverse=false,Setup setup=Setup::Normal){
    // A setup failure has no checkpoint from the previously configured lane.
    witness={"configuration",{},{},{},{}};
    if(reverse && !registerInput(s,c))return false;
    if(!installDetActions2D(s,c.vitality.progress.actions) || !installDetProgress2D(s,c.vitality.progress))return false;
    if(c.vitality.respawnRecovery){if(!installDetCharacterRespawn2D(s,c.vitality.respawnFlow->respawn)
        || !installDetCharacterRespawnFlow2D(s,*c.vitality.respawnFlow))return false;}
    else if(!installDetCollision2D(s,c.vitality.progress.actions.collision))return false;
    auto installed=c.vitality;if(setup==Setup::ForeignPermission)++installed.permissions[0].maxAmount;
    if(!installDetVitality2D(s,installed) || !installDetAttack2D(s,c))return false;
    if(!reverse && !registerInput(s,c))return false;
    if(setup==Setup::LateSetter && !s.registerSystem(8,c.vitality.progress.actions.collision.priority,[c](DetTickContext& ctx){
        detSetAttackInput2D(ctx,c,10,{false,true});return true;}))return false;
    std::vector<SimEntityId> ids;for(const auto& hp:c.vitality.actors)ids.push_back(hp.actor);
    if(!c.vitality.respawnRecovery)ids.push_back(60);if(reverse)std::reverse(ids.begin(),ids.end());
    for(auto id:ids){auto a=actor(c,id);if(setup==Setup::MaskedTarget && id==20)
        writeDetState(detCollisionBodySchema2D(c.vitality.progress.actions.collision),a.blocks.at(c.vitality.progress.actions.collision.bodySchema),DetBodyMask,std::uint32_t{0});
        if(!s.addEntity(id,std::move(a)))return false;}return s.seal();
}
DetTickInput frame(std::uint64_t tick,bool held=true,bool facingRight=true,SimEntityId owner=10){
    return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(owner),static_cast<std::uint8_t>(held),static_cast<std::uint8_t>(facingRight)}},{1,1,99,{51}}}};
}
void sample(DetTickInput& t,SimEntityId owner,bool held,bool right){t.commands.push_back({1,static_cast<std::uint32_t>(t.commands.size()),1,
    {static_cast<std::uint8_t>(owner),static_cast<std::uint8_t>(held),static_cast<std::uint8_t>(right)}});}
void hurt(DetTickInput& t,unsigned permission,unsigned amount){t.commands.push_back({1,static_cast<std::uint32_t>(t.commands.size()),2,
    {static_cast<std::uint8_t>(permission),static_cast<std::uint8_t>(amount)}});}
void manual(DetTickInput& t,SimEntityId id=10){t.commands.push_back({1,static_cast<std::uint32_t>(t.commands.size()),3,{static_cast<std::uint8_t>(id)}});}
std::vector<DetTickInput> inputs(){std::vector<DetTickInput> result;for(unsigned t=0;t<16;++t)
    result.push_back(frame(t,t!=1 && t!=6,t==0));return result;}
auto hp(DeterministicSession& s,const DetAttack2DConfig& c,SimEntityId id){return detVitalityState2D(checkpoint(s),c.vitality,id);}
auto attack(DeterministicSession& s,const DetAttack2DConfig& c,SimEntityId id=10){return detAttackState2D(checkpoint(s),c,id);}
auto result(DeterministicSession& s,const DetAttack2DConfig& c,SimEntityId id=10){return detAttackResult2D(checkpoint(s),c,id);}
void golden(DeterministicSession& s,const DetAttack2DConfig& c,unsigned tick){
    // Explicit processed-tick table, not a reimplementation of a stage function.
    static constexpr std::array<DetAttackPhase2D,16> phases{DetAttackPhase2D::Warmup,DetAttackPhase2D::Warmup,
        DetAttackPhase2D::Active,DetAttackPhase2D::Active,DetAttackPhase2D::Recovery,DetAttackPhase2D::Recovery,DetAttackPhase2D::Idle,
        DetAttackPhase2D::Warmup,DetAttackPhase2D::Warmup,DetAttackPhase2D::Active,DetAttackPhase2D::Active,
        DetAttackPhase2D::Recovery,DetAttackPhase2D::Recovery,DetAttackPhase2D::Idle,DetAttackPhase2D::Idle,DetAttackPhase2D::Idle};
    const auto a=attack(s,c);const auto r=result(s,c);require(a.phase==phases[tick] && a.attackCount==(tick<7?1u:2u),"Hand W2/A2/R2 processed-tick and held-busy table tick="+std::to_string(tick));
    require(a.lockedFacingRight==(tick<7),"Locked facing survives later sampled facing and a busy rising edge");
    require(hp(s,c,20).hp==(tick<2?20u:17u) && hp(s,c,30).hp==(tick<9?20u:17u),"Hand right/left damage exactly at ticks2/9");
    require(hp(s,c,40).hp==20 && hp(s,c,50).hp==20,"Strict touching face and overlapping non-whitelisted actor remain unharmed");
    require(r.attemptCount==(tick<2?0u:(tick<9?1u:2u)),"At most one candidate per target per cycle");
    if(tick==2 || tick==3)require(a.hitMask==1 && r.target==20 && r.tick==2 && r.actual==3 && r.hpBefore==20 && r.hpAfter==17,"Right hand target bit and actual historical Hurt");
    if(tick==9 || tick==10)require(a.hitMask==2 && r.target==30 && r.tick==9 && r.actual==3,"Left hand target bit");++handRows;
}
void windows(){auto c=policy();DeterministicSession s(sessionConfig());require(configure(s,c),s.error());const auto frames=inputs();
    for(unsigned t=0;t<16;++t){advance(s,frames[t],"hand-windows");golden(s,c,t);}
    require(attack(s,c).attackCount==2 && !attack(s,c).cancelCount,"Held through Idle never auto-repeats");
    auto masked=policy();masked.owners[0].warmupTicks=0;masked.owners[0].activeTicks=1;masked.owners[0].recoveryTicks=0;
    DeterministicSession mask(sessionConfig());require(configure(mask,masked,false,Setup::MaskedTarget),mask.error());advance(mask,frame(0),"masked-hand");
    require(!result(mask,masked).attemptCount && hp(mask,masked,20).hp==20,"Current reciprocal mask0 prevents whitelist candidate");
    std::cout<<"PASS hand windows, held/busy release, locked mirror, strict face, whitelist and mask\n";
}
void denied(){for(bool budget:{false,true}){auto c=policy();c.owners[0].warmupTicks=0;c.owners[0].activeTicks=5;c.owners[0].recoveryTicks=0;
    c.owners[0].targets={{20,1,3}};if(budget)c.vitality.maxOperationsPerTick=1;else c.vitality.actors[1].invulnerabilityTicks=3;
    DeterministicSession s(sessionConfig());require(configure(s,c),s.error());for(unsigned t=0;t<6;++t){auto in=frame(t);if(!t)hurt(in,1,1);advance(s,in,budget?"budget-denial":"protected-denial");
        require(hp(s,c,20).hp==19 && result(s,c).attemptCount==1 && result(s,c).actual==0,"Normal denial consumes hit bit; protection expiry/fresh budget cannot retry");
        require(result(s,c).code==(budget?DetVitalityCode2D::BudgetExceeded:DetVitalityCode2D::Invulnerable) && attack(s,c).hitMask==1,"Exact recorded denial and consumed bit");++handRows;}}
    std::cout<<"PASS normal Invulnerable/BudgetExceeded consume hit without delayed retry\n";
}
DetAttack2DConfig tradePolicy(bool recovery){auto c=policy();auto& v=c.vitality;v.actors={{10,1,0},{20,2,0}};v.progress.counters={{1,0,3,3},{2,0,3,3}};
    v.permissions={{1,20,DetVitalityKind2D::Hurt,3},{2,10,DetVitalityKind2D::Hurt,3}};
    c.owners={{10,{q(2),q(2)},{D::fromInt(1),D{}},0,4,0,{{20,1,3}}},
        {20,{q(2),q(2)},{D::fromInt(1),D{}},0,4,0,{{10,2,3}}}};
    if(recovery){v.progress.actions.collision.oneWayPlatforms=true;v.progress.actions.collision.characterRespawn=true;
        DetCharacterRespawnFlow2DConfig f;f.respawn.character.advanced=true;f.respawn.character.gravity={};f.respawn.character.collision=v.progress.actions.collision;
        f.respawn.spawns={{1,10,{D::fromInt(-4),D{}}},{2,20,{D::fromInt(4),D{}}}};f.bindings={{10,1},{20,2}};
        f.externalDeaths=true;f.externalDeathPermissions={{1,10,v.systemId},{2,20,v.systemId}};v.respawnRecovery=true;v.respawnFlow=f;
        v.recoveries={{10,1,3,true},{20,2,3,false}};}
    return c;
}
void trades(){for(bool recovery:{false,true}){auto c=tradePolicy(recovery);DeterministicSession s(sessionConfig());require(configure(s,c),s.error());
    auto in=frame(0);sample(in,20,true,false);advance(s,in,recovery?"recovering-trade":"alive-trade");
    require(result(s,c,10).actual==3 && result(s,c,20).actual==3 && hp(s,c,10).deathCount==1 && hp(s,c,20).deathCount==1,"One initial alive snapshot allows real mutual lethal hits");
    require(hp(s,c,10).hp==(recovery?3u:0u) && hp(s,c,20).hp==(recovery?3u:0u),"Only explicit actual accepted recovery returns trade HP");
    if(recovery)for(auto id:{SimEntityId{10},SimEntityId{20}}){const auto r=detVitalityRecoveryState2D(checkpoint(s),c.vitality,id);
        require(r.restoreCount==1 && r.reviveCount==1 && r.restoreWriteCount==1 && r.lastRestoreKind==DetRespawnFlowRequestKind2D::ExternalDeath,"Real Vitals6 to Flow7 accepted post restores lethal trade");}
    in=frame(1);sample(in,20,true,false);advance(s,in,"trade-next-cancel");for(auto id:{SimEntityId{10},SimEntityId{20}})
        require(attack(s,c,id).phase==DetAttackPhase2D::Idle && attack(s,c,id).cancelCount==1 && result(s,c,id).attemptCount==1
            && result(s,c,id).lastCancelReason==(recovery?DetAttackCancel2D::Recovered:DetAttackCancel2D::Dead),"Next Attack cancels dead/new-generation cycle without extra HP attempt");handRows+=4;}
    auto c=tradePolicy(true);c.owners.resize(1);c.owners[0].warmupTicks=2;c.owners[0].targets[0].amount=1;
    DeterministicSession equal(sessionConfig());require(configure(equal,c),equal.error());advance(equal,frame(0),"equal-hp-start");auto m=frame(1);manual(m);
    advance(equal,m,"equal-hp-accepted");const auto rec=detVitalityRecoveryState2D(checkpoint(equal),c.vitality,10);
    require(rec.restoreCount==1 && !rec.restoreWriteCount && !rec.reviveCount && hp(equal,c,10).hp==3,"Same-HP Accepted advances restoration generation without a numeric HP write");
    advance(equal,frame(2),"equal-hp-cancel");require(attack(equal,c).phase==DetAttackPhase2D::Idle && attack(equal,c).cancelCount==1
        && attack(equal,c).recoveryGeneration==1 && result(equal,c).lastCancelReason==DetAttackCancel2D::Recovered && !result(equal,c).attemptCount,"Explicit copied restoreCount cancels warmup before first active attempt");
    advance(equal,frame(3),"equal-hp-held");require(attack(equal,c).attackCount==1 && hp(equal,c,20).hp==3,"Held after recovered cancellation never restarts");handRows+=4;
    std::cout<<"PASS initial-alive lethal trade, dead/fresh generation cancel, same-HP accepted normalization\n";
}
void saveBytes(const fs::path& path,const Bytes& data){std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));file.close();require(bool(file),"Write "+path.filename().string());}
std::uint64_t confirmedEvents=0,rollbackCount=0,replayedTicks=0;
void closure(const fs::path& output){const auto c=policy();const auto frames=inputs();DeterministicSession base(sessionConfig());require(configure(base,c),base.error());
    std::vector<DetSessionCheckpoint> cps{checkpoint(base)};std::vector<DetConfirmedEvent> events;
    for(unsigned t=0;t<16;++t){advance(base,frames[t],"closure-baseline");golden(base,c,t);cps.push_back(checkpoint(base));
        for(const auto& event:cps.back().pendingEvents)events.push_back({1,t,event});}
    require(!events.empty(),"Closure must produce real collision events, not a vacuous confirmed-effects comparison");confirmedEvents=events.size();
    // The counter/HP/window checks above are independent selected hand assertions.
    // Below compare complete actual CP bytes across execution paths; static policy,
    // transform history and every other channel are NOT independently regenerated.
    DeterministicSession reverse(sessionConfig());require(configure(reverse,c,true),reverse.error());equalCP(reverse,cps[0],"Reverse registration initial entire CP");
    for(unsigned t=0;t<16;++t){auto in=frames[t];std::reverse(in.commands.begin(),in.commands.end());advance(reverse,in,"closure-reverse");
        equalCP(reverse,cps[t+1],"Reverse actor/system registration and input-vector order entire CP");}
    DeterministicSession restored(sessionConfig());require(configure(restored,c,true) && restored.restore(cps[3]),restored.error());
    for(unsigned t=3;t<16;++t){advance(restored,frames[t],"closure-restored");equalCP(restored,cps[t+1],"Restore active cycle with consumed target continues complete CP");}
    const auto immutableBefore=bytes(restored);const auto schemas=detAttackStateSchemas2D(c);const auto& stateSchema=schemas.at(0);const auto& ledgerSchema=schemas.at(1);
    for(unsigned n=0;n<9;++n){auto bad=cps[3];const auto field=[&](auto member){return detAttackField2D(c,10,member);};switch(n){
        case 0:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetAttackPhase),std::uint32_t{4});break;
        case 1:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetAttackStartTick),std::uint64_t{100});break;
        case 2:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetAttackHitMask),std::uint32_t{8});break;
        case 3:writeDetState(stateSchema,bad.globals.at(c.stateSchema),DetAttackProcessedTick,std::uint64_t{0});break;
        case 4:writeDetState(ledgerSchema,bad.globals.at(c.ledgerSchema),detAttackLedgerField2D(c,10,DetAttackAttemptCount),std::uint64_t{0});break;
        case 5:bad.globals.at(c.policySchemaBase).at(3)^=1;break;
        case 6:writeDetState(stateSchema,bad.actors.at(10).blocks.at(c.stateSchema),field(DetAttackHeld),true);break;
        case 7:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetAttackRecoveryGeneration),std::uint64_t{1});break;
        case 8:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetAttackCancelCount),std::uint64_t{1});break;}
        witness={"invalid-restore",checkpoint(restored),{},bad,{}};require(!restored.restore(bad),"Forged attack policy/mirror/window/ledger CP rejected case="+std::to_string(n));
        require(bytes(restored)==immutableBefore,"Rejected attack restore leaves entire current CP unchanged");++negativeRestores;}
    auto different=c;different.owners[0].warmupTicks=3;DeterministicSession other(sessionConfig());require(configure(other,different),other.error());
    const auto beforeOther=bytes(other);require(other.manifest()!=base.manifest() && !other.restore(cps[3]) && bytes(other)==beforeOther,"Literal warmup policy changes manifest and rejects incompatible restore atomically");++negativeRestores;
    DeterministicSession corrected(sessionConfig());require(configure(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;
    // Complete button samples can Hold; metadata is harmless Omit. No one-shot
    // manual or Hurt request is held in this representative sequence.
    rc.prediction={{1,DetPredictionMode::Hold},{99,DetPredictionMode::Omit}};DeterministicRollback rollback(corrected,rc);
    for(unsigned t=0;t<16;++t){
        if(t==7){witness={"closure-predicted-advance",checkpoint(corrected),{},cps[t+1],{}};
            const bool predicted=rollback.advance();require(predicted,rollback.error());
            // The real contribution belongs to tick7, but submission starts from
            // the newly predicted CP8; do not bind it to the earlier CP7 or guess.
            witness={"closure-late-member-submit",checkpoint(corrected),{},cps[t+1],frames[t]};
            const bool submitted=rollback.submit(1,frames[t]);witness.actual=corrected.checkpoint();require(submitted,rollback.error());}
        else{witness={"closure-member-submit",checkpoint(corrected),{},cps[t+1],frames[t]};
            const bool submitted=rollback.submit(1,frames[t]);require(submitted,rollback.error());
            witness={"closure-confirmed-advance",checkpoint(corrected),{},cps[t+1],frames[t]};
            const bool moved=rollback.advance();witness.actual=corrected.checkpoint();require(moved,rollback.error());}
        ++advances;equalCP(corrected,cps[t+1],"Real late held-to-press correction replays entire attack CP");}
    rollbackCount=rollback.rollbackCount();replayedTicks=rollback.replayedTicks();require(rollbackCount>0 && replayedTicks>0,"Delayed tick7 must actually roll back and replay");
    require(rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"Real trigger effects drain exactly once after actual confirmation");
    DeterministicSession recording(sessionConfig());require(configure(recording,c),recording.error());DetReplayWriter writer;
    const std::string replayPath=(output/"attack.rpl").string();const bool began=writer.begin(recording,replayPath,2);require(began,writer.error());
    for(unsigned t=0;t<16;++t){witness={"closure-rpl-record",checkpoint(recording),{},cps[t+1],frames[t]};require(writer.advance(frames[t]),writer.error());
        ++advances;equalCP(recording,cps[t+1],"Actual .rpl recorder complete CP");}require(writer.finish(),writer.error());
    DeterministicSession playback(sessionConfig());require(configure(playback,c,true),playback.error());DetReplayReader reader;
    // Keep this path string alive through every reader operation; no temporary
    // view is used by the replay opener or its later frame reads.
    const std::string recordedPath=writer.path();const bool opened=reader.open(recordedPath);require(opened,reader.error());
    const bool initialized=reader.restoreInitial(playback);require(initialized,reader.error());equalCP(playback,cps[0],"Actual .rpl initial restore");
    while(!reader.atEnd()){const auto t=playback.nextTick();witness={"closure-rpl-playback",checkpoint(playback),{},cps.at(t+1),frames.at(t)};
        require(reader.advance(playback),reader.error());++advances;equalCP(playback,cps.at(playback.nextTick()),"Actual .rpl playback entire CP");}
    for(auto t:{std::uint64_t{3},std::uint64_t{16}}){require(reader.seek(playback,t),reader.error());equalCP(playback,cps[t],"Actual .rpl seek active/final complete CP");}
    std::ofstream trace(output/"tick-hashes.txt",std::ios::binary);for(const auto& cp:cps)trace<<cp.nextTick<<' '<<detCheckpointHash(cp)<<'\n';
    trace.close();require(bool(trace),"Write bounded actual hash trace");saveBytes(output/"initial.state",encodeDetCheckpoint(cps.front()));saveBytes(output/"final.state",encodeDetCheckpoint(cps.back()));
    std::ofstream raw(output/"canonical-inputs.bin",std::ios::binary);for(const auto& in:frames){const auto encoded=encodeDetInput(in);const auto size=static_cast<std::uint32_t>(encoded.size());
        for(unsigned n=0;n<4;++n)raw.put(static_cast<char>(size>>(8*n)));raw.write(reinterpret_cast<const char*>(encoded.data()),static_cast<std::streamsize>(encoded.size()));}
    raw.close();require(bool(raw),"Write actual representative input frames");
    std::cout<<"PASS16tick/17CP reverse, active restore, real late rollback/effects, actual .rpl/2seek byte equivalence\n";
}
void limits(){auto c=policy();witness={"invalid-configuration",{},{},{},{}};for(unsigned n=0;n<9;++n){auto bad=c;switch(n){case 0:bad.owners[0].activeTicks=0;break;
        case 1:bad.owners[0].warmupTicks=65536;break;case 2:bad.owners[0].halfExtent.x=D::fromBits(0x7fc00000u);break;
        case 3:bad.owners[0].targets[0].permission=2;break;case 4:std::reverse(bad.owners[0].targets.begin(),bad.owners[0].targets.end());break;
        case 5:bad.stateSchema=bad.ledgerSchema;break;case 6:bad.policySchemaBase=bad.stateSchema;break;case 7:bad.systemId=bad.vitality.systemId;break;
        case 8:bad.owners[0].targets[0].amount=21;break;}
        DeterministicSession s(sessionConfig());require(!installDetAttack2D(s,bad) && !s.faulted(),"Malformed immutable attack authority rejected before destination registration case="+std::to_string(n));++negativeConfigs;}
    DeterministicSession wrong(sessionConfig());require(!configure(wrong,c,false,Setup::ForeignPermission),"Actually installed Vitality policy differs from complete declared nested policy");++negativeConfigs;
    DeterministicSession late(sessionConfig());require(configure(late,c,false,Setup::LateSetter),late.error());const auto initial=checkpoint(late);const auto before=bytes(late);
    witness={"late-input-fault",initial,{},{},frame(0)};require(!late.advance(frame(0)) && late.faulted(),"Attack input after processed system faults rather than altering the next sample");
    require(late.restore(initial) && bytes(late)==before,"Explicit recovery from late setter fault restores original entire CP");
    witness={"bounded-descriptors",{},{},{},{}};auto maximum=c;maximum.owners.clear();maximum.vitality.actors.clear();maximum.vitality.progress.counters.clear();maximum.vitality.permissions.clear();
    for(unsigned n=0;n<8;++n){const auto id=SimEntityId{10*(n+1)};maximum.vitality.actors.push_back({id,n+1,0});maximum.vitality.progress.counters.push_back({n+1,0,20,20});
        maximum.vitality.permissions.push_back({n+1,id,DetVitalityKind2D::Hurt,1});}
    for(unsigned n=0;n<8;++n){DetAttackOwner2D owner;owner.actor=10*(n+1);owner.halfExtent={q(1),q(1)};for(unsigned t=0;t<8;++t)if(t!=n)
        owner.targets.push_back({10*(t+1),t+1,1});maximum.owners.push_back(std::move(owner));}
    const auto descriptors=detAttackStateSchemas2D(maximum);require(descriptors.size()==2 && descriptors[0].fields.size()==121 && descriptors[1].fields.size()==97
        && encodeDetAttackConfig2D(maximum).size()<=kDetAttack2DMaxPolicyBytes && detAttackPolicySchemas2D(maximum).size()<=17,"Eight-owner phase/ledger and full policy bounds; joint Session capacity remains actual seal-dependent");
    maximum.owners.push_back(maximum.owners.back());DeterministicSession ninth(sessionConfig());require(!installDetAttack2D(ninth,maximum),"Ninth owner exceeds bounded configuration");++negativeConfigs;
    std::cout<<"PASS bounded authoring, actual nested policy, malformed restore and late authority\n";
}
std::string json(const std::string& text){std::string output="\"";for(const auto c:text){if(c=='\"' || c=='\\')output+='\\';
    if(c=='\n')output+="\\n";else if(c=='\r')output+="\\r";else if(c=='\t')output+="\\t";else if(static_cast<unsigned char>(c)>=0x20)output+=c;}
    return output+'\"';}
void failed(const fs::path& output,const std::string& reason){if(output.empty())return;
    if(witness.before)saveBytes(output/"failure-before.state",encodeDetCheckpoint(*witness.before));
    if(witness.actual)saveBytes(output/"failure-actual.state",encodeDetCheckpoint(*witness.actual));
    if(witness.expected)saveBytes(output/"failure-expected.state",encodeDetCheckpoint(*witness.expected));
    if(witness.input)saveBytes(output/"failure-canonical-input.bin",encodeDetInput(*witness.input));
    std::ofstream record(output/"failure.json",std::ios::binary);record<<"{\"status\":\"failed\",\"phase\":"<<json(witness.phase)<<",\"reason\":"<<json(reason);
    if(witness.input)record<<",\"tick\":\""<<witness.input->tick<<'\"';if(witness.expected && witness.actual){const auto diff=firstDetDifference(*witness.expected,*witness.actual);
        if(diff)record<<",\"firstDifference\":{\"section\":"<<json(diff->section)<<",\"entity\":\""<<diff->entity<<"\",\"component\":"<<diff->component
            <<",\"field\":"<<diff->field<<",\"lane\":"<<diff->lane<<",\"expected\":\""<<diff->expected<<"\",\"actual\":\""<<diff->actual<<"\"}";}
    record<<"}\n";record.close();require(bool(record),"Write failure witness");
}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto p=parent/("attack2d-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}
    throw std::runtime_error("No fresh artifact directory");}
} // namespace
int main(int argc,char** argv){fs::path output;try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];
    if(arg=="--output" && n+1<argc && parent.empty())parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_Attack2DChecks --output EXISTING_PARENT");}
    output=fresh(parent);windows();denied();trades();closure(output);limits();std::ofstream summary(output/"summary.json",std::ios::binary);
    summary<<"{\"status\":\"passed\",\"profile\":1,\"closureTicks\":16,\"closureCpRows\":17,\"rplFiles\":1,\"seeks\":2,\"handRows\":\""<<handRows
        <<"\",\"advanceCalls\":\""<<advances<<"\",\"negativeRestores\":"<<negativeRestores<<",\"negativeConfigs\":"<<negativeConfigs
        <<",\"confirmedEvents\":\""<<confirmedEvents<<"\",\"rollbacks\":\""<<rollbackCount<<"\",\"replayedTicks\":\""<<replayedTicks
        <<"\",\"oracleScope\":\"selected hand phase/HP/geometry/denial/trade/recovery checks; entire CP actual execution equality\"}\n";
    summary.close();require(bool(summary),"Write completed summary");std::cout<<"PASS Stage51 attack; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){try{failed(output,e.what());}catch(const std::exception& logging){std::cerr<<"Witness write failed: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Stage51 attack: "<<e.what()<<'\n';return 1;}}
